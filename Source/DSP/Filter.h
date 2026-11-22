// Filters. Svf is a trapezoidal section used inside the oscillators; Filter
// is the voice filter.
#pragma once

#include "Core.h"
#include "HypraPatch.h"
#include "Mapping.h"

#include <array>

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

// [measured, black box] The voice filter. Its response fits a Chamberlin
// state-variable section at 88.2 kHz to 0.02 dB: all-pole low-pass, with the
// band and high outputs for the other types. -24 dB is two identical
// sections. The -24 dB output is scaled by the damping q; the -12 dB output
// by min(1, 0.5 + 2.8 q), a fit to three measured resonance settings.
class Filter
{
public:
    void reset() noexcept
    {
        low_ = { 0.0, 0.0 };
        band_ = { 0.0, 0.0 };
        fresh_ = true;
    }

    // coefficient = 2 sin(pi fc / 88200), from mapping::cutoffCoefficient.
    void configure (double coefficient, double resonance, FilterType type, bool steep) noexcept
    {
        type_ = type;
        steep_ = steep;
        q_ = mapping::resonanceQ (resonance);
        gain_ = steep ? q_ : std::min (1.0, 0.5 + 2.8 * q_);
        target_ = std::clamp (coefficient, 1.0e-4, 1.0);
        if (fresh_)
            f_ = target_;
        fresh_ = false;
        step_ = (target_ - f_) / kControlInterval;
        left_ = kControlInterval;
    }

    double process (double x) noexcept
    {
        if (left_ > 0)
        {
            --left_;
            f_ += step_;
        }
        double y = section (0, x);
        if (steep_)
            y = section (1, y);
        return gain_ * y;
    }

private:
    double section (std::size_t i, double x) noexcept
    {
        low_[i] += f_ * band_[i];
        const double high = x - low_[i] - q_ * band_[i];
        band_[i] += f_ * high;
        // Fixed point would saturate; this only stops a runaway.
        band_[i] = std::clamp (band_[i], -64.0, 64.0);
        low_[i] = std::clamp (low_[i], -64.0, 64.0);
        return type_ == FilterType::LowPass ? low_[i] : type_ == FilterType::BandPass ? band_[i] : high;
    }

    std::array<double, 2> low_ {}, band_ {};
    FilterType type_ { FilterType::LowPass };
    bool steep_ { true }, fresh_ { true };
    double f_ { 0.01 }, target_ { 0.01 }, step_ { 0.0 }, q_ { 1.0 }, gain_ { 1.0 };
    int left_ { 0 };
};
} // namespace hypra
