// OSC 1 (seven waveforms) and OSC 2 (three waveforms, sync, ring, X-Mod) at 88.2 kHz.
#pragma once

#include "Core.h"
#include "HypraPatch.h"
#include "Mapping.h"

#include <array>

namespace hypra
{
namespace osc
{
// 24-bit phase accumulator in top bits of a 32-bit unsigned word.
inline std::uint32_t increment24 (double hz) noexcept
{
    const double cycles = std::clamp (hz / kEngineRate, -0.499, 0.499);
    const auto steps = static_cast<std::int32_t> (cycles * 16777216.0);
    return static_cast<std::uint32_t> (steps) << 8;
}

inline double bipolarPhase (std::uint32_t phase) noexcept
{
    return phase * (1.0 / 2147483648.0) - 1.0;
}

inline double triangle (double p) noexcept
{
    return 1.0 - 4.0 * std::abs (p - 0.5);
}

// Polynomial edge window attenuating discontinuity aliasing near phase wraps.
inline double aaWindow (double dist, double inc, double mult) noexcept
{
    const double a = sat (-mult * std::abs (inc));
    const double b = sat (3.0 * a);
    const double c = sat (2.5 * b + 0.09375);
    const double slope = sat (1.0 + 0.3125 * a + 0.6875 * b);
    return sat (1.0 + c + dist * (16.0 * slope + 1.0));
}

inline double hfComp (double inc) noexcept
{
    return sat (0.125 + 2.0 * inc * inc) * 4.0;
}

// OSC 1 Saw: cubic shape warp with anti-aliased edge window and HF lift.
inline double shapedSawHardware (double p, double inc, double s) noexcept
{
    const double win = aaWindow (1.0 - std::abs (p), inc, 4.0);
    const double poly = sat ((1.5 - 4.0 * s) * p + (4.0 * s - 1.0) * p * p * p);
    return sat (sat (poly * win) * hfComp (inc)) * (23.0 / 32.0);
}

// Normalised saw waveform over unit phase [0, 1) for offline harmonic checks.
inline double shapedSaw (double unitP, double shape) noexcept
{
    const double p = 2.0 * unitP - 1.0;
    const double s = mapping::waveShape (shape);
    const double inc = 2.0 * 100.0 / kEngineRate;
    return shapedSawHardware (p, inc, s) / (23.0 / 32.0) * 2.0;
}

// OSC 1 Triangle: cubic phase skew followed by a Chebyshev T3 wavefolder.
inline double shapedTriangleHardware (double p, double s) noexcept
{
    const double u = 0.5 * p + 0.5;
    const double w = (1.0 - s) * u + s * u * u * u;
    const double t = 2.0 * std::abs (2.0 * w - 1.0) - 1.0;
    const double y = sat (t + 4.0 * s * (t * t * t - t));
    return y * (1.0 - 0.5 * s) * (39.0 / 128.0);
}

// Three-stage anti-aliased variable pulse shaper with DC removal.
inline double pulseHardware (double p, double inc, double pw) noexcept
{
    const double tri = 1.0 - 2.0 * std::abs (p);
    const double aInc = std::abs (inc);
    const double dc = sat (2.0 * aInc);
    const double dcOff = pw * sat (1.0 - dc * dc);
    const double raw = sat (tri + dcOff);

    const double a1 = sat (-4.0 * aInc);
    const double w1 = sat (a1 + 1.0);
    const double a2 = sat (4.0 * a1);
    const double w2 = sat (a2 + 1.0);
    const double a3 = sat (4.0 * a2);
    const double w3 = sat (a3 + 1.0);

    const double pulse = sat (raw + 4.0 * raw * w1 + 4.0 * sat (4.0 * raw) * w2
                              + (raw >= 0.0 ? 1.0 : -1.0) * w3);
    return (pulse - dcOff) * (19.0 / 128.0);
}

// OSC 1 Triangle Mod: wrapped high-gain triangle with anti-aliased fold window.
inline double triangleModHardware (double p, double inc, double off) noexcept
{
    const double tri = 1.0 - 2.0 * std::abs (p);
    const double wrapped = wrap2 (tri * (1.125 + off));
    const double win = aaWindow (1.0 - std::abs (wrapped), inc, 8.0);
    return wrapped * win * (20.0 / 128.0);
}

inline double osc2SawHardware (double p, double inc) noexcept
{
    const double win = aaWindow (1.0 - std::abs (p), inc, 4.0);
    return sat (sat (0.5 * p * win) * hfComp (inc));
}

inline double osc2TriangleHardware (double p) noexcept
{
    return 0.625 * (std::abs (p) - 0.5);
}

inline double syncSawHardware (double p1, double inc1, double inc2, double ratio) noexcept
{
    const double win1 = aaWindow (1.0 - std::abs (p1), inc1, 4.0);
    const double p2 = wrap2 (ratio * (p1 + 1.0) - 1.0);
    const double win2 = aaWindow (1.0 - std::abs (p2), inc2, 4.0);
    return sat (sat (0.5 * p2 * win2) * hfComp (inc2) * win1);
}

inline double syncTriangleHardware (double p1, double inc1, double ratio) noexcept
{
    const double win1 = aaWindow (1.0 - std::abs (p1), inc1, 4.0);
    const double p2a = wrap2 (ratio * (p1 + 1.0) - 1.0);
    const double p2b = wrap2 (-ratio * (1.0 - p1) - 1.0);
    const double tA = std::abs (p2a) - 0.5;
    const double tB = std::abs (p2b) - 0.5;
    return 0.625 * sat ((tA - tB) * win1 + tB);
}

inline double syncPulseHardware (double p1, double inc1, double inc2, double ratio, double pw) noexcept
{
    const double win1 = aaWindow (1.0 - std::abs (p1), inc1, 4.0);
    const double p2 = wrap2 (ratio * (p1 + 1.0) - 1.0);
    return pulseHardware (p2, inc2, pw) * win1;
}
} // namespace osc

class SuperSaw
{
public:
    void trigger (Random& random) noexcept
    {
        for (auto& phase : phase_)
            phase = random.next();
        low1_ = band1_ = low2_ = band2_ = 0.0;
        fresh_ = true;
    }

    void configure (double hz, double detune, double mix) noexcept
    {
        hz_ = std::max (hz, 1.0);
        detuneTarget_ = mapping::superDetuneReg (detune);
        mixTarget_ = mapping::superMixSide (mix);
        if (fresh_)
        {
            detuneCur_ = detuneTarget_;
            mixCur_ = mixTarget_;
            fresh_ = false;
        }
        updateSteps (detuneCur_);
    }

    double process() noexcept
    {
        constexpr double smoothAlpha = 1.0 / 512.0;
        detuneCur_ += smoothAlpha * (detuneTarget_ - detuneCur_);
        mixCur_ += smoothAlpha * (mixTarget_ - mixCur_);
        if (std::abs (detuneTarget_ - detuneCur_) > 1.0e-9)
            updateSteps (detuneCur_);

        double centre = 0.0, sides = 0.0;
        for (std::size_t i = 0; i < phase_.size(); ++i)
        {
            const std::uint32_t before = phase_[i];
            phase_[i] += step_[i];
            const double saw = osc::bipolarPhase (phase_[i]);
            if (i == 3)
            {
                centre = saw;
                wrapped_ = phase_[i] < before;
            }
            else
            {
                sides += saw;
            }
        }

        const double atten = 1.0 - sat (2.1875 * mixCur_);
        const double hpIn = sat (0.1953125 * centre + mixCur_ * sides) * 2.0 * atten;

        const double inc = std::clamp (2.0 * hz_ / kEngineRate, 1.0e-5, 0.99);
        const double f = std::min (0.99, 1.53125 * inc);

        low1_ = sat (low1_ + f * band1_);
        const double high1 = sat (0.5 * hpIn - sat (low1_ + band1_));
        band1_ = sat (band1_ + 2.0 * f * high1);

        low2_ = sat (low2_ + 2.0 * f * band2_);
        const double high2 = sat (0.3125 * high1 - sat (low2_ + band2_));
        band2_ = sat (band2_ + 2.0 * f * high2);

        return sat (1.8125 * high2);
    }

    bool wrapped() const noexcept { return wrapped_; }
    double centrePhase() const noexcept { return osc::bipolarPhase (phase_[3]); }

    int stepsFromCentre (int saw) const noexcept
    {
        return static_cast<std::int32_t> (step_[static_cast<std::size_t> (saw)] - step_[3]) / 256;
    }

private:
    void updateSteps (double detuneReg) noexcept
    {
        const double centre = std::floor (std::clamp (hz_ / kEngineRate, 0.0, 0.499) * 16777216.0);
        const double baseDetune = std::floor (centre * detuneReg);
        for (std::size_t i = 0; i < step_.size(); ++i)
        {
            const double steps = std::min (8388607.0,
                                           centre + std::floor (baseDetune * mapping::superDetuneMultipliers[i]));
            step_[i] = static_cast<std::uint32_t> (static_cast<std::int32_t> (steps)) << 8;
        }
    }

    std::array<std::uint32_t, 7> phase_ {}, step_ {};
    double hz_ { 440.0 };
    double detuneCur_ { 0.0 }, detuneTarget_ { 0.0 };
    double mixCur_ { 0.0 }, mixTarget_ { 0.0 };
    double low1_ { 0.0 }, band1_ { 0.0 }, low2_ { 0.0 }, band2_ { 0.0 };
    bool wrapped_ { false }, fresh_ { true };
};

class NoiseOsc
{
public:
    void reset() noexcept
    {
        low1_ = band1_ = low2_ = band2_ = 0.0;
        fresh_ = true;
    }

    void configure (double hz, double cutoff, double resonance) noexcept
    {
        fTarget_ = mapping::noiseCoefficient (hz, cutoff);
        qHalfTarget_ = mapping::noiseResonanceQHalf (resonance, fTarget_);
        if (fresh_)
        {
            f_ = fTarget_;
            qHalf_ = qHalfTarget_;
            fresh_ = false;
        }
    }

    double process (Random& random) noexcept
    {
        constexpr double smoothAlpha = 1.0 / 512.0;
        f_ += smoothAlpha * (fTarget_ - f_);
        qHalf_ += smoothAlpha * (qHalfTarget_ - qHalf_);
        const double q = 2.0 * qHalf_;

        const double n = random.bipolarValue();
        const double x1 = sat (n * (qHalf_ + 1.0 / 1024.0));
        low1_ = sat (low1_ + f_ * band1_);
        const double h1 = sat (x1 - sat (low1_ + q * band1_));
        band1_ = sat (band1_ + f_ * h1);

        const double x2 = sat (0.835 * low1_);
        low2_ = sat (low2_ + f_ * band2_);
        const double h2 = sat (x2 - sat (low2_ + q * band2_));
        band2_ = sat (band2_ + f_ * h2);

        return sat (low2_ * 2.0 * sat (sat (4.75 * qHalf_) + 0.1797)) * 0.46875;
    }

private:
    double f_ { 0.02 }, fTarget_ { 0.02 };
    double qHalf_ { 0.5 }, qHalfTarget_ { 0.5 };
    double low1_ { 0.0 }, band1_ { 0.0 }, low2_ { 0.0 }, band2_ { 0.0 };
    bool fresh_ { true };
};

// Two-tap fractional comb feedback oscillator without a separate resonant filter.
class FeedbackOsc
{
public:
    void reset() noexcept
    {
        buffer_.fill (0.0f);
        write_ = 0;
        dc_ = 0.0;
        fresh_ = true;
    }

    void retrigger() noexcept {}

    void configure (double hz, double harmonics, double amount) noexcept
    {
        d1Target_ = std::clamp (kEngineRate / std::max (hz, 15.0), 2.0, static_cast<double> (size - 3));
        hTarget_ = mapping::feedbackHarmonics (harmonics);
        amtTarget_ = mapping::feedbackAmount (amount);
        if (fresh_)
        {
            d1_ = d1Target_;
            h_ = hTarget_;
            amt_ = amtTarget_;
            fresh_ = false;
        }
    }

    double process (double rawSawPhase) noexcept
    {
        d1_ += (1.0 / 128.0) * (d1Target_ - d1_);
        h_ += (1.0 / 512.0) * (hTarget_ - h_);
        amt_ += (1.0 / 512.0) * (amtTarget_ - amt_);

        const double d2 = std::max (1.0, d1_ * (0.5 + h_));
        const double tap1 = 2.0 * readLerp (d1_);
        const double tap2 = 2.0 * readLerp (d2);

        const double loop = sat (0.546875 * rawSawPhase + 0.3125 * tap1 + (51.0 / 64.0) * amt_ * tap2);
        const double hp = sat (0.998046875 * loop + dc_);
        dc_ = sat (dc_ - 0.00390625 * hp);

        buffer_[static_cast<std::size_t> (write_)] = static_cast<float> (0.65625 * hp);
        write_ = (write_ + 1) & mask;
        return 0.359375 * hp;
    }

private:
    double readLerp (double delaySamples) const noexcept
    {
        const double pos = static_cast<double> (write_) - delaySamples + size;
        const int idx = static_cast<int> (pos);
        const double frac = pos - idx;
        const double a = buffer_[static_cast<std::size_t> (idx & mask)];
        const double b = buffer_[static_cast<std::size_t> ((idx + 1) & mask)];
        return a + frac * (b - a);
    }

    static constexpr int size = 8192, mask = size - 1;
    std::array<float, size> buffer_ {};
    int write_ { 0 };
    double d1_ { 100.0 }, d1Target_ { 100.0 };
    double h_ { 0.0 }, hTarget_ { 0.0 };
    double amt_ { 0.0 }, amtTarget_ { 0.0 };
    double dc_ { 0.0 };
    bool fresh_ { true };
};

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
            p1_ = osc::bipolarPhase (random.next());
            p2_ = osc::bipolarPhase (random.next());
            last2_ = 0.0;
            dc_ = 0.0;
            noise_.reset();
            feedback_.reset();
            freshSmooth_ = true;
        }
        if (wave == Wave1::FeedbackOsc)
            feedback_.retrigger();
    }

    void configure (const Controls& c) noexcept
    {
        c_ = c;
        inc1_ = std::clamp (2.0 * c.hz1 / kEngineRate, -0.998, 0.998);
        inc2_ = std::clamp (2.0 * c.hz2 / kEngineRate, -0.998, 0.998);
        if (c.wave1 == Wave1::SuperSaw)
            super_.configure (c.hz1, c.ctrl1, c.ctrl2);
        else if (c.wave1 == Wave1::Noise)
            noise_.configure (c.hz1, c.ctrl1, c.ctrl2);
        else if (c.wave1 == Wave1::FeedbackOsc)
            feedback_.configure (c.hz1, c.ctrl1, c.ctrl2);

        shapeTarget_ = c.wave1 == Wave1::TriangleMod ? mapping::triangleModOffset (c.ctrl1)
                     : c.wave1 == Wave1::Square      ? mapping::pulseWidth (c.ctrl1)
                                                     : mapping::waveShape (c.ctrl1);
        pw2Target_ = mapping::pulseWidth (c.width2);
        balTarget_ = std::clamp (c.gain2, 0.0, 1.0);
        if (freshSmooth_)
        {
            shape_ = shapeTarget_;
            pw2_ = pw2Target_;
            bal_ = balTarget_;
            freshSmooth_ = false;
        }

        fmGain_ = mapping::crossModGain (c.xmod);
        fm_ = c.xmod > 0.0 && c.wave1 != Wave1::SuperSaw && c.wave1 != Wave1::Noise;
    }

    double process (Random& random) noexcept
    {
        shape_ += (1.0 / 2048.0) * (shapeTarget_ - shape_);
        pw2_ += (1.0 / 2048.0) * (pw2Target_ - pw2_);
        bal_ += (1.0 / 64.0) * (balTarget_ - bal_);

        double first = 0.0;
        double masterPhase = 0.0;
        const double inc1Eff = fm_ ? std::clamp (inc1_ + fmGain_ * last2_, -0.998, 0.998) : inc1_;

        if (c_.wave1 == Wave1::SuperSaw)
        {
            first = super_.process();
            masterPhase = super_.centrePhase();
        }
        else
        {
            p1_ = wrap2 (p1_ + inc1Eff);
            masterPhase = p1_;
            switch (c_.wave1)
            {
                case Wave1::SuperSaw: break;
                case Wave1::TriangleMod: first = osc::triangleModHardware (p1_, inc1Eff, shape_); break;
                case Wave1::Noise: first = noise_.process (random); break;
                case Wave1::FeedbackOsc: first = feedback_.process (p1_); break;
                case Wave1::Square: first = osc::pulseHardware (p1_, inc1Eff, shape_); break;
                case Wave1::Saw: first = osc::shapedSawHardware (p1_, inc1Eff, shape_); break;
                case Wave1::Triangle: first = osc::shapedTriangleHardware (p1_, shape_); break;
            }
        }

        double second = 0.0;
        if (c_.sync)
        {
            const double ratio = c_.hz2 / std::max (c_.hz1, 1.0);
            switch (c_.wave2)
            {
                case Wave2::Square:   second = osc::syncPulseHardware (masterPhase, inc1Eff, inc2_, ratio, pw2_); break;
                case Wave2::Saw:      second = osc::syncSawHardware (masterPhase, inc1Eff, inc2_, ratio); break;
                case Wave2::Triangle: second = osc::syncTriangleHardware (masterPhase, inc1Eff, ratio); break;
            }
        }
        else
        {
            p2_ = wrap2 (p2_ + inc2_);
            switch (c_.wave2)
            {
                case Wave2::Square:   second = osc::pulseHardware (p2_, inc2_, pw2_); break;
                case Wave2::Saw:      second = osc::osc2SawHardware (p2_, inc2_); break;
                case Wave2::Triangle: second = osc::osc2TriangleHardware (p2_); break;
            }
        }
        last2_ = c_.wave2 == Wave2::Triangle ? (second * 1.6) : second;

        if (c_.ring)
            second = sat (4.0 * first * second);

        const double mix = (1.0 - bal_) * first + bal_ * second;
        const double y = sat (mix + dc_);
        dc_ = sat (dc_ - 0.00048828125 * y);
        return y;
    }

private:
    Controls c_;
    SuperSaw super_;
    NoiseOsc noise_;
    FeedbackOsc feedback_;
    double p1_ { 0.0 }, p2_ { 0.0 }, inc1_ { 0.0 }, inc2_ { 0.0 };
    double shape_ { 0.0 }, shapeTarget_ { 0.0 };
    double pw2_ { 0.0 }, pw2Target_ { 0.0 };
    double bal_ { 0.0 }, balTarget_ { 0.0 };
    double last2_ { 0.0 }, fmGain_ { 0.0 }, dc_ { 0.0 };
    bool fm_ { false }, freshSmooth_ { true };
};
} // namespace hypra
