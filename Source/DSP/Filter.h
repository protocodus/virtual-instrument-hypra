// [inferred topology] OM pp.59-61 gives three types, two slopes and
// oscillation at full resonance. The DSP algorithm is not published, so this
// is a trapezoidal state-variable section, twice for -24 dB/oct.
#pragma once

#include "Core.h"
#include "HypraPatch.h"

namespace hypra
{
class Svf
{
public:
    struct Out { double low, band, high; };

    void reset() noexcept { s1_ = s2_ = 0.0; }

    void configure (double hz, double damping) noexcept
    {
        g_ = std::tan (kPi * std::min (hz, 0.45 * kEngineRate) / kEngineRate);
        k_ = damping;
        a1_ = 1.0 / (1.0 + g_ * (g_ + k_));
    }

    Out process (double x) noexcept { return tick (x, g_, k_, a1_); }

    // Damping rises with band-pass amplitude. That bounds self-oscillation
    // at a steady sine and is negligible at ordinary levels.
    Out processLimited (double x, double g, double k) noexcept
    {
        const double limited = k + 0.02 * std::min (s1_ * s1_, 64.0);
        return tick (x, g, limited, 1.0 / (1.0 + g * (g + limited)));
    }

private:
    Out tick (double x, double g, double k, double a1) noexcept
    {
        const double a2 = g * a1, v3 = x - s2_;
        const double band = a1 * s1_ + a2 * v3;
        const double low = s2_ + a2 * s1_ + g * a2 * v3;
        s1_ = 2.0 * band - s1_;
        s2_ = 2.0 * low - s2_;
        return { low, band, x - k * band - low };
    }

    double g_ { 0.0 }, k_ { 1.0 }, a1_ { 1.0 };
    double s1_ { 0.0 }, s2_ { 0.0 };
};

class Filter
{
public:
    void reset() noexcept
    {
        first_.reset();
        second_.reset();
        fresh_ = true;
    }

    void configure (double hz, double resonance, FilterType type, bool steep) noexcept
    {
        if (type != type_ || steep != steep_ || resonance != resonance_)
            setShape (resonance, type, steep);
        gTarget_ = std::tan (kPi * std::min (hz, 0.45 * kEngineRate) / kEngineRate);
        if (fresh_)
            g_ = gTarget_;
        fresh_ = false;
        step_ = (gTarget_ - g_) / kControlInterval;
        left_ = kControlInterval;
    }

    double process (double x) noexcept
    {
        if (left_ > 0)
        {
            --left_;
            g_ += step_;
        }
        x *= inputGain_;
        if (! steep_)
            return select (second_.processLimited (x, g_, k2_));
        const double y = select (first_.processLimited (x, g_, k1_));
        return outputGain_ * select (second_.processLimited (y, g_, k2_));
    }

private:
    void setShape (double resonance, FilterType type, bool steep) noexcept
    {
        type_ = type;
        steep_ = steep;
        resonance_ = resonance;
        const double r = std::clamp (resonance, 0.0, 1.0);
        const bool band = type == FilterType::BandPass;
        // Butterworth dampings at zero resonance; just negative at the top.
        const double q1 = band ? 1.41421356 : 1.84775907;
        const double q2 = ! steep || band ? 1.41421356 : 0.76536686;
        k1_ = q1 + (1.0 - q1) * r;
        k2_ = q2 * std::pow (1.0 - r, 1.8) - 0.012 * std::pow (r, 10.0);
        inputGain_ = 1.0 / (1.0 + 1.2 * r); // [inferred] level compensation
        outputGain_ = band ? 1.5 : 1.0;
    }

    double select (const Svf::Out& out) const noexcept
    {
        return type_ == FilterType::LowPass ? out.low
             : type_ == FilterType::BandPass ? out.band : out.high;
    }

    Svf first_, second_;
    FilterType type_ { FilterType::LowPass };
    bool steep_ { true }, fresh_ { true };
    double resonance_ { -1.0 };
    double g_ { 0.01 }, gTarget_ { 0.01 }, step_ { 0.0 };
    double k1_ { 1.848 }, k2_ { 0.765 }, inputGain_ { 1.0 }, outputGain_ { 1.0 };
    int left_ { 0 };
};
} // namespace hypra
