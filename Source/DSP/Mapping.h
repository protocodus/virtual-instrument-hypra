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

// [measured] Key follow on JE-8086 (cutoff tracked between notes 36 and 84,
// pivot at 60): +32 is 100 % and +63 200 %, but -64 is only -100 %. OM p.61
// prints -64 as -200 %; the capture wins.
inline double keyFollow (int value) noexcept
{
    const int v = std::clamp (value, -64, 63);
    return v < 0 ? v / 64.0 : v / 32.0;
}

namespace detail
{
// Points at 0, 4 ... 60 and 63 of a depth knob, taken symmetrically for
// negative values (-64 reads as 63).
inline double depthTable (const std::array<double, 17>& points, int value) noexcept
{
    const int v = std::clamp (value, -64, 63);
    const double a = std::min (std::abs (v), 63);
    const std::size_t i = std::min<std::size_t> (static_cast<std::size_t> (a / 4.0), 15);
    const double lo = 4.0 * static_cast<double> (i), hi = i == 15 ? 63.0 : lo + 4.0;
    const double y = points[i] + (points[i + 1] - points[i]) * (a - lo) / (hi - lo);
    return v < 0 ? -y : y;
}
} // namespace detail

// [measured] Depths on JE-8086. Pitch env: semitones at the envelope's
// peak, extrapolated back to note-on from the decay. Pitch LFO 1 swings half
// as far each way (measured 2-4 % short of half, within the tracker's
// smoothing of the triangle's corners). Filter env: cutoff shift in CUTOFF
// steps (semitones), read off a resonant noise filter; +63 reaches the
// top of the range from 0. Filter LFO 1 is half of it (±2 steps, the meter
// is noisy there).
inline constexpr std::array<double, 17> pitchEnvTable {
    0.0, 0.231, 0.955, 2.142, 3.781, 5.864, 8.384, 11.282, 14.57,
    18.178, 22.098, 26.256, 30.662, 35.21, 39.923, 44.682, 48.324
};
inline constexpr std::array<double, 17> filterEnvTable {
    0.0, 0.37, 1.48, 4.65, 8.72, 14.19, 20.51, 28.05, 36.13,
    45.18, 54.78, 65.23, 75.98, 87.51, 99.11, 111.18, 120.19
};
inline double pitchEnvSemitones (int value) noexcept { return detail::depthTable (pitchEnvTable, value); }
inline double pitchLfoSemitones (int value) noexcept { return 0.5 * pitchEnvSemitones (value); }
inline double filterEnvSemitones (int value) noexcept { return detail::depthTable (filterEnvTable, value); }
inline double filterLfoSemitones (int value) noexcept { return 0.5 * filterEnvSemitones (value); }

// [measured] Seconds to fall 60 dB for DECAY and RELEASE on JE-8086, every
// value: a fit every fourth step from 12 to 112 (geometric in between; the
// held-out odd steps land within 1 % median), each value at both ends. The
// top climbs in groups of four, with a jump at 117. Below 10 the release
// drops 20-30 dB in a few ms and then trails at about -6 dB per 20 ms; only
// the fast part is kept. The filter and pitch envelopes use the same table
// (checked at 16, 48, 80, 112 and at 0-127 for pitch decay).
inline constexpr std::array<double, 128> envelopeTable {
    0.001, 0.002, 0.004, 0.006, 0.009, 0.01, 0.015, 0.018,
    0.02, 0.026, 0.0885, 0.0965, 0.1018, 0.1094, 0.1175, 0.1261,
    0.1355, 0.1454, 0.1562, 0.1677, 0.1801, 0.1924, 0.2056, 0.2197,
    0.2347, 0.25, 0.2663, 0.2837, 0.3022, 0.3237, 0.3467, 0.3713,
    0.3977, 0.4209, 0.4454, 0.4713, 0.4987, 0.5287, 0.5605, 0.5942,
    0.6299, 0.668, 0.7083, 0.7511, 0.7965, 0.8449, 0.8963, 0.9508,
    1.009, 1.069, 1.134, 1.202, 1.275, 1.35, 1.431, 1.516,
    1.606, 1.682, 1.761, 1.845, 1.932, 1.99, 2.05, 2.112,
    2.176, 2.252, 2.33, 2.41, 2.494, 2.593, 2.696, 2.803,
    2.915, 3.053, 3.198, 3.35, 3.509, 3.661, 3.819, 3.984,
    4.155, 4.292, 4.432, 4.578, 4.728, 4.901, 5.08, 5.266,
    5.459, 5.696, 5.943, 6.201, 6.47, 6.814, 7.176, 7.557,
    7.959, 8.501, 9.079, 9.698, 10.36, 10.77, 11.19, 11.63,
    12.09, 12.65, 13.23, 13.84, 14.48, 15.33, 16.23, 17.19,
    18.2, 19.07, 20.31, 20.88, 21.52, 54.98, 53.45, 56.74,
    57.58, 68.45, 69.12, 71.73, 73.01, 92.99, 91.41, 100.9
};
inline double envelopeSeconds60 (int value) noexcept
{
    return envelopeTable[static_cast<std::size_t> (std::clamp (value, 0, 127))];
}
// One time constant of the table, used by every stage.
inline double envelopeTau (int value) noexcept { return envelopeSeconds60 (value) / 6.907755279; }

// [measured] LFO 1 RATE on JE-8086 at 0, 4 ... 124 and 127, from the slope
// of a pitch-modulating triangle (to 52) and from its cycle count (from 56).
// 127 jumps to 50 Hz. Geometric between points.
inline constexpr std::array<double, 33> lfoTable {
    0.0499, 0.06252, 0.07701, 0.1006, 0.1375, 0.1757, 0.2368, 0.3002, 0.3878, 0.5049, 0.6534,
    0.8505, 1.103, 1.435, 1.865, 2.406, 3.225, 3.674, 4.207, 4.794, 5.426, 6.163,
    7.031, 7.992, 9.091, 11.12, 11.77, 14.0, 16.15, 18.29, 20.73, 22.72, 50.04
};
inline double lfoHz (int value) noexcept
{
    const double a = std::clamp (value, 0, 127);
    const std::size_t i = std::min<std::size_t> (static_cast<std::size_t> (a / 4.0), 31);
    const double lo = 4.0 * static_cast<double> (i), hi = i == 31 ? 127.0 : lo + 4.0;
    return lfoTable[i] * std::pow (lfoTable[i + 1] / lfoTable[i], (a - lo) / (hi - lo));
}

// [inferred] LFO 1 FADE, not captured yet.
inline double fadeSeconds (int value) noexcept { return 8.0 * unit (value) * unit (value); }

// [measured] PORTAMENTO TIME on JE-8086: a straight line in pitch that takes
// the same time for 5, 12 and 24 semitones, up or down. Seconds at 0, 16
// ... 112 and 127, geometric between (the in-between steps are not
// captured).
inline constexpr std::array<double, 9> portamentoTable { 0.0, 0.027, 0.056, 0.150, 0.316, 0.574, 1.084, 2.425, 8.95 };
inline double portamentoSeconds (int value) noexcept
{
    const double a = std::clamp (value, 0, 127);
    const std::size_t i = std::min<std::size_t> (static_cast<std::size_t> (a / 16.0), 7);
    const double lo = 16.0 * static_cast<double> (i), hi = i == 7 ? 127.0 : lo + 16.0, u = (a - lo) / (hi - lo);
    if (i == 0)
        return portamentoTable[1] * u;
    return portamentoTable[i] * std::pow (portamentoTable[i + 1] / portamentoTable[i], u);
}

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
