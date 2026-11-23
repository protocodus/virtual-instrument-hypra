// OM p.65 connection: TONE CONTROL -> DELAY -> CHORUS (fed from the sum,
// added to the delay's stereo) -> PAN. The twelve chorus and five delay
// type names are Roland's; every modulation setting is [inferred].
#pragma once

#include "Mapping.h"

#include <array>
#include <vector>

namespace hypra
{
class ToneControl
{
public:
    void reset() noexcept { lowState_ = highState_ = 0.0; }

    // [measured, black box] JE-8086 TONE CONTROL: first-order shelves.
    // TREBLE turns above a fixed 940 Hz (+9.8 dB at 32, +14.6 at 63); BASS
    // below a fixed 435 Hz (+6.5 dB at 32, +28.7 at 63, which still rises
    // at 31 Hz). Cuts mirror the boosts. Gains in dB are straight between
    // 0, 32 and 63.
    void configure (int bass, int treble) noexcept
    {
        low_ = shelf (gainDb (bass, 6.52, 28.67), true);
        high_ = shelf (gainDb (treble, 9.82, 14.61), false);
    }

    double process (double x) noexcept
    {
        return section (high_, highState_, section (low_, lowState_, x));
    }

private:
    struct Coefficients
    {
        double b0 { 1.0 }, b1 { 0.0 }, a1 { 0.0 };
    };

    static double gainDb (int value, double at32, double at63) noexcept
    {
        const double v = std::abs (std::clamp (value, -64, 63) * 1.0);
        const double g = v <= 32.0 ? at32 * v / 32.0 : at32 + (at63 - at32) * std::min (1.0, (v - 32.0) / 31.0);
        return value < 0 ? -g : g;
    }

    // Bilinear first-order shelf. Low: (s + G w) / (s + w) with the upper
    // turn at 435 Hz; high: (G s + w) / (s + w) with the lower turn at 940 Hz.
    // A cut is the boost's inverse.
    static Coefficients shelf (double gainDb, bool low) noexcept
    {
        const double g = std::pow (10.0, std::abs (gainDb) / 20.0);
        const double turn = low ? 435.0 / g : 940.0 * g;
        const double k = std::tan (kPi * std::min (turn, 0.45 * kEngineRate) / kEngineRate);
        // analog (n1 s + n0) / (d1 s + d0) with s normalised to w
        double n1 = 1.0, n0 = low ? g : 1.0, d1 = 1.0, d0 = 1.0;
        if (! low)
            n1 = g;
        if (gainDb < 0.0)
        {
            std::swap (n1, d1);
            std::swap (n0, d0);
        }
        const double a0 = d1 + d0 * k;
        Coefficients c;
        c.b0 = (n1 + n0 * k) / a0;
        c.b1 = (n0 * k - n1) / a0;
        c.a1 = (d0 * k - d1) / a0;
        return c;
    }

    static double section (const Coefficients& c, double& state, double x) noexcept
    {
        // transposed direct form II
        const double y = c.b0 * x + state;
        state = c.b1 * x - c.a1 * y;
        return y;
    }

    Coefficients low_, high_;
    double lowState_ { 0.0 }, highState_ { 0.0 };
};

class Delay
{
public:
    void prepare()
    {
        left_.assign (size, 0.0f);
        right_.assign (size, 0.0f);
        write_ = 0;
    }

    void reset() noexcept
    {
        std::fill (left_.begin(), left_.end(), 0.0f);
        std::fill (right_.begin(), right_.end(), 0.0f);
        write_ = 0;
        snap();
    }

    void configure (DelayType type, int time, int feedback, int level) noexcept
    {
        type_ = type;
        target_ = std::min (mapping::delaySeconds (type, time) * kEngineRate, size - 3.0);
        feedback_ = mapping::delayFeedback (feedback);
        level_ = mapping::delayLevel (type) * unit (level);
    }

    void snap() noexcept { delay_ = target_; }

    // The wet signal only.
    Stereo process (double x) noexcept
    {
        delay_ += 0.0001 * (target_ - delay_); // moving TIME bends the echoes
        if (std::abs (target_ - delay_) < 1.0e-4)
            delay_ = target_;
        const double position = write_ - delay_ + size;
        const int index = static_cast<int> (position);
        const double fraction = position - index;
        const auto read = [&] (const std::vector<float>& b)
        {
            const double a = b[static_cast<std::size_t> (index & mask)];
            return a + fraction * (b[static_cast<std::size_t> ((index + 1) & mask)] - a);
        };
        const double l = read (left_), r = read (right_);
        const bool mono = type_ == DelayType::MonoShort || type_ == DelayType::MonoLong;
        // Panning: the line alternates, so the first echo is on the first side.
        left_[static_cast<std::size_t> (write_)] = store (x + feedback_ * (mono ? l : r));
        right_[static_cast<std::size_t> (write_)] = store (mono ? 0.0 : feedback_ * l);
        write_ = (write_ + 1) & mask;
        if (mono)
            return { level_ * l, level_ * l };
        // [measured] PANNING SHORT answers on the right first, like R->L.
        return type_ == DelayType::PanRL || type_ == DelayType::PanShort ? Stereo { level_ * r, level_ * l }
                                        : Stereo { level_ * l, level_ * r };
    }

private:
    static float store (double v) noexcept
    {
        return std::abs (v) < 1.0e-12 ? 0.0f : static_cast<float> (std::clamp (v, -4.0, 4.0));
    }

    static constexpr int size = 1 << 17, mask = size - 1;
    std::vector<float> left_, right_;
    int write_ { 0 };
    DelayType type_ { DelayType::PanLR };
    double delay_ { 100.0 }, target_ { 100.0 }, feedback_ { 0.0 }, level_ { 0.0 };
};

class Chorus
{
public:
    void reset() noexcept
    {
        for (auto& line : line_)
            line.fill (0.0f);
        for (auto& chain : allPass_)
            chain.fill (0.0);
        for (auto& side : taps_)
            side.fill ({});
        fed_ = {};
        highPass_ = {};
        write_ = 0;
        phase_ = 0.0;
        prime_ = true;
    }

    void configure (ChorusType type, int level) noexcept
    {
        if (type != type_)
        {
            type_ = type;
            prime_ = true;
        }
        level_ = unit (level);
    }

    void control() noexcept
    {
        const Preset& p = presets[static_cast<std::size_t> (type_)];
        phase_ += p.rate / kControlRate;
        phase_ -= std::floor (phase_);
        for (int side = 0; side < 2; ++side)
        {
            const auto s = static_cast<std::size_t> (side);
            if (p.stages > 0)
            {
                const double swing = 0.5 + 0.5 * std::sin (kTwoPi * (phase_ + side * p.spread));
                const double t = std::tan (kPi * 120.0 * std::exp2 (p.depth * swing) / kEngineRate);
                coefficient_[s] = (t - 1.0) / (t + 1.0);
                continue;
            }
            for (int tap = 0; tap < p.taps; ++tap)
            {
                const double angle = phase_ + static_cast<double> (tap) / p.taps + side * p.spread;
                const double ms = p.base + p.depth * std::sin (kTwoPi * angle);
                Tap& t = taps_[s][static_cast<std::size_t> (tap)];
                const double target = std::max (1.0, ms * 0.001 * kEngineRate);
                if (prime_)
                    t.delay = target;
                t.step = (target - t.delay) / kControlInterval;
            }
        }
        prime_ = false;
    }

    Stereo process (double x) noexcept
    {
        const Preset& p = presets[static_cast<std::size_t> (type_)];
        Stereo out;
        for (int side = 0; side < 2; ++side)
        {
            const auto s = static_cast<std::size_t> (side);
            double& fed = side ? fed_.r : fed_.l;
            const double input = x + p.feedback * fed;
            double wet = 0.0;
            if (p.stages > 0)
            {
                const double c = coefficient_[s];
                double v = input;
                for (int i = 0; i < p.stages; ++i)
                {
                    double& z = allPass_[s][static_cast<std::size_t> (i)];
                    const double y = c * v + z;
                    z = v - c * y;
                    v = y;
                }
                wet = v;
            }
            else
            {
                for (int tap = 0; tap < p.taps; ++tap)
                {
                    Tap& t = taps_[s][static_cast<std::size_t> (tap)];
                    t.delay += t.step;
                    const double position = write_ - t.delay + size;
                    const int index = static_cast<int> (position);
                    const double a = line_[s][static_cast<std::size_t> (index & mask)];
                    const double b = line_[s][static_cast<std::size_t> ((index + 1) & mask)];
                    wet += a + (position - index) * (b - a);
                }
                wet *= p.taps == 3 ? 0.5 : 1.0;
                line_[s][static_cast<std::size_t> (write_)] = static_cast<float> (softClip (input / 3.0) * 3.0);
            }
            fed = softClip (wet);
            if (p.highPass)
            {
                double& z = side ? highPass_.r : highPass_.l;
                z += highPole_ * (wet - z);
                wet -= z;
            }
            (side ? out.r : out.l) = level_ * wet;
        }
        write_ = (write_ + 1) & mask;
        return out;
    }

private:
    struct Preset
    {
        double base, depth; // ms, or for a phaser the sweep in octaves
        double rate, feedback, spread;
        int taps, stages;
        bool highPass;
    };

    static constexpr std::array<Preset, 12> presets { {
        { 13.0, 3.2, 0.28, 0.0, 0.17, 3, 0, false },  // SUPER CHORUS SLW
        { 11.0, 2.0, 0.65, 0.0, 0.17, 3, 0, false },  // SUPER CHORUS MID
        { 8.0, 0.6, 2.60, 0.0, 0.17, 3, 0, false },   // SUPER CHORUS FST
        { 10.0, 1.8, 0.45, 0.0, 0.17, 3, 0, true },   // SUPER CHORUS CLR
        { 2.6, 2.1, 0.12, 0.70, 0.25, 1, 0, false },  // FLANGER SLOW
        { 4.5, 4.2, 0.22, 0.80, 0.25, 1, 0, false },  // FLANGER DEEP
        { 2.0, 1.5, 1.60, 0.65, 0.25, 1, 0, false },  // FLANGER FAST
        { 0.0, 5.5, 0.15, 0.55, 0.25, 1, 6, false },  // DEEP PHASING SLW
        { 0.0, 6.5, 0.09, 0.82, 0.10, 1, 12, false }, // JET PHASING
        { 3.0, 2.6, 0.90, -0.75, 0.50, 1, 0, false }, // TWISTING
        { 12.1, 0.0, 0.00, 0.35, 0.00, 1, 0, false }, // FREEZE PHASE 1 [measured comb, 11.6-12.6 ms, weak]
        { 0.92, 0.0, 0.00, 0.10, 0.00, 1, 0, false }, // FREEZE PHASE 2 [measured comb, 0.92 ms, faint]
    } };

    struct Tap { double delay { 441.0 }, step { 0.0 }; };

    static constexpr int size = 4096, mask = size - 1;
    std::array<std::array<float, size>, 2> line_ {};
    std::array<std::array<double, 12>, 2> allPass_ {};
    std::array<std::array<Tap, 3>, 2> taps_ {};
    std::array<double, 2> coefficient_ {};
    Stereo fed_, highPass_;
    double highPole_ { onePole (700.0) };
    ChorusType type_ { ChorusType::SuperSlow };
    double level_ { 0.0 }, phase_ { 0.0 };
    int write_ { 0 };
    bool prime_ { true };
};
} // namespace hypra
