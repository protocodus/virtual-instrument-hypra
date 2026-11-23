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

    void processBlockQ23 (Q23* buf, int n) noexcept
    {
        if (! steep_)
        {
            switch (type_)
            {
                case FilterType::LowPass:  runBlockImpl<false, FilterType::LowPass> (buf, n); break;
                case FilterType::BandPass: runBlockImpl<false, FilterType::BandPass> (buf, n); break;
                case FilterType::HighPass: runBlockImpl<false, FilterType::HighPass> (buf, n); break;
            }
            return;
        }
        switch (type_)
        {
            case FilterType::LowPass:  runBlockImpl<true, FilterType::LowPass> (buf, n); break;
            case FilterType::BandPass: runBlockImpl<true, FilterType::BandPass> (buf, n); break;
            case FilterType::HighPass: runBlockImpl<true, FilterType::HighPass> (buf, n); break;
        }
    }

    static void process4BlockQ23 (Filter* const f[4], Q23* const bufs[4], int n, FilterType type, bool steep) noexcept
    {
        if (! steep)
        {
            switch (type)
            {
                case FilterType::LowPass:  run4BlockImpl<false, FilterType::LowPass> (f, bufs, n); break;
                case FilterType::BandPass: run4BlockImpl<false, FilterType::BandPass> (f, bufs, n); break;
                case FilterType::HighPass: run4BlockImpl<false, FilterType::HighPass> (f, bufs, n); break;
            }
            return;
        }
        switch (type)
        {
            case FilterType::LowPass:  run4BlockImpl<true, FilterType::LowPass> (f, bufs, n); break;
            case FilterType::BandPass: run4BlockImpl<true, FilterType::BandPass> (f, bufs, n); break;
            case FilterType::HighPass: run4BlockImpl<true, FilterType::HighPass> (f, bufs, n); break;
        }
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
    template <bool Steep, FilterType Type>
    void runBlockImpl (Q23* buf, int n) noexcept
    {
        Q23 low0 = low_[0], band0 = band_[0], low1 = low_[1], band1 = band_[1], s1Prev = stage1Prev_;
        Coeff15 fc = fc_, qc = qc_;
        const Coeff15 comp = comp12Split_;
        Q23 fCur = f_, qCur = qHalf_;
        bool fMob = fMoving_, qMob = qMoving_;

        for (int i = 0; i < n; ++i)
        {
            if (fMob)
            {
                const Q23 nextF = sat24 (fCur + mulSplit15 (sat24 (fTarget_ - fCur), fAlpha_.hi, fAlpha_.lo));
                if (nextF == fCur)
                    fMob = false;
                else
                {
                    fCur = nextF;
                    fc = splitCoeff15 (fCur);
                }
            }
            if (qMob)
            {
                const Q23 nextQ = sat24 (qCur + mulSplit15 (sat24 (qHalfTarget_ - qCur), qAlpha_.hi, qAlpha_.lo));
                if (nextQ == qCur)
                    qMob = false;
                else
                {
                    qCur = nextQ;
                    qc = splitCoeff15 (qCur);
                }
            }

            low0 = sat24 (low0 + mulSplit15 (band0, fc.hi, fc.lo));
            const Q23 h0 = sat24 (buf[i] - sat24 (low0 + mulSplit15x2 (band0, qc.hi, qc.lo)));
            band0 = sat24 (band0 + mulSplit15 (h0, fc.hi, fc.lo));
            const Q23 y = Type == FilterType::LowPass ? low0 : (Type == FilterType::BandPass ? band0 : h0);
            if constexpr (! Steep)
            {
                buf[i] = sat24 (mulSplit15 (y, comp.hi, comp.lo));
            }
            else
            {
                const Q23 stage2In = s1Prev;
                s1Prev = y;
                low1 = sat24 (low1 + mulSplit15 (band1, fc.hi, fc.lo));
                const Q23 h1 = sat24 (mulSplit15x2 (stage2In, qc.hi, qc.lo) - sat24 (low1 + mulSplit15x2 (band1, qc.hi, qc.lo)));
                band1 = sat24 (band1 + mulSplit15 (h1, fc.hi, fc.lo));
                buf[i] = Type == FilterType::LowPass ? low1 : (Type == FilterType::BandPass ? band1 : h1);
            }
        }
        low_[0] = low0; band_[0] = band0; low_[1] = low1; band_[1] = band1; stage1Prev_ = s1Prev;
        f_ = fCur; qHalf_ = qCur; fc_ = fc; qc_ = qc; fMoving_ = fMob; qMoving_ = qMob;
    }

    template <bool Steep, FilterType Type>
    static void run4BlockImpl (Filter* const f[4], Q23* const bufs[4], int n) noexcept
    {
        Int32x4 low0 = Int32x4::make (f[0]->low_[0], f[1]->low_[0], f[2]->low_[0], f[3]->low_[0]);
        Int32x4 band0 = Int32x4::make (f[0]->band_[0], f[1]->band_[0], f[2]->band_[0], f[3]->band_[0]);
        Int32x4 low1 = Int32x4::make (f[0]->low_[1], f[1]->low_[1], f[2]->low_[1], f[3]->low_[1]);
        Int32x4 band1 = Int32x4::make (f[0]->band_[1], f[1]->band_[1], f[2]->band_[1], f[3]->band_[1]);
        Int32x4 s1Prev = Int32x4::make (f[0]->stage1Prev_, f[1]->stage1Prev_, f[2]->stage1Prev_, f[3]->stage1Prev_);

        Int32x4 fcHi = Int32x4::make (f[0]->fc_.hi, f[1]->fc_.hi, f[2]->fc_.hi, f[3]->fc_.hi);
        Int32x4 fcLo = Int32x4::make (f[0]->fc_.lo, f[1]->fc_.lo, f[2]->fc_.lo, f[3]->fc_.lo);
        Int32x4 qcHi = Int32x4::make (f[0]->qc_.hi, f[1]->qc_.hi, f[2]->qc_.hi, f[3]->qc_.hi);
        Int32x4 qcLo = Int32x4::make (f[0]->qc_.lo, f[1]->qc_.lo, f[2]->qc_.lo, f[3]->qc_.lo);

        const bool fAny = f[0]->fMoving_ || f[1]->fMoving_ || f[2]->fMoving_ || f[3]->fMoving_;
        const bool qAny = f[0]->qMoving_ || f[1]->qMoving_ || f[2]->qMoving_ || f[3]->qMoving_;
        Int32x4 fCur = Int32x4::make (f[0]->f_, f[1]->f_, f[2]->f_, f[3]->f_);
        Int32x4 qCur = Int32x4::make (f[0]->qHalf_, f[1]->qHalf_, f[2]->qHalf_, f[3]->qHalf_);
        const Int32x4 fTgt = Int32x4::make (f[0]->fTarget_, f[1]->fTarget_, f[2]->fTarget_, f[3]->fTarget_);
        const Int32x4 qTgt = Int32x4::make (f[0]->qHalfTarget_, f[1]->qHalfTarget_, f[2]->qHalfTarget_, f[3]->qHalfTarget_);
        const Int32x4 fAHi = Int32x4::make (f[0]->fAlpha_.hi, f[1]->fAlpha_.hi, f[2]->fAlpha_.hi, f[3]->fAlpha_.hi);
        const Int32x4 fALo = Int32x4::make (f[0]->fAlpha_.lo, f[1]->fAlpha_.lo, f[2]->fAlpha_.lo, f[3]->fAlpha_.lo);
        const Int32x4 qAHi = Int32x4::make (f[0]->qAlpha_.hi, f[1]->qAlpha_.hi, f[2]->qAlpha_.hi, f[3]->qAlpha_.hi);
        const Int32x4 qALo = Int32x4::make (f[0]->qAlpha_.lo, f[1]->qAlpha_.lo, f[2]->qAlpha_.lo, f[3]->qAlpha_.lo);
        const Int32x4 mask7f = Int32x4::splat (0x7f);
        Int32x4 prevF = fCur, prevQ = qCur;

        for (int i = 0; i < n; ++i)
        {
            if (fAny)
            {
                prevF = fCur;
                fCur = sat24x4 (fCur + mulSplit15x4 (sat24x4 (fTgt - fCur), fAHi, fALo));
                fcHi = fCur >> 16;
                fcLo = (fCur >> 9) & mask7f;
            }
            if (qAny)
            {
                prevQ = qCur;
                qCur = sat24x4 (qCur + mulSplit15x4 (sat24x4 (qTgt - qCur), qAHi, qALo));
                qcHi = qCur >> 16;
                qcLo = (qCur >> 9) & mask7f;
            }

            const Int32x4 x = Int32x4::make (bufs[0][i], bufs[1][i], bufs[2][i], bufs[3][i]);
            low0 = sat24x4 (low0 + mulSplit15x4 (band0, fcHi, fcLo));
            const Int32x4 h0 = sat24x4 (x - sat24x4 (low0 + mulSplit15x2x4 (band0, qcHi, qcLo)));
            band0 = sat24x4 (band0 + mulSplit15x4 (h0, fcHi, fcLo));
            const Int32x4 y = Type == FilterType::LowPass ? low0 : (Type == FilterType::BandPass ? band0 : h0);

            Int32x4 out;
            if constexpr (! Steep)
            {
                const Int32x4 compHi = Int32x4::make (f[0]->comp12Split_.hi, f[1]->comp12Split_.hi, f[2]->comp12Split_.hi, f[3]->comp12Split_.hi);
                const Int32x4 compLo = Int32x4::make (f[0]->comp12Split_.lo, f[1]->comp12Split_.lo, f[2]->comp12Split_.lo, f[3]->comp12Split_.lo);
                out = sat24x4 (mulSplit15x4 (y, compHi, compLo));
            }
            else
            {
                const Int32x4 stage2In = s1Prev;
                s1Prev = y;
                low1 = sat24x4 (low1 + mulSplit15x4 (band1, fcHi, fcLo));
                const Int32x4 h1 = sat24x4 (mulSplit15x2x4 (stage2In, qcHi, qcLo) - sat24x4 (low1 + mulSplit15x2x4 (band1, qcHi, qcLo)));
                band1 = sat24x4 (band1 + mulSplit15x4 (h1, fcHi, fcLo));
                out = Type == FilterType::LowPass ? low1 : (Type == FilterType::BandPass ? band1 : h1);
            }
            bufs[0][i] = out[0];
            bufs[1][i] = out[1];
            bufs[2][i] = out[2];
            bufs[3][i] = out[3];
        }

        for (int k = 0; k < 4; ++k)
        {
            f[k]->low_[0] = low0[k]; f[k]->band_[0] = band0[k];
            f[k]->low_[1] = low1[k]; f[k]->band_[1] = band1[k];
            f[k]->stage1Prev_ = s1Prev[k];
            if (fAny && f[k]->fMoving_)
            {
                f[k]->f_ = fCur[k];
                f[k]->fc_ = { fcHi[k], fcLo[k] };
                if (prevF[k] == fCur[k])
                    f[k]->fMoving_ = false;
            }
            if (qAny && f[k]->qMoving_)
            {
                f[k]->qHalf_ = qCur[k];
                f[k]->qc_ = { qcHi[k], qcLo[k] };
                if (prevQ[k] == qCur[k])
                    f[k]->qMoving_ = false;
            }
        }
    }

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
