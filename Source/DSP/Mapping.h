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
// [measured, black box] DETUNE at every value from 34 on JE-8086 (the
// spread of the seven partials at the third harmonic of C5): it steps in
// pairs from 34 to 61 and climbs steeply from 120 to 1.0 at 127, where
// Szabo's points interpolate linearly (up to 47 % off at 124). Below 34 the
// partials are too close to resolve, so Szabo's points stay.
inline constexpr std::array<double, 128> detuneEveryValue {
    0.0027, 0.0036961, 0.0046922, 0.0056883, 0.0066844, 0.0076805, 0.0086766, 0.0096727,
    0.011218, 0.012764, 0.014309, 0.015854, 0.0174, 0.018945, 0.020491, 0.022036,
    0.023527, 0.025018, 0.026509, 0.028, 0.029491, 0.030982, 0.032473, 0.033964,
    0.035564, 0.037164, 0.038764, 0.040364, 0.041964, 0.043564, 0.045164, 0.046764,
    0.048309, 0.049855, 0.053124, 0.053172, 0.055987, 0.056035, 0.059006, 0.059006,
    0.062245, 0.062245, 0.065108, 0.065157, 0.06828, 0.06828, 0.07136, 0.07136,
    0.074778, 0.074778, 0.077617, 0.077666, 0.080635, 0.080635, 0.083604, 0.083604,
    0.086871, 0.086871, 0.090398, 0.090447, 0.093682, 0.093682, 0.09665, 0.09665,
    0.09994, 0.10291, 0.10593, 0.10879, 0.11229, 0.11526, 0.11828, 0.1213,
    0.12454, 0.12756, 0.13064, 0.13408, 0.13705, 0.1406, 0.14342, 0.14659,
    0.1494, 0.15566, 0.16196, 0.16822, 0.17421, 0.18127, 0.18725, 0.19336,
    0.19945, 0.20571, 0.2117, 0.21796, 0.22448, 0.23073, 0.23699, 0.24282,
    0.24934, 0.2556, 0.26159, 0.26816, 0.27468, 0.28067, 0.28677, 0.29295,
    0.29913, 0.30511, 0.31191, 0.31816, 0.32442, 0.3304, 0.33693, 0.34276,
    0.34902, 0.35586, 0.36184, 0.3681, 0.37409, 0.38045, 0.38644, 0.39254,
    0.39922, 0.42409, 0.44943, 0.47387, 0.52376, 0.5991, 0.6985, 1
};
inline double superDetune (double knob) noexcept
{
    const double at = std::clamp (knob, 0.0, 1.0) * 127.0;
    const std::size_t i = std::min<std::size_t> (static_cast<std::size_t> (at), 126);
    return detuneEveryValue[i] + (detuneEveryValue[i + 1] - detuneEveryValue[i]) * (at - static_cast<double> (i));
}

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

// [measured, black box] On JE-8086 the feedback howl sits exactly on a
// harmonic of the note (within 0.1 %), so the loop is one period long. A
// factory-performance recording (P14) had suggested 2.5 samples short; that
// file is wet and the emulator wins.
inline constexpr double feedbackLoopShortfall = 0.0;

// [measured, black box] The harmonic the howl picks for HARMONICS (OSC 1
// CTRL 1) at 0, 4 ... 124, AMOUNT 64, C3; it is the same on every key from
// F#2 to C6 at 64. It jumps about rather than climbing; values between the
// points take the nearest one.
inline constexpr std::array<int, 32> feedbackHarmonicTable {
    2, 2, 2, 2, 16, 7, 32, 41, 8, 14, 35, 3, 3, 27, 25, 19,
    4, 13, 23, 10, 16, 6, 13, 7, 8, 28, 11, 13, 17, 23, 2, 2
};
inline int feedbackHarmonic (double harmonics) noexcept
{
    const int i = static_cast<int> (std::lround (std::clamp (harmonics, 0.0, 1.0) * 127.0 / 4.0));
    return feedbackHarmonicTable[static_cast<std::size_t> (std::min (i, 31))];
}

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

// [measured] Every envelope segment starts late: decay and release by
// 7.5 ms from 10 to 29 and by 25 ms from 30 up (fits every fourth value, and
// on a C6 for the short ones); attack by 14 ms from 30 up. Below that they
// start at once. The cause is not known; the level holds meanwhile.
inline double attackHoldSeconds (int value) noexcept { return value >= 30 ? 0.014 : 0.0; }

// [measured] Below 30 the attack is faster than the table: fitted on a C6
// (one cycle is 1 ms) every second value, geometric from 8 to 28, measured
// points below.
inline double attackTau (int value) noexcept
{
    const int v = std::clamp (value, 0, 127);
    if (v >= 30)
        return envelopeTau (v);
    if (v >= 8)
        return 0.0042113 * std::exp (0.08069 * v);
    constexpr std::array<double, 5> low { 0.0, 0.0015, 0.0022, 0.0029, 0.00803 };
    const std::size_t i = static_cast<std::size_t> (v / 2);
    return v % 2 == 0 ? low[i] : 0.5 * (low[i] + low[i + 1]);
}
inline double segmentHoldSeconds (int value) noexcept
{
    return value >= 30 ? 0.025 : value >= 10 ? 0.0075 : 0.0;
}

// [measured] LFO 1 FADE raises the LFO's depth along a straight line in
// decibels, from 37 dB down to full, in the envelope table's 60 dB time
// divided by 4.45 (64, 80, 96, 112 within 2 %; 127 runs 12 % long).
inline double fadeSeconds (int value) noexcept
{
    return value <= 0 ? 0.0 : envelopeSeconds60 (value) / 4.45;
}
inline double fadeLevel (double age, double seconds) noexcept
{
    if (seconds <= 0.0 || age >= seconds)
        return 1.0;
    return std::pow (10.0, -37.0 / 20.0 * (1.0 - std::max (age, 0.0) / seconds));
}

// [measured] LFO 2 RATE: geometric, 0.7815 Hz at 0 and an octave every 25.6
// steps (every eighth step 0 to 120 within 0.6 %), with the same jump to
// 50 Hz at 127 as LFO 1.
inline double lfo2Hz (int value) noexcept
{
    const int v = std::clamp (value, 0, 127);
    return v == 127 ? 50.0 : 0.7815 * std::exp2 (v / 25.61);
}

// [measured] Amp LFO depth (LFO 1 and LFO 2): the gain swings by this
// fraction each way, on the pitch-depth curve (29 % at 32, 95 % at 63).
inline double ampLfoDepth (int value) noexcept
{
    return 0.953 * pitchEnvSemitones (value) / pitchEnvTable[16];
}

// [measured] X-MOD: OSC 2's waveform (unscaled, ±1) moves OSC 1 by this
// many Hz, the same at C3, C4 and C5 and with OSC 2 a fifth or an octave up
// (fits of the spectrum at 0, 16 ... 127, 69 Hz a step).
inline double crossModHz (double depth) noexcept { return 8776.0 * depth; }

// [measured] The noise oscillator's filter is the main filter's -24 dB
// shape (two identical sections, the same resonance table) with its
// coefficient on the played note at CUTOFF 64, a semitone a step.
inline double noiseCoefficient (double hz, double cutoff) noexcept
{
    return std::min (1.0, kTwoPi * hz * std::exp2 ((cutoff * 127.0 - 64.0) / 12.0) / kEngineRate);
}

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

// [measured] OSC BALANCE is a constant-power crossfade: -3 dB each in the
// centre (OM p.58 suggests both at full; the capture does not).
inline double balanceGain (int value, bool first) noexcept
{
    const double angle = kPi / 4.0 * (1.0 + bipolar (value));
    return first ? std::cos (angle) : std::sin (angle);
}
} // namespace hypra::mapping
