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
        low_ = { 0, 0 };
        band_ = { 0, 0 };
        stage1Prev_ = 0;
        fMoving_ = qMoving_ = false;
        fresh_ = true;
        lowD_ = { 0.0, 0.0 };
        bandD_ = { 0.0, 0.0 };
        freshD_ = true;
    }

    void configure (double coefficient, double resonance, FilterType type, bool steep,
                    double fSmoothAlpha = 1.0 / 512.0, double qSmoothAlpha = 1.0 / 512.0) noexcept
    {
        const double fClamped = std::clamp (coefficient, 1.0 / 1024.0, 1.0);
        const double qHalf = mapping::resonanceQHalf (resonance, fClamped);
        const double comp = steep ? 1.0 : mapping::resonance12AmpComp (resonance);
        fTargetD_ = fClamped;
        qHalfTargetD_ = qHalf;
        comp12D_ = comp;
        fSmoothD_ = std::clamp (fSmoothAlpha, 0.0, 1.0);
        qSmoothD_ = std::clamp (qSmoothAlpha, 0.0, 1.0);
        if (freshD_)
        {
            fD_ = fTargetD_;
            qHalfD_ = qHalfTargetD_;
            freshD_ = false;
        }
        configureQ23 (toQ23 (fClamped), toQ23 (qHalf), toQ23 (comp), type, steep,
                      toQ23 (fSmoothAlpha), toQ23 (qSmoothAlpha));
    }

    void configureQ23 (Q23 fTarget, Q23 qHalfTarget, Q23 comp12, FilterType type, bool steep,
                       Q23 fSmoothAlpha = 1 << 14, Q23 qSmoothAlpha = 1 << 14) noexcept
    {
        type_ = type;
        steep_ = steep;
        fAlpha_ = splitCoeff15 (std::clamp (fSmoothAlpha, 1, kQ23Max));
        qAlpha_ = splitCoeff15 (std::clamp (qSmoothAlpha, 1, kQ23Max));
        fTarget_ = std::clamp (fTarget, 8192, kQ23Max);
        qHalfTarget_ = std::clamp (qHalfTarget, 0, kQ23One >> 1);
        comp12Split_ = splitCoeff15 (std::clamp (comp12, 0, kQ23One));
        if (fresh_)
        {
            f_ = fTarget_;
            qHalf_ = qHalfTarget_;
            fc_ = splitCoeff15 (f_);
            qc_ = splitCoeff15 (qHalf_);
            fMoving_ = qMoving_ = false;
            fresh_ = false;
        }
        else
        {
            fMoving_ = f_ != fTarget_;
            qMoving_ = qHalf_ != qHalfTarget_;
        }
    }

    Q23 processQ23 (Q23 x) noexcept
    {
        if (fMoving_)
        {
            const Q23 nextF = sat24 (f_ + mulSplit15 (sat24 (fTarget_ - f_), fAlpha_.hi, fAlpha_.lo));
            if (nextF == f_)
                fMoving_ = false;
            else
            {
                f_ = nextF;
                fc_ = splitCoeff15 (f_);
            }
        }
        if (qMoving_)
        {
            const Q23 nextQ = sat24 (qHalf_ + mulSplit15 (sat24 (qHalfTarget_ - qHalf_), qAlpha_.hi, qAlpha_.lo));
            if (nextQ == qHalf_)
                qMoving_ = false;
            else
            {
                qHalf_ = nextQ;
                qc_ = splitCoeff15 (qHalf_);
            }
        }

        low_[0] = sat24 (low_[0] + mulSplit15 (band_[0], fc_.hi, fc_.lo));
        const Q23 h0 = sat24 (x - sat24 (low_[0] + mulSplit15x2 (band_[0], qc_.hi, qc_.lo)));
        band_[0] = sat24 (band_[0] + mulSplit15 (h0, fc_.hi, fc_.lo));
        const Q23 y = type_ == FilterType::LowPass ? low_[0]
                    : type_ == FilterType::BandPass ? band_[0] : h0;
        if (! steep_)
            return sat24 (mulSplit15 (y, comp12Split_.hi, comp12Split_.lo));

        // Second stage consumes Stage 1's latched output from the preceding sample.
        const Q23 stage2In = stage1Prev_;
        stage1Prev_ = y;
        low_[1] = sat24 (low_[1] + mulSplit15 (band_[1], fc_.hi, fc_.lo));
        const Q23 h1 = sat24 (mulSplit15x2 (stage2In, qc_.hi, qc_.lo) - sat24 (low_[1] + mulSplit15x2 (band_[1], qc_.hi, qc_.lo)));
        band_[1] = sat24 (band_[1] + mulSplit15 (h1, fc_.hi, fc_.lo));
        return type_ == FilterType::LowPass ? low_[1]
             : type_ == FilterType::BandPass ? band_[1] : h1;
    }

    double process (double x) noexcept
    {
        fD_ += fSmoothD_ * (fTargetD_ - fD_);
        qHalfD_ += qSmoothD_ * (qHalfTargetD_ - qHalfD_);

        lowD_[0] = sat (lowD_[0] + fD_ * bandD_[0]);
        const double h0 = sat (x - sat (lowD_[0] + 2.0 * qHalfD_ * bandD_[0]));
        bandD_[0] = sat (bandD_[0] + fD_ * h0);
        const double y = type_ == FilterType::LowPass ? lowD_[0]
                       : type_ == FilterType::BandPass ? bandD_[0] : h0;
        if (! steep_)
            return sat (y * comp12D_);

        lowD_[1] = sat (lowD_[1] + fD_ * bandD_[1]);
        const double h1 = sat (2.0 * qHalfD_ * y - sat (lowD_[1] + 2.0 * qHalfD_ * bandD_[1]));
        bandD_[1] = sat (bandD_[1] + fD_ * h1);
        return type_ == FilterType::LowPass ? lowD_[1]
             : type_ == FilterType::BandPass ? bandD_[1] : h1;
    }

private:
    std::array<Q23, 2> low_ {}, band_ {};
    Q23 stage1Prev_ { 0 };
    Coeff15 fc_ { splitCoeff15 (327680) }, qc_ { splitCoeff15 (kQ23One >> 1) };
    Coeff15 fAlpha_ { splitCoeff15 (1 << 14) }, qAlpha_ { splitCoeff15 (1 << 14) }, comp12Split_ { splitCoeff15 (kQ23Max) };
    Q23 f_ { 327680 }, fTarget_ { 327680 };
    Q23 qHalf_ { kQ23One >> 1 }, qHalfTarget_ { kQ23One >> 1 };
    FilterType type_ { FilterType::LowPass };
    bool steep_ { true }, fMoving_ { false }, qMoving_ { false }, fresh_ { true };

    std::array<double, 2> lowD_ {}, bandD_ {};
    double fD_ { 5.0 / 128.0 }, fTargetD_ { 5.0 / 128.0 }, fSmoothD_ { 1.0 / 512.0 };
    double qHalfD_ { 0.5 }, qHalfTargetD_ { 0.5 }, qSmoothD_ { 1.0 / 512.0 }, comp12D_ { 1.0 };
    bool freshD_ { true };
};
} // namespace trancera
