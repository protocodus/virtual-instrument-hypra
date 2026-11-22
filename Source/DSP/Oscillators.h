// The seven OSC 1 waveforms and three OSC 2 waveforms. All run naively at
// 88.2 kHz: the aliasing is part of the instrument, so there is no BLEP.
// Shapes follow the drawings and wording in OM pp.55-56; the formulas that
// make those shapes are this project's own.
#pragma once

#include "Filter.h"
#include "Mapping.h"

#include <array>

namespace hypra
{
namespace osc
{
inline constexpr double phaseScale = 1.0 / 4294967296.0;

// [measured] 24-bit phase at 88.2 kHz, here in the top bits: the saws of a
// recorded JP-8080 drift apart in exact multiples of 88200 / 2^24 Hz.
inline std::uint32_t increment (double hz) noexcept
{
    const double cycles = std::clamp (hz / kEngineRate, -0.499, 0.499);
    const auto steps = static_cast<std::int32_t> (cycles * 16777216.0);
    return static_cast<std::uint32_t> (steps) << 8;
}

inline double unitPhase (std::uint32_t phase) noexcept { return phase * phaseScale; }

class SineTable
{
public:
    SineTable() noexcept
    {
        for (std::size_t i = 0; i < values_.size(); ++i)
            values_[i] = std::sin (kTwoPi * static_cast<double> (i) / size);
    }

    double operator() (double cycles) const noexcept
    {
        const double position = (cycles - std::floor (cycles)) * size;
        const auto index = std::min (static_cast<std::size_t> (position), static_cast<std::size_t> (size - 1));
        return values_[index] + (position - static_cast<double> (index)) * (values_[index + 1] - values_[index]);
    }

private:
    static constexpr int size = 2048;
    std::array<double, size + 1> values_ {};
};

inline const SineTable sine;

inline double triangle (double p) noexcept { return 1.0 - 4.0 * std::abs (p - 0.5); }

// Mean removed, as a pulse wave at the jack has none.
inline double square (double p, double width) noexcept
{
    const double duty = 0.5 + 0.47 * std::clamp (width, 0.0, 1.0);
    return (p < duty ? 1.0 : -1.0) - (2.0 * duty - 1.0);
}

// OM p.56: round with a strong fundamental at both ends, a plain saw a
// quarter of the way up, thin ("as though an HPF were applied") in the centre.
inline double shapedSaw (double p, double shape) noexcept
{
    const double saw = 2.0 * p - 1.0, fundamental = -sine (p);
    if (shape < 0.25)
    {
        const double a = 4.0 * std::max (0.0, shape);
        return (1.0 - a) * (0.75 * fundamental + 0.25 * saw) + a * saw;
    }
    if (shape < 0.5)
        return saw - 4.0 * (shape - 0.25) * 0.586 * fundamental;
    const double a = 2.0 * (std::min (1.0, shape) - 0.5);
    return (1.0 - a) * (saw - 0.586 * fundamental) + a * 0.85 * fundamental;
}

// OM p.56: triangle, cusped in the centre, folded double humps at the top.
inline double shapedTriangle (double p, double shape) noexcept
{
    const double t = triangle (p), cusp = t * t * t;
    if (shape < 0.5)
        return t + 2.0 * std::max (0.0, shape) * (cusp - t);
    const double a = 2.0 * (std::min (1.0, shape) - 0.5);
    return (1.0 - a) * cusp + a * 0.9 * sine (0.43 * t);
}

// OM p.55: a triangle wrapped at the rails. OFFSET moves the wrap points,
// which makes alternate segments unequal and cancels most of the fundamental.
inline double triangleMod (double p, double offset) noexcept
{
    double y = 2.0 * triangle (p) + 0.95 * std::clamp (offset, 0.0, 1.0);
    if (y > 1.0)
        y -= 2.0;
    else if (y < -1.0)
        y += 2.0;
    return y;
}
} // namespace osc

class SuperSaw
{
public:
    // Szabo 2010: the seven phases are unrelated at every note-on.
    void trigger (Random& random) noexcept
    {
        for (auto& phase : phase_)
            phase = random.next();
        highPass_.reset();
        highPass2_.reset();
    }

    void configure (double hz, double detune, double mix) noexcept
    {
        if (detune != detune_ || mix != mix_)
        {
            detune_ = detune;
            mix_ = mix;
            amount_ = mapping::superDetune (detune);
            centre_ = mapping::superCentre (mix);
            side_ = mapping::superSide (mix);
        }
        // [measured at DETUNE 0] each side saw is the centre's integer step
        // plus a floored product, as an arithmetic shift leaves it.
        const double centre = std::floor (std::clamp (hz / kEngineRate, 0.0, 0.499) * 16777216.0);
        for (std::size_t i = 0; i < step_.size(); ++i)
        {
            const double steps = std::min (8388607.0, centre + std::floor (centre * mapping::superOffsets[i] * amount_));
            step_[i] = static_cast<std::uint32_t> (static_cast<std::int32_t> (steps)) << 8;
        }
        // [measured] two identical 12 dB high-passes, from amplitude and
        // phase of a recorded JP-8080. [inferred] that they track every key
        // at the same ratio: the recording is one note.
        highPass_.configure (hz * mapping::superHighPassRatio, 1.0 / mapping::superHighPassQ);
        highPass2_.configure (hz * mapping::superHighPassRatio, 1.0 / mapping::superHighPassQ);
    }

    double process() noexcept
    {
        double centre = 0.0, sides = 0.0;
        for (std::size_t i = 0; i < phase_.size(); ++i)
        {
            const std::uint32_t before = phase_[i];
            phase_[i] += step_[i];
            const double saw = 2.0 * osc::unitPhase (phase_[i]) - 1.0;
            if (i == 3)
            {
                centre = saw;
                wrapped_ = phase_[i] < before;
            }
            else
                sides += saw;
        }
        // [inferred] level, chosen so eight voices at full MIX stay under full scale.
        return 0.58 * highPass2_.process (highPass_.process (centre_ * centre + side_ * sides).high).high;
    }

    bool wrapped() const noexcept { return wrapped_; }

    // A saw's speed against the centre one, in steps of the 24-bit phase.
    int stepsFromCentre (int saw) const noexcept
    {
        return static_cast<std::int32_t> (step_[static_cast<std::size_t> (saw)] - step_[3]) / 256;
    }

private:
    std::array<std::uint32_t, 7> phase_ {}, step_ {};
    double detune_ { -1.0 }, mix_ { -1.0 };
    double amount_ { 0.0 }, centre_ { 1.0 }, side_ { 0.0 };
    Svf highPass_, highPass2_;
    bool wrapped_ { false };
};

class NoiseOsc
{
public:
    void reset() noexcept { filter_.reset(); }

    // OM pp.55-56: low-passed noise that follows the key, silent with CUTOFF
    // down and a sine with RESONANCE up. Both laws are [inferred].
    void configure (double hz, double cutoff, double resonance) noexcept
    {
        const double fc = std::clamp (hz * std::exp2 (-5.0 + 11.0 * cutoff), 5.0, 0.45 * kEngineRate);
        const double damping = 1.6 * (1.0 - resonance) * (1.0 - resonance) + 0.004;
        filter_.configure (fc, damping);
        const double ringing = std::min (8.0, 0.5 / std::sqrt (kPi * fc / (3.0 * kEngineRate * damping)));
        const double r2 = resonance * resonance;
        gain_ = (1.0 - r2 + r2 * ringing) * std::min (1.0, 20.0 * cutoff);
    }

    double process (Random& random) noexcept
    {
        return softClip (gain_ * filter_.process (random.bipolarValue()).low);
    }

private:
    Svf filter_;
    double gain_ { 1.0 };
};

// [inferred mechanism] OM p.56 says only that it resembles guitar feedback,
// that AMOUNT sets the level of the harmonics and HARMONICS their sound.
// Like a string in front of an amplifier: a saw drives a limited loop one
// period long, so it can only ring on harmonics of the note, and a band-pass
// in the loop picks which one. It blooms after each attack and howls above
// unity gain.
class FeedbackOsc
{
public:
    void reset() noexcept
    {
        buffer_.fill (0.0f);
        write_ = 0;
        band_.reset();
        bloom_ = 0.0;
        delay_ = 0.0;
    }

    void retrigger() noexcept { bloom_ = 0.0; }

    void configure (double hz, double harmonics, double amount) noexcept
    {
        target_ = std::clamp (kEngineRate / std::max (1.0, hz), 2.0, static_cast<double> (size - 3));
        if (delay_ == 0.0)
            delay_ = target_;
        band_.configure (std::clamp (hz * (1.0 + 7.0 * harmonics), 10.0, 18000.0), damping);
        bloom_ += onePole (0.8, kControlRate) * (1.0 - bloom_);
        amount_ = amount;
        loopGain_ = 1.8 * amount * (0.45 + 0.55 * bloom_);
    }

    double process (double saw) noexcept
    {
        delay_ += 0.01 * (target_ - delay_);
        const double position = static_cast<double> (write_) - delay_ + size;
        const int index = static_cast<int> (position);
        const double fraction = position - index;
        const double a = buffer_[static_cast<std::size_t> (index & mask)];
        const double b = buffer_[static_cast<std::size_t> ((index + 1) & mask)];
        const double fed = damping * band_.process (a + fraction * (b - a)).band;
        const double loop = softClip (0.15 * amount_ * saw + loopGain_ * fed);
        buffer_[static_cast<std::size_t> (write_)] = static_cast<float> (loop);
        write_ = (write_ + 1) & mask;
        return saw * (0.8 - 0.62 * amount_) + 0.95 * amount_ * loop;
    }

private:
    static constexpr int size = 8192, mask = size - 1;
    static constexpr double damping = 1.25; // broad, so the howl stays in tune
    std::array<float, size> buffer_ {};
    int write_ { 0 };
    Svf band_;
    double bloom_ { 0.0 }, amount_ { 0.0 }, loopGain_ { 0.0 };
    double delay_ { 0.0 }, target_ { 100.0 };
};

// OSC 1, OSC 2 and the three functions between them, wired as OM p.59:
// X-MOD runs from 2 to 1, SYNC from 1 to 2, and RING takes OSC 2's place
// in the balance.
class OscillatorPair
{
public:
    struct Controls
    {
        Wave1 wave1 { Wave1::SuperSaw };
        Wave2 wave2 { Wave2::Saw };
        double hz1 { 0.0 }, hz2 { 0.0 };
        double ctrl1 { 0.0 }, ctrl2 { 0.0 }, width2 { 0.0 }, xmod { 0.0 };
        double gain1 { 1.0 }, gain2 { 0.0 };
        bool sync { false }, ring { false };
    };

    void trigger (Random& random, Wave1 wave, bool fresh) noexcept
    {
        super_.trigger (random);
        if (fresh)
        {
            phase1_ = random.next();
            phase2_ = random.next();
            last2_ = 0.0;
            noise_.reset();
            feedbackReady_ = false;
        }
        if (wave == Wave1::FeedbackOsc)
            feedback_.retrigger();
    }

    void configure (const Controls& c) noexcept
    {
        c_ = c;
        step1_ = osc::increment (c.hz1);
        step2_ = osc::increment (c.hz2);
        if (c.wave1 == Wave1::SuperSaw)
            super_.configure (c.hz1, c.ctrl1, c.ctrl2);
        else if (c.wave1 == Wave1::Noise)
            noise_.configure (c.hz1, c.ctrl1, c.ctrl2);
        else if (c.wave1 == Wave1::FeedbackOsc)
        {
            if (! feedbackReady_)
                feedback_.reset();
            feedbackReady_ = true;
            feedback_.configure (c.hz1, c.ctrl1, c.ctrl2);
        }
        // [inferred] linear, through-zero FM with a square-law depth.
        fmDepth_ = 8.0 * c.xmod * c.xmod * c.hz1;
        // OM p.58: no cross modulation with SUPER SAW or NOISE.
        fm_ = c.xmod > 0.0 && c.wave1 != Wave1::SuperSaw && c.wave1 != Wave1::Noise;
    }

    double process (Random& random) noexcept
    {
        double first = 0.0;
        bool wrapped;
        if (c_.wave1 == Wave1::SuperSaw)
        {
            first = super_.process();
            wrapped = super_.wrapped();
        }
        else
        {
            const std::uint32_t before = phase1_;
            const std::uint32_t step = fm_ ? osc::increment (c_.hz1 + fmDepth_ * last2_) : step1_;
            phase1_ += step;
            wrapped = static_cast<std::int32_t> (step) >= 0 && phase1_ < before;
            const double p = osc::unitPhase (phase1_);
            switch (c_.wave1)
            {
                case Wave1::SuperSaw: break;
                case Wave1::TriangleMod: first = 0.7 * osc::triangleMod (p, c_.ctrl1); break;
                case Wave1::Noise: first = noise_.process (random); break;
                case Wave1::FeedbackOsc: first = feedback_.process (2.0 * p - 1.0); break;
                case Wave1::Square: first = 0.6 * osc::square (p, c_.ctrl1); break;
                case Wave1::Saw: first = 0.8 * osc::shapedSaw (p, c_.ctrl1); break;
                case Wave1::Triangle: first = 0.9 * osc::shapedTriangle (p, c_.ctrl1); break;
            }
        }

        // [inferred] SYNC resets on the sample, with no sub-sample correction.
        phase2_ = c_.sync && wrapped ? 0u : phase2_ + step2_;
        const double p2 = osc::unitPhase (phase2_);
        double second = 0.0;
        switch (c_.wave2)
        {
            case Wave2::Square: second = 0.6 * osc::square (p2, c_.width2); break;
            case Wave2::Saw: second = 0.8 * (2.0 * p2 - 1.0); break;
            case Wave2::Triangle: second = 0.9 * osc::triangle (p2); break;
        }
        last2_ = second;
        if (c_.ring)
            second *= 1.6 * first;
        return c_.gain1 * first + c_.gain2 * second;
    }

private:
    Controls c_;
    SuperSaw super_;
    NoiseOsc noise_;
    FeedbackOsc feedback_;
    std::uint32_t phase1_ { 0 }, phase2_ { 0 }, step1_ { 0 }, step2_ { 0 };
    double last2_ { 0.0 }, fmDepth_ { 0.0 };
    bool fm_ { false }, feedbackReady_ { false };
};
} // namespace hypra
