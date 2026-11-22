// Every control law, tagged with what supports it:
//   [settled]  a Roland document gives the number
//   [measured] a published black-box measurement of the hardware
//   [inferred] chosen here; no source pins it. Replace with a capture.
// Docs/research/hardware-model-2026-11-22.md lists them in one table.
#pragma once

#include "Core.h"
#include "HypraPatch.h"

#include <array>

namespace hypra::mapping
{
// [measured] A. Szabo, "How to Emulate the Super Saw", KTH 2010: frequency
// offsets of the seven saws at full DETUNE, from a JP-8000.
inline constexpr std::array<double, 7> superOffsets {
    -0.11002313, -0.06288439, -0.01952356, 0.0, 0.01991221, 0.06216538, 0.10745242
};

namespace detail
{
// Linear interpolation through a measurement every eighth step of a knob.
template <std::size_t N>
inline double table (const std::array<double, N>& points, double knob) noexcept
{
    const double at = std::clamp (knob, 0.0, 1.0) * 127.0;
    const std::size_t i = std::min<std::size_t> (static_cast<std::size_t> (at / 8.0), N - 2);
    const double lo = 8.0 * static_cast<double> (i), hi = i + 2 == N ? 127.0 : lo + 8.0;
    const double low = lo == 0.0 ? 0.0 : lo - 1.0, high = i + 2 == N ? 127.0 : hi - 1.0;
    return points[i] + (points[i + 1] - points[i]) * (at - low) / (high - low);
}

// The same for points at 0, 8 ... 120 and 127.
inline double table8 (const std::array<double, 17>& points, double knob) noexcept
{
    const double at = std::clamp (knob, 0.0, 1.0) * 127.0;
    const std::size_t i = std::min<std::size_t> (static_cast<std::size_t> (at / 8.0), 15);
    const double low = 8.0 * static_cast<double> (i), high = i == 15 ? 127.0 : low + 8.0;
    return points[i] + (points[i + 1] - points[i]) * (at - low) / (high - low);
}
} // namespace detail

// [measured] A. Szabo, "How to Emulate the Super Saw", KTH 2010, Table 2:
// DETUNE at MIDI 0, 7, 15 ... 119, 127, from a JP-8000 and a JP-8080.
// Interpolated, not his 11th-order fit, which misses the table by up to 4%
// and turns back near the middle. His 0 at MIDI 0 is his zero; a JP-8080
// recording gives 0.0027 there (Docs/research/hardware-ab-2026-11-22.md).
inline constexpr std::array<double, 17> detuneTable {
    0.0027, 0.00967268, 0.0220363, 0.0339636, 0.0467636, 0.0591273, 0.0714909, 0.0838545,
    0.0967273, 0.121527, 0.147127, 0.193455, 0.243418, 0.2933815, 0.343345, 0.3928, 1.0
};
inline double superDetune (double knob) noexcept { return detail::table (detuneTable, knob); }

// [measured] Szabo Table 3: centre and side levels against MIX, same points.
inline constexpr std::array<double, 17> centreTable {
    1.0, 0.965, 0.93, 0.901, 0.86, 0.83, 0.795, 0.76, 0.72, 0.69, 0.65, 0.62, 0.585, 0.55, 0.51, 0.48, 0.445
};
inline constexpr std::array<double, 17> sideTable {
    0.03836, 0.12, 0.19, 0.25, 0.31, 0.37, 0.42, 0.46, 0.5, 0.53, 0.56, 0.58, 0.59, 0.6, 0.605, 0.6, 0.59
};
inline double superCentre (double mix) noexcept { return detail::table (centreTable, mix); }
inline double superSide (double mix) noexcept { return detail::table (sideTable, mix); }

// [measured] JP-8080 recording, Docs/research/hardware-ab-2026-11-22.md.
inline constexpr double superHighPassRatio = 0.904;
inline constexpr double superHighPassQ = 0.878;

// [measured, two notes of factory performance P14] the feedback loop runs
// this many samples short of one period, so its howl beats against the
// harmonic it sits beside.
inline constexpr double feedbackLoopShortfall = 2.5;

// The laws below marked [measured, black box] come from JE-8086 2.2.27
// running JP-8000 system 1.05, played over MIDI and analysed from its output
// only (Docs/research/black-box-je8086-2026-11-22.md).

// [measured, black box] Pulse width: 50 % at 0, plus 1/256 per step.
inline double pulseDuty (double width) noexcept { return 0.5 + std::clamp (width, 0.0, 1.0) * 127.0 / 256.0; }

// [measured, black box] Saw SHAPE adds K (1/n^2) to every harmonic of the
// saw. K at 0, 8 ... 120, 127; the values at 40 and 120 are interpolated.
inline constexpr std::array<double, 17> sawShapeTable {
    1.267, 0.904, 0.575, 0.307, -0.005, -0.306, -0.607, -0.914, -1.210,
    -1.529, -1.822, -2.115, -2.418, -2.731, -3.035, -3.290, -3.523
};
inline double sawShapeK (double shape) noexcept { return detail::table8 (sawShapeTable, shape); }

// [measured, black box] Triangle mod wraps the triangle times this gain.
inline double triangleModGain (double offset) noexcept
{
    return std::clamp (2.1175 - std::clamp (offset, 0.0, 1.0) * 127.0 / 128.0, 1.375, 2.015);
}

// [measured, black box] The filter coefficient rises a semitone per step of
// CUTOFF FREQ and is 0.039016 (548 Hz) at 64; it stops at 1. "Octaves" here
// are twelve steps of the control.
inline double cutoffOctaves (int value) noexcept { return std::clamp (value, 0, 127) / 12.0; }
inline double cutoffCoefficient (double octaves) noexcept
{
    return std::clamp (0.039016 * std::exp2 (octaves - 64.0 / 12.0), 1.0e-4, 1.0);
}

// [measured, black box] Damping q of each filter section at RESONANCE 0, 32,
// 64, 96, 112 and 127.
inline double resonanceQ (double resonance) noexcept
{
    constexpr double at[] { 0.0, 32.0, 64.0, 96.0, 112.0, 127.0 };
    constexpr double q[] { 0.993, 0.639, 0.287, 0.0748, 0.0209, 0.004 };
    const double r = std::clamp (resonance, 0.0, 1.0) * 127.0;
    int i = 0;
    while (i < 4 && r > at[i + 1])
        ++i;
    return q[i] + (q[i + 1] - q[i]) * (r - at[i]) / (at[i + 1] - at[i]);
}

// [settled] OM p.61: +32 is 100 %, -64 is -200 %.
inline double keyFollow (int value) noexcept { return value / 32.0; }

// [inferred] Modulation spans.
inline constexpr double filterEnvOctaves = 10.0;
inline constexpr double filterLfoOctaves = 4.0;
inline constexpr double pitchEnvSemitones = 24.0;
inline double pitchLfoSemitones (int value) noexcept
{
    const double d = bipolar (value);
    return 12.0 * d * std::abs (d);
}

// [inferred] Segment times. The shortest attack clicks, as reviews of the
// hardware report.
inline double attackSeconds (int value) noexcept { return 0.0005 * std::pow (12000.0, unit (value)); }
inline double decaySeconds (int value) noexcept { return 0.004 * std::pow (3000.0, unit (value)); }
inline double lfoHz (int value) noexcept { return 0.05 * std::pow (500.0, unit (value)); }
inline double fadeSeconds (int value) noexcept { return 8.0 * unit (value) * unit (value); }
inline double portamentoSeconds (int value) noexcept { return 0.004 * std::pow (750.0, unit (value)); }

// [settled] OM p.65: the delay time of each type with TIME fully right.
// PANNING SHORT is printed as 156 ms and as a quarter of 625 ms.
inline double delayMaximum (DelayType type) noexcept
{
    switch (type)
    {
        case DelayType::MonoLong: return 1.250;
        case DelayType::PanLR:
        case DelayType::PanRL: return 0.625;
        case DelayType::PanShort:
        case DelayType::MonoShort: break;
    }
    return 0.625 / 4.0;
}

// [inferred] Square-law TIME taper.
inline double delaySeconds (DelayType type, int value) noexcept
{
    const double v = (std::clamp (value, 0, 127) + 1) / 128.0;
    return std::max (0.001, delayMaximum (type) * v * v);
}

// [settled] OM p.57: +/-24 semitones with +/-50 cents, or four octaves of
// FINE at either WIDE end.
inline double osc2Semitones (const Osc2Params& p) noexcept
{
    const double fine = std::clamp (p.fine, -50, 50);
    if (p.range <= -25)
        return (fine - 50.0) / 100.0 * 48.0;
    if (p.range >= 25)
        return (fine + 50.0) / 100.0 * 48.0;
    return p.range + fine / 100.0;
}

// [settled] OM p.58: both oscillators at full volume in the centre.
inline double balanceGain (int value, bool first) noexcept
{
    const double b = bipolar (value);
    return std::min (1.0, first ? 1.0 - b : 1.0 + b);
}
} // namespace hypra::mapping
