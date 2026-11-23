// Two-stage state-variable voice filter with per-stage rail saturation.
#pragma once

#include "Core.h"
#include "TranceraPatch.h"
#include "Mapping.h"

#include <array>

namespace trancera
{
class Filter
{
public:
    void reset() noexcept
    {
        low_ = { 0.0, 0.0 };
        band_ = { 0.0, 0.0 };
        fresh_ = true;
    }

    void configure (double coefficient, double resonance, FilterType type, bool steep,
                    double fSmoothAlpha = 1.0 / 512.0, double qSmoothAlpha = 1.0 / 512.0) noexcept
    {
        type_ = type;
        steep_ = steep;
        fSmoothAlpha_ = fSmoothAlpha;
        qSmoothAlpha_ = qSmoothAlpha;
        fTarget_ = std::clamp (coefficient, 1.0 / 1024.0, 1.0);
        qHalfTarget_ = mapping::resonanceQHalf (resonance, fTarget_);
        comp12_ = steep ? 1.0 : mapping::resonance12AmpComp (resonance);
        if (fresh_)
        {
            f_ = fTarget_;
            qHalf_ = qHalfTarget_;
            fresh_ = false;
        }
    }

    double process (double x) noexcept
    {
        f_ += fSmoothAlpha_ * (fTarget_ - f_);
        qHalf_ += qSmoothAlpha_ * (qHalfTarget_ - qHalf_);
        const double q = 2.0 * qHalf_;

        low_[0] = sat (low_[0] + f_ * band_[0]);
        const double h0 = sat (x - sat (low_[0] + q * band_[0]));
        band_[0] = sat (band_[0] + f_ * h0);
        const double y = type_ == FilterType::LowPass ? low_[0]
                       : type_ == FilterType::BandPass ? band_[0] : h0;
        if (! steep_)
            return comp12_ * y;

        const double x1 = sat (q * y);
        low_[1] = sat (low_[1] + f_ * band_[1]);
        const double h1 = sat (x1 - sat (low_[1] + q * band_[1]));
        band_[1] = sat (band_[1] + f_ * h1);
        return type_ == FilterType::LowPass ? low_[1]
             : type_ == FilterType::BandPass ? band_[1] : h1;
    }

private:
    std::array<double, 2> low_ {}, band_ {};
    FilterType type_ { FilterType::LowPass };
    bool steep_ { true }, fresh_ { true };
    double f_ { 5.0 / 128.0 }, fTarget_ { 5.0 / 128.0 }, fSmoothAlpha_ { 1.0 / 512.0 };
    double qHalf_ { 0.5 }, qHalfTarget_ { 0.5 }, qSmoothAlpha_ { 1.0 / 512.0 }, comp12_ { 1.0 };
};
} // namespace trancera
