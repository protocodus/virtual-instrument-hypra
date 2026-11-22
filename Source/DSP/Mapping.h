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

// [measured] Szabo's 11th-order fit of the DETUNE law.
inline double superDetune (double knob) noexcept
{
    const double x = std::clamp (knob, 0.0, 1.0);
    // [measured] The fit's 0.00301 intercept gives the wrong steps at knob 0:
    // a recorded JP-8080 allows 0.00267 to 0.00272 there.
    if (x == 0.0)
        return 0.0027;
    return ((((((((((10028.7312891634 * x - 50818.8652045924) * x
                    + 111363.4808729368) * x - 138150.6761080548) * x
                  + 106649.6679158292) * x - 53046.9642751875) * x
                + 17019.9518580080) * x - 3425.0836591318) * x
              + 404.2703938388) * x - 24.1878824391) * x
            + 0.6717417634) * x + 0.0030115596;
}

// [measured] Szabo's MIX laws.
inline double superCentre (double mix) noexcept { return -0.55366 * mix + 0.99785; }
inline double superSide (double mix) noexcept { return (-0.73764 * mix + 1.2841) * mix + 0.044372; }

// [measured] JP-8080 recording, Docs/research/hardware-ab-2026-11-22.md.
inline constexpr double superHighPassRatio = 0.904;
inline constexpr double superHighPassQ = 0.878;

// [inferred] Ten octaves from 20 Hz.
inline double cutoffOctaves (int value) noexcept { return 10.0 * unit (value); }
inline double cutoffHz (double octaves) noexcept
{
    return std::clamp (20.0 * std::exp2 (octaves), 10.0, 0.45 * kEngineRate);
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
