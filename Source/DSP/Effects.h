// Part effects chain: ToneControl -> Delay -> Multi-FX Chorus.
#pragma once

#include "Core.h"
#include "TranceraPatch.h"
#include "Mapping.h"

#include <array>
#include <vector>

namespace trancera
{
class ToneControl
{
public:
    void reset() noexcept
    {
        xB1_ = yB1_ = xT1_ = yT1_ = 0.0;
        xB1Q_ = yB1Q_ = yBoutPrevQ_ = xT1Q_ = yT1Q_ = 0;
        snap();
        fresh_ = true;
    }

    void snap() noexcept
    {
        negBB_ = negBBTarget_;
        aBQ23_ = aBTarget_;
        negBT_ = negBTTarget_;
        aTQ23_ = aTTarget_;
        gCutQ23_ = gCutTarget_;
        gBoostQ23_ = gBoostTarget_;
        updateSplits();
        moving_ = false;
        fresh_ = false;
    }

    void configure (int bass, int treble) noexcept
    {
        const auto bi = static_cast<std::size_t> (std::clamp (bass + 64, 0, 127));
        const auto ti = static_cast<std::size_t> (std::clamp (treble + 64, 0, 127));
        bB_ = static_cast<double> (mapping::toneBassB[bi]) / 32768.0;
        aB_ = static_cast<double> (mapping::toneBassA[bi]) / 32768.0;
        bT_ = static_cast<double> (mapping::toneTrebleB[ti]) / 32768.0;
        aT_ = static_cast<double> (mapping::toneTrebleA[ti]) / 32768.0;
        gCut_ = static_cast<double> (mapping::toneTrebleCut[ti]) / 16384.0;
        gBoost_ = static_cast<double> (mapping::toneTrebleBoost[ti]) / 16384.0;

        negBBTarget_ = -static_cast<std::int32_t> (mapping::toneBassB[bi]) * 256;
        aBTarget_ = static_cast<std::int32_t> (mapping::toneBassA[bi]) * 256;
        negBTTarget_ = -static_cast<std::int32_t> (mapping::toneTrebleB[ti]) * 256;
        aTTarget_ = static_cast<std::int32_t> (mapping::toneTrebleA[ti]) * 256;
        gCutTarget_ = static_cast<std::int32_t> (mapping::toneTrebleCut[ti]) * 256;
        gBoostTarget_ = static_cast<std::int32_t> (mapping::toneTrebleBoost[ti]) * 256;

        if (fresh_)
        {
            snap();
        }
        else
        {
            moving_ = negBB_ != negBBTarget_ || aBQ23_ != aBTarget_
                      || negBT_ != negBTTarget_ || aTQ23_ != aTTarget_
                      || gCutQ23_ != gCutTarget_ || gBoostQ23_ != gBoostTarget_;
        }
    }

    // Copies filter delay state and smoothed coefficients when a dual-mono voice bus widens to stereo.
    void copyStateFrom (const ToneControl& other) noexcept
    {
        xB1Q_ = other.xB1Q_;
        yB1Q_ = other.yB1Q_;
        yBoutPrevQ_ = other.yBoutPrevQ_;
        xT1Q_ = other.xT1Q_;
        yT1Q_ = other.yT1Q_;
        negBB_ = other.negBB_;
        negBBTarget_ = other.negBBTarget_;
        aBQ23_ = other.aBQ23_;
        aBTarget_ = other.aBTarget_;
        negBT_ = other.negBT_;
        negBTTarget_ = other.negBTTarget_;
        aTQ23_ = other.aTQ23_;
        aTTarget_ = other.aTTarget_;
        gCutQ23_ = other.gCutQ23_;
        gCutTarget_ = other.gCutTarget_;
        gBoostQ23_ = other.gBoostQ23_;
        gBoostTarget_ = other.gBoostTarget_;
        moving_ = other.moving_;
        fresh_ = other.fresh_;
        updateSplits();
    }

    // Internal hardware gain staging with rail clipping and -12 dB flat headroom.
    Q23 processSaturatingQ23 (Q23 x) noexcept
    {
        if (moving_)
            stepCoeffs();
        // Treble stage runs on the previous sample's cut-scaled bass output before bass updates its latch.
        const Q23 yT = sat24 (yBoutPrevQ_ + mulSplit15 (xT1Q_, negBTSplit_.hi, negBTSplit_.lo)
                                          + mulSplit15 (yT1Q_, aTSplit_.hi, aTSplit_.lo));
        xT1Q_ = yBoutPrevQ_;
        yT1Q_ = yT;
        const Q23 out = sat24 (mulSplit15x2 (yT, gBoostSplit_.hi, gBoostSplit_.lo));

        const Q23 yB = sat24 (x + mulSplit15 (xB1Q_, negBBSplit_.hi, negBBSplit_.lo)
                                + mulSplit15 (yB1Q_, aBSplit_.hi, aBSplit_.lo));
        xB1Q_ = x;
        yB1Q_ = yB;
        yBoutPrevQ_ = sat24 (mulSplit15x2 (yB, gCutSplit_.hi, gCutSplit_.lo));
        return out;
    }

    void processBlockSaturatingQ23 (Q23* buf, int n) noexcept
    {
        int i = 0;
        while (i < n && moving_)
        {
            buf[i] = processSaturatingQ23 (buf[i]);
            ++i;
        }
        if (i >= n)
            return;
        Q23 xB1 = xB1Q_, yB1 = yB1Q_, yBoutPrev = yBoutPrevQ_, xT1 = xT1Q_, yT1 = yT1Q_;
        const std::int32_t nbHi = negBBSplit_.hi, nbLo = negBBSplit_.lo;
        const std::int32_t abHi = aBSplit_.hi, abLo = aBSplit_.lo;
        const std::int32_t ntHi = negBTSplit_.hi, ntLo = negBTSplit_.lo;
        const std::int32_t atHi = aTSplit_.hi, atLo = aTSplit_.lo;
        const std::int32_t gcHi = gCutSplit_.hi, gcLo = gCutSplit_.lo;
        const std::int32_t gbHi = gBoostSplit_.hi, gbLo = gBoostSplit_.lo;

        for (; i < n; ++i)
        {
            const Q23 x = buf[i];
            const Q23 yT = sat24 (yBoutPrev + mulSplit15 (xT1, ntHi, ntLo) + mulSplit15 (yT1, atHi, atLo));
            xT1 = yBoutPrev;
            yT1 = yT;
            buf[i] = sat24 (mulSplit15x2 (yT, gbHi, gbLo));

            const Q23 yB = sat24 (x + mulSplit15 (xB1, nbHi, nbLo) + mulSplit15 (yB1, abHi, abLo));
            xB1 = x;
            yB1 = yB;
            yBoutPrev = sat24 (mulSplit15x2 (yB, gcHi, gcLo));
        }

        xB1Q_ = xB1;
        yB1Q_ = yB1;
        yBoutPrevQ_ = yBoutPrev;
        xT1Q_ = xT1;
        yT1Q_ = yT1;
    }

    double processSaturating (double x) noexcept
    {
        return fromQ23 (processSaturatingQ23 (toQ23 (x)));
    }

    // Unclipped unity-normalized shelf response for standalone frequency sweeps.
    double process (double x) noexcept
    {
        const double yB = x - bB_ * xB1_ + aB_ * yB1_;
        xB1_ = x;
        yB1_ = yB;
        const double yBout = gCut_ * yB;
        const double yT = yBout - bT_ * xT1_ + aT_ * yT1_;
        xT1_ = yBout;
        yT1_ = yT;
        return 4.0 * gBoost_ * yT;
    }

private:
    void updateSplits() noexcept
    {
        negBBSplit_ = splitCoeff15 (negBB_);
        aBSplit_ = splitCoeff15 (aBQ23_);
        negBTSplit_ = splitCoeff15 (negBT_);
        aTSplit_ = splitCoeff15 (aTQ23_);
        gCutSplit_ = splitCoeff15 (gCutQ23_);
        gBoostSplit_ = splitCoeff15 (gBoostQ23_);
    }

    void stepCoeffs() noexcept
    {
        negBB_ = smoothStepQ23 (negBB_, negBBTarget_, 7);
        aBQ23_ = smoothStepQ23 (aBQ23_, aBTarget_, 7);
        negBT_ = smoothStepQ23 (negBT_, negBTTarget_, 7);
        aTQ23_ = smoothStepQ23 (aTQ23_, aTTarget_, 7);
        gCutQ23_ = smoothStepQ23 (gCutQ23_, gCutTarget_, 7);
        gBoostQ23_ = smoothStepQ23 (gBoostQ23_, gBoostTarget_, 7);
        updateSplits();
        moving_ = negBB_ != negBBTarget_ || aBQ23_ != aBTarget_
                  || negBT_ != negBTTarget_ || aTQ23_ != aTTarget_
                  || gCutQ23_ != gCutTarget_ || gBoostQ23_ != gBoostTarget_;
    }

    double bB_ { 31743.0 / 32768.0 }, aB_ { 31743.0 / 32768.0 };
    double bT_ { 30589.0 / 32768.0 }, aT_ { 30589.0 / 32768.0 };
    double gCut_ { 1.0 }, gBoost_ { 0.25 };
    double xB1_ { 0.0 }, yB1_ { 0.0 }, xT1_ { 0.0 }, yT1_ { 0.0 };

    Q23 negBB_ { -31743 * 256 }, negBBTarget_ { -31743 * 256 };
    Q23 aBQ23_ { 31743 * 256 }, aBTarget_ { 31743 * 256 };
    Q23 negBT_ { -30589 * 256 }, negBTTarget_ { -30589 * 256 };
    Q23 aTQ23_ { 30589 * 256 }, aTTarget_ { 30589 * 256 };
    Q23 gCutQ23_ { 16384 * 256 }, gCutTarget_ { 16384 * 256 };
    Q23 gBoostQ23_ { 4096 * 256 }, gBoostTarget_ { 4096 * 256 };
    Coeff15 negBBSplit_ { splitTable15 (-31743) }, aBSplit_ { splitTable15 (31743) };
    Coeff15 negBTSplit_ { splitTable15 (-30589) }, aTSplit_ { splitTable15 (30589) };
    Coeff15 gCutSplit_ { splitTable15 (16384) }, gBoostSplit_ { splitTable15 (4096) };
    Q23 xB1Q_ { 0 }, yB1Q_ { 0 }, yBoutPrevQ_ { 0 }, xT1Q_ { 0 }, yT1Q_ { 0 };
    bool moving_ { false }, fresh_ { true };
};

class Delay
{
public:
    void prepare()
    {
        buf_.assign (size, 0u);
        write_ = 0;
        reset();
    }

    void reset() noexcept
    {
        std::fill (buf_.begin(), buf_.end(), std::uint16_t { 0 });
        write_ = 0;
        lp_ = dc_ = 0;
        snap();
    }

    int maxDelayTicks() const noexcept
    {
        return static_cast<int> (std::max (d1Q16_, d1TargetQ16_) >> 16);
    }

    void configure (DelayType type, int time, int feedback, int level, int delaySync = 0, double bpm = 120.0) noexcept
    {
        type_ = type;
        const double rawSamples = delaySync > 0 ? mapping::delaySyncedSamples (type, delaySync, bpm)
                                                : mapping::delaySamples (type, time);
        d1TargetQ16_ = static_cast<std::int64_t> (std::llround (std::clamp (rawSamples, 2.0, static_cast<double> (size - 3)) * 65536.0));
        fbTarget_ = mapping::levelCurveQ23 (feedback);
        lvlTarget_ = mapping::levelCurveQ23 (level);
        fbMoving_ = fb_ != fbTarget_;
        lvlMoving_ = lvl_ != lvlTarget_;
        switch (type)
        {
            case DelayType::PanLR:
                lpfK8_ = 103;
                gL1Target_ = 0; gL2Target_ = kQ23Max;
                gR1Target_ = kQ23Max; gR2Target_ = 0;
                break;
            case DelayType::PanRL:
                lpfK8_ = 103;
                gL1Target_ = kQ23Max; gL2Target_ = 0;
                gR1Target_ = 0; gR2Target_ = kQ23Max;
                break;
            case DelayType::PanShort:
                lpfK8_ = 107;
                gL1Target_ = kQ23Max; gL2Target_ = 0;
                gR1Target_ = 0; gR2Target_ = kQ23Max;
                break;
            case DelayType::MonoShort:
                lpfK8_ = 107;
                gL1Target_ = kQ23Max; gL2Target_ = 0;
                gR1Target_ = kQ23Max; gR2Target_ = 0;
                break;
            case DelayType::MonoLong:
                lpfK8_ = 96;
                gL1Target_ = kQ23Max; gL2Target_ = 0;
                gR1Target_ = kQ23Max; gR2Target_ = 0;
                break;
        }
        routeMoving_ = gL1_ != gL1Target_ || gL2_ != gL2Target_
                       || gR1_ != gR1Target_ || gR2_ != gR2Target_;
        lpfOneMinusK8_ = 128 - lpfK8_;
    }

    void snap() noexcept
    {
        d1Q16_ = d1TargetQ16_;
        fb_ = fbTarget_;
        lvl_ = lvlTarget_;
        fbSplit_ = splitCoeff15 (fb_);
        lvlSplit_ = splitCoeff15 (lvl_);
        gL1_ = gL1Target_; gL2_ = gL2Target_;
        gR1_ = gR1Target_; gR2_ = gR2Target_;
        updateRouteSplits();
        fbMoving_ = lvlMoving_ = routeMoving_ = false;
    }

    StereoQ23 processQ23 (Q23 x) noexcept
    {
        d1Q16_ = stepDelayPos (d1Q16_, d1TargetQ16_);
        if (fbMoving_)
        {
            fb_ = smoothStepQ23 (fb_, fbTarget_, 7);
            fbSplit_ = splitCoeff15 (fb_);
            fbMoving_ = fb_ != fbTarget_;
        }
        if (lvlMoving_)
        {
            lvl_ = smoothStepQ23 (lvl_, lvlTarget_, 8);
            lvlSplit_ = splitCoeff15 (lvl_);
            lvlMoving_ = lvl_ != lvlTarget_;
        }
        if (routeMoving_)
            stepRouteGains();

        const Q23 tap1 = readQ23 (d1Q16_);
        const Q23 tap2 = readQ23 (d1Q16_ >> 1);

        const Q23 sIn = sat24 (mul8 (x, 96, 7) + mulSplit15 (tap1, fbSplit_.hi, fbSplit_.lo));
        lp_ = sat24 (mul8 (sIn, lpfOneMinusK8_, 7) + mul8 (lp_, lpfK8_, 7));
        const Q23 hp = sat24 (mulImmediate15 (lp_, 127, 96, 7) + dc_);
        dc_ = sat24 (dc_ + mulImmediate15 (hp, -1, 64, 7));
        buf_[static_cast<std::size_t> (write_)] = packDelay16 (hp);
        write_ = (write_ + 1) & mask;

        const Q23 w1 = mulSplit15 (tap1, lvlSplit_.hi, lvlSplit_.lo);
        const Q23 w2 = mulSplit15 (tap2, lvlSplit_.hi, lvlSplit_.lo);
        if (routeMoving_)
            return { sat24 (mulSplit15 (w1, gL1Split_.hi, gL1Split_.lo) + mulSplit15 (w2, gL2Split_.hi, gL2Split_.lo)),
                     sat24 (mulSplit15 (w1, gR1Split_.hi, gR1Split_.lo) + mulSplit15 (w2, gR2Split_.hi, gR2Split_.lo)) };
        switch (type_)
        {
            case DelayType::PanLR:
                return { w2, w1 };
            case DelayType::PanRL:
            case DelayType::PanShort:
                return { w1, w2 };
            case DelayType::MonoShort:
            case DelayType::MonoLong:
                break;
        }
        return { w1, w1 };
    }

    StereoQ23 processMixQ23 (Q23 x) noexcept
    {
        const StereoQ23 wet = processQ23 (x);
        const Q23 dry = mul8 (x, 112, 7);
        return { sat24 (dry + wet.l), sat24 (dry + wet.r) };
    }

    void processBlockMixQ23 (const Q23* in, Q23* outL, Q23* outR, int n) noexcept
    {
        processBlockMixCore<false> (in, in, outL, outR, n);
    }

    void processBlockMixStereoQ23 (const Q23* inL, const Q23* inR, Q23* outL, Q23* outR, int n) noexcept
    {
        processBlockMixCore<true> (inL, inR, outL, outR, n);
    }

    Stereo process (double x) noexcept
    {
        const StereoQ23 y = processQ23 (toQ23 (x));
        return { fromQ23 (y.l), fromQ23 (y.r) };
    }

    Stereo processMix (double x) noexcept
    {
        const StereoQ23 y = processMixQ23 (toQ23 (x));
        return { fromQ23 (y.l), fromQ23 (y.r) };
    }

private:
    template <bool kStereo>
    void processBlockMixCore (const Q23* inL, const Q23* inR, Q23* outL, Q23* outR, int n) noexcept
    {
        if (buf_.empty())
        {
            for (int i = 0; i < n; ++i)
            {
                const Q23 dryL = mul8 (inL[i], 112, 7);
                outL[i] = dryL;
                outR[i] = kStereo ? mul8 (inR[i], 112, 7) : dryL;
            }
            return;
        }
        int i = 0;
        while (i < n && (d1Q16_ != d1TargetQ16_ || fbMoving_ || lvlMoving_ || routeMoving_))
        {
            const Q23 x = kStereo ? static_cast<Q23> ((static_cast<std::int64_t> (inL[i]) + inR[i]) >> 1) : inL[i];
            const StereoQ23 wet = processQ23 (x);
            const Q23 dryL = mul8 (inL[i], 112, 7);
            const Q23 dryR = kStereo ? mul8 (inR[i], 112, 7) : dryL;
            outL[i] = sat24 (dryL + wet.l);
            outR[i] = sat24 (dryR + wet.r);
            ++i;
        }
        if (i >= n)
            return;

        std::uint16_t* const bufPtr = buf_.data();
        int write = write_;
        const std::int64_t d1Q16 = d1Q16_;
        const Coeff15 fbSplit = fbSplit_, lvlSplit = lvlSplit_;
        Q23 lp = lp_, dc = dc_;
        const std::int32_t lpfK8 = lpfK8_, lpfOmK8 = lpfOneMinusK8_;
        const int route = type_ == DelayType::PanLR ? 0
                        : (type_ == DelayType::PanRL || type_ == DelayType::PanShort) ? 1 : 2;
        const std::int32_t whole1 = static_cast<std::int32_t> (d1Q16 >> 16);
        const std::int32_t frac1 = static_cast<std::int32_t> (d1Q16 & 0xFFFF);
        const std::int64_t d2Q16 = d1Q16 >> 1;
        const std::int32_t whole2 = static_cast<std::int32_t> (d2Q16 >> 16);
        const std::int32_t frac2 = static_cast<std::int32_t> (d2Q16 & 0xFFFF);

        auto runSettled = [&] (auto routeTag) noexcept
        {
            constexpr int r = decltype (routeTag)::value;
            for (; i < n; ++i)
            {
                const Q23 tap1 = readFixedQ23At (bufPtr, write, whole1, frac1);
                const Q23 tap2 = readFixedQ23At (bufPtr, write, whole2, frac2);
                const Q23 dl = inL[i];
                const Q23 dr = kStereo ? inR[i] : dl;
                const Q23 x = kStereo ? static_cast<Q23> ((static_cast<std::int64_t> (dl) + dr) >> 1) : dl;
                const Q23 sIn = sat24 (mul8 (x, 96, 7) + mulSplit15 (tap1, fbSplit.hi, fbSplit.lo));
                lp = sat24 (mul8 (sIn, lpfOmK8, 7) + mul8 (lp, lpfK8, 7));
                const Q23 hp = sat24 (mulImmediate15 (lp, 127, 96, 7) + dc);
                dc = sat24 (dc + mulImmediate15 (hp, -1, 64, 7));
                bufPtr[static_cast<std::size_t> (write)] = packDelay16 (hp);
                write = (write + 1) & mask;

                const Q23 w1 = mulSplit15 (tap1, lvlSplit.hi, lvlSplit.lo);
                const Q23 w2 = mulSplit15 (tap2, lvlSplit.hi, lvlSplit.lo);
                const Q23 dryL = mul8 (dl, 112, 7);
                const Q23 dryR = kStereo ? mul8 (dr, 112, 7) : dryL;
                if (r == 0)
                {
                    outL[i] = sat24 (dryL + w2);
                    outR[i] = sat24 (dryR + w1);
                }
                else if (r == 1)
                {
                    outL[i] = sat24 (dryL + w1);
                    outR[i] = sat24 (dryR + w2);
                }
                else
                {
                    outL[i] = sat24 (dryL + w1);
                    outR[i] = sat24 (dryR + w1);
                }
            }
        };
        if (route == 0)
            runSettled (std::integral_constant<int, 0> {});
        else if (route == 1)
            runSettled (std::integral_constant<int, 1> {});
        else
            runSettled (std::integral_constant<int, 2> {});

        write_ = write;
        lp_ = lp;
        dc_ = dc;
    }

    static TRANCERA_FORCE_INLINE Q23 readFixedQ23At (const std::uint16_t* buf, int write, std::int32_t whole, std::int32_t frac) noexcept
    {
        const std::int32_t idx = (write - whole) & mask;
        const Q23 a = unpackDelay16 (buf[static_cast<std::size_t> (idx)]);
        const Q23 b = unpackDelay16 (buf[static_cast<std::size_t> ((idx - 1) & mask)]);
        return static_cast<Q23> (a + ((static_cast<std::int64_t> (b - a) * frac) >> 16));
    }

    static TRANCERA_FORCE_INLINE Q23 readQ23At (const std::uint16_t* buf, int write, std::int64_t delayQ16) noexcept
    {
        return readFixedQ23At (buf, write,
                               static_cast<std::int32_t> (delayQ16 >> 16),
                               static_cast<std::int32_t> (delayQ16 & 0xFFFF));
    }

    Q23 readQ23 (std::int64_t delayQ16) const noexcept
    {
        if (buf_.empty())
            return 0;
        return readQ23At (buf_.data(), write_, delayQ16);
    }

    static constexpr std::int64_t stepDelayPos (std::int64_t cur, std::int64_t target) noexcept
    {
        const std::int64_t diff = target - cur;
        if (diff == 0)
            return cur;
        constexpr std::int64_t kMaxDelayStepQ16 = 32768; // 0.5 samples/sample max slew
        std::int64_t step = diff >> 12;
        if (step == 0)
            step = diff > 0 ? 1 : -1;
        else if (step > kMaxDelayStepQ16)
            step = kMaxDelayStepQ16;
        else if (step < -kMaxDelayStepQ16)
            step = -kMaxDelayStepQ16;
        return cur + step;
    }

    void updateRouteSplits() noexcept
    {
        gL1Split_ = splitCoeff15 (gL1_);
        gL2Split_ = splitCoeff15 (gL2_);
        gR1Split_ = splitCoeff15 (gR1_);
        gR2Split_ = splitCoeff15 (gR2_);
    }

    void stepRouteGains() noexcept
    {
        gL1_ = smoothStepQ23 (gL1_, gL1Target_, 7);
        gL2_ = smoothStepQ23 (gL2_, gL2Target_, 7);
        gR1_ = smoothStepQ23 (gR1_, gR1Target_, 7);
        gR2_ = smoothStepQ23 (gR2_, gR2Target_, 7);
        updateRouteSplits();
        routeMoving_ = gL1_ != gL1Target_ || gL2_ != gL2Target_
                       || gR1_ != gR1Target_ || gR2_ != gR2Target_;
    }

    static constexpr int size = 1 << 18, mask = size - 1;
    std::vector<std::uint16_t> buf_;
    int write_ { 0 };
    DelayType type_ { DelayType::PanLR };
    std::int64_t d1Q16_ { 17640LL << 16 }, d1TargetQ16_ { 17640LL << 16 };
    Q23 fb_ { 0 }, fbTarget_ { 0 };
    Q23 lvl_ { 0 }, lvlTarget_ { 0 };
    Coeff15 fbSplit_ {}, lvlSplit_ {};
    Q23 gL1_ { 0 }, gL1Target_ { 0 }, gL2_ { kQ23Max }, gL2Target_ { kQ23Max };
    Q23 gR1_ { kQ23Max }, gR1Target_ { kQ23Max }, gR2_ { 0 }, gR2Target_ { 0 };
    Coeff15 gL1Split_ {}, gL2Split_ { splitCoeff15 (kQ23Max) };
    Coeff15 gR1Split_ { splitCoeff15 (kQ23Max) }, gR2Split_ {};
    std::int32_t lpfK8_ { 103 }, lpfOneMinusK8_ { 25 };
    Q23 lp_ { 0 }, dc_ { 0 };
    bool fbMoving_ { false }, lvlMoving_ { false }, routeMoving_ { false };
};

// Unified stereo dual-tap Multi-FX engine across all twelve chorus, flanger, and phaser presets.
class Chorus
{
public:
    void reset() noexcept
    {
        buf_.fill (0u);
        write_ = 0;
        phase_ = 0;
        lpIn_ = fbLp_ = dc_ = wetL_ = wetR_ = 0;
        distHpL_ = distHpR_ = distLpL_ = distLpR_ = distDcL_ = distDcR_ = 0.0;
        pendingType_ = type_;
        typeFade_ = 256;
        fadeState_ = FadeState::Idle;
        snap();
        fresh_ = true;
    }

    void configure (ChorusType type, int level) noexcept
    {
        lvlTarget_ = mapping::levelCurveQ23 (level);
        distLvlTarget_ = static_cast<Q23> ((static_cast<std::int64_t> (std::clamp (level, 0, 127)) * kQ23One) / 127);
        lvlMoving_ = lvl_ != lvlTarget_ || distLvl_ != distLvlTarget_;
        if (fresh_)
        {
            type_ = pendingType_ = type;
            phase_ = 0;
            lpIn_ = fbLp_ = dc_ = wetL_ = wetR_ = 0;
            distHpL_ = distHpR_ = distLpL_ = distLpR_ = distDcL_ = distDcR_ = 0.0;
            typeFade_ = 256;
            fadeState_ = FadeState::Idle;
            fresh_ = false;
        }
        else if (type != pendingType_ || (fadeState_ == FadeState::Idle && type != type_))
        {
            if (lvl_ == 0 && lvlTarget_ == 0 && distLvl_ == 0 && distLvlTarget_ == 0 && wetL_ == 0 && wetR_ == 0)
            {
                if (type_ == ChorusType::Distortion && type != ChorusType::Distortion)
                    buf_.fill (0u);
                type_ = pendingType_ = type;
                phase_ = 0;
                lpIn_ = fbLp_ = dc_ = 0;
                distHpL_ = distHpR_ = distLpL_ = distLpR_ = distDcL_ = distDcR_ = 0.0;
                typeFade_ = 256;
                fadeState_ = FadeState::Idle;
            }
            else if (fadeState_ == FadeState::FadeOut && type == type_)
            {
                pendingType_ = type;
                fadeState_ = FadeState::FadeIn;
            }
            else
            {
                pendingType_ = type;
                fadeState_ = FadeState::FadeOut;
            }
        }
    }

    void snap() noexcept
    {
        if (type_ != pendingType_ && (type_ == ChorusType::Distortion || pendingType_ == ChorusType::Distortion))
            buf_.fill (0u);
        type_ = pendingType_;
        typeFade_ = 256;
        fadeState_ = FadeState::Idle;
        lvl_ = lvlTarget_;
        distLvl_ = distLvlTarget_;
        lvlSplit_ = splitCoeff15 (lvl_);
        lvlMoving_ = false;
        fresh_ = false;
    }

    StereoQ23 processMixQ23 (StereoQ23 dry) noexcept
    {
        if (lvlMoving_)
        {
            lvl_ = smoothStepQ23 (lvl_, lvlTarget_, 7);
            distLvl_ = smoothStepQ23 (distLvl_, distLvlTarget_, 7);
            lvlSplit_ = splitCoeff15 (lvl_);
            lvlMoving_ = lvl_ != lvlTarget_ || distLvl_ != distLvlTarget_;
        }
        if (fadeState_ == FadeState::FadeOut)
        {
            if (typeFade_ > 0)
                --typeFade_;
            if (typeFade_ <= 0)
            {
                if (type_ != pendingType_ && (type_ == ChorusType::Distortion || pendingType_ == ChorusType::Distortion))
                    buf_.fill (0u);
                type_ = pendingType_;
                phase_ = 0;
                lpIn_ = fbLp_ = dc_ = wetL_ = wetR_ = 0;
                distHpL_ = distHpR_ = distLpL_ = distLpR_ = distDcL_ = distDcR_ = 0.0;
                typeFade_ = 0;
                fadeState_ = FadeState::FadeIn;
            }
        }
        else if (fadeState_ == FadeState::FadeIn)
        {
            if (typeFade_ < 256)
                ++typeFade_;
            if (typeFade_ >= 256)
            {
                typeFade_ = 256;
                fadeState_ = FadeState::Idle;
            }
        }

        if (type_ == ChorusType::Distortion)
        {
            const Q23 dL = processDistortionChannel (dry.l, distHpL_, distLpL_, distDcL_);
            const Q23 dR = processDistortionChannel (dry.r, distHpR_, distLpR_, distDcR_);
            const Q23 wL = fadeState_ == FadeState::Idle
                               ? dL
                               : static_cast<Q23> ((static_cast<std::int64_t> (dL) * typeFade_) >> 8);
            const Q23 wR = fadeState_ == FadeState::Idle
                               ? dR
                               : static_cast<Q23> ((static_cast<std::int64_t> (dR) * typeFade_) >> 8);
            return { sat24 (mul8 (dry.l, 116, 7) + mul8 (wL, 116, 7)),
                     sat24 (mul8 (dry.r, 116, 7) + mul8 (wR, 116, 7)) };
        }

        const PresetQ23& p = preset (type_);
        phase_ = wrap24 (phase_ + p.phaseStep);
        const std::int32_t absP = std::abs (phase_);
        const std::int32_t invP = kQ23One - absP;

        const std::int32_t dLQ16 = std::max<std::int32_t> (65536, static_cast<std::int32_t> (((static_cast<std::int64_t> (absP) * p.modQ16) >> 23) + p.baseQ16));
        const std::int32_t dRQ16 = std::max<std::int32_t> (65536, static_cast<std::int32_t> (((static_cast<std::int64_t> (invP) * p.modQ16) >> 23) + p.baseQ16 + p.dRQ16));
        const Q23 tapLRaw = readQ23 (dLQ16);
        const Q23 tapRRaw = readQ23 (dRQ16);

        const Q23 m = (dry.l >> 1) + (dry.r >> 1);
        lpIn_ = sat24 (mulSplit15 (m, p.oneMinusInFlt.hi, p.oneMinusInFlt.lo)
                       + mulSplit15 (lpIn_, p.inFlt.hi, p.inFlt.lo));
        const Q23 hpIn = sat24 (m - lpIn_);

        const Q23 fbIn = mulSplit15 (tapRRaw, p.fb.hi, p.fb.lo);
        fbLp_ = sat24 (mulSplit15 (fbIn, p.oneMinusFbFlt.hi, p.oneMinusFbFlt.lo)
                       + mulSplit15 (fbLp_, p.fbFlt.hi, p.fbFlt.lo));

        const Q23 toDelay = sat24 (mulImmediate15 (sat24 (hpIn + fbLp_), 127, 96, 7) + dc_);
        dc_ = sat24 (dc_ + mulImmediate15 (toDelay, -1, 64, 7));
        buf_[static_cast<std::size_t> (write_)] = packDelay16 (toDelay);
        write_ = (write_ + 1) & mask;

        const Q23 tapL = mulSplit15 (tapLRaw, lvlSplit_.hi, lvlSplit_.lo);
        const Q23 tapR = mulSplit15 (tapRRaw, lvlSplit_.hi, lvlSplit_.lo);
        const Q23 combL = sat24 (tapL + mul8 (tapR, -90, 7));
        const Q23 combR = sat24 (tapR + mul8 (tapL, -90, 7));
        wetL_ = sat24 (mulSplit15 (combL, p.oneMinusOutFlt.hi, p.oneMinusOutFlt.lo)
                       + mulSplit15 (wetL_, p.outFlt.hi, p.outFlt.lo));
        wetR_ = sat24 (mulSplit15 (combR, p.oneMinusOutFlt.hi, p.oneMinusOutFlt.lo)
                       + mulSplit15 (wetR_, p.outFlt.hi, p.outFlt.lo));

        const Q23 wL = fadeState_ == FadeState::Idle
                           ? wetL_
                           : static_cast<Q23> ((static_cast<std::int64_t> (wetL_) * typeFade_) >> 8);
        const Q23 wR = fadeState_ == FadeState::Idle
                           ? wetR_
                           : static_cast<Q23> ((static_cast<std::int64_t> (wetR_) * typeFade_) >> 8);
        return { sat24 (mul8 (dry.l, 116, 7) + mul8 (wL, 116, 7)),
                 sat24 (mul8 (dry.r, 116, 7) + mul8 (wR, 116, 7)) };
    }

    void advanceIdlePhase (int n) noexcept
    {
        if (type_ == ChorusType::Distortion)
            return;
        const PresetQ23& p = preset (type_);
        phase_ = wrap24 (static_cast<std::int32_t> (static_cast<std::int64_t> (phase_)
                                                  + static_cast<std::int64_t> (n) * p.phaseStep));
    }

    void processBlockMixQ23 (Q23* bufL, Q23* bufR, int n) noexcept
    {
        int i = 0;
        while (i < n && (lvlMoving_ || fadeState_ != FadeState::Idle))
        {
            const StereoQ23 mixed = processMixQ23 ({ bufL[i], bufR[i] });
            bufL[i] = mixed.l;
            bufR[i] = mixed.r;
            ++i;
        }
        if (i >= n)
            return;

        if (type_ == ChorusType::Distortion)
        {
            double hpL = distHpL_, hpR = distHpR_;
            double lpL = distLpL_, lpR = distLpR_;
            double dcL = distDcL_, dcR = distDcR_;
            for (; i < n; ++i)
            {
                const Q23 dryL = bufL[i], dryR = bufR[i];
                const Q23 wL = processDistortionChannel (dryL, hpL, lpL, dcL);
                const Q23 wR = processDistortionChannel (dryR, hpR, lpR, dcR);
                bufL[i] = sat24 (mul8 (dryL, 116, 7) + mul8 (wL, 116, 7));
                bufR[i] = sat24 (mul8 (dryR, 116, 7) + mul8 (wR, 116, 7));
            }
            distHpL_ = hpL; distHpR_ = hpR;
            distLpL_ = lpL; distLpR_ = lpR;
            distDcL_ = dcL; distDcR_ = dcR;
            return;
        }

        const PresetQ23& p = preset (type_);
        int write = write_;
        Q23 phase = phase_, lpIn = lpIn_, fbLp = fbLp_, dc = dc_, wetL = wetL_, wetR = wetR_;
        const Coeff15 lvlSplit = lvlSplit_;
        std::uint16_t* const bufPtr = buf_.data();

        for (; i < n; ++i)
        {
            phase = wrap24 (phase + p.phaseStep);
            const std::int32_t absP = phase < 0 ? -phase : phase;
            const std::int32_t invP = kQ23One - absP;

            const std::int32_t dLQ16 = std::max<std::int32_t> (65536, static_cast<std::int32_t> (((static_cast<std::int64_t> (absP) * p.modQ16) >> 23) + p.baseQ16));
            const std::int32_t dRQ16 = std::max<std::int32_t> (65536, static_cast<std::int32_t> (((static_cast<std::int64_t> (invP) * p.modQ16) >> 23) + p.baseQ16 + p.dRQ16));
            const Q23 tapLRaw = readQ23At (bufPtr, write, dLQ16);
            const Q23 tapRRaw = readQ23At (bufPtr, write, dRQ16);

            const Q23 dL = bufL[i], dR = bufR[i];
            const Q23 m = (dL >> 1) + (dR >> 1);
            lpIn = sat24 (mulSplit15 (m, p.oneMinusInFlt.hi, p.oneMinusInFlt.lo)
                          + mulSplit15 (lpIn, p.inFlt.hi, p.inFlt.lo));
            const Q23 hpIn = sat24 (m - lpIn);

            const Q23 fbIn = mulSplit15 (tapRRaw, p.fb.hi, p.fb.lo);
            fbLp = sat24 (mulSplit15 (fbIn, p.oneMinusFbFlt.hi, p.oneMinusFbFlt.lo)
                          + mulSplit15 (fbLp, p.fbFlt.hi, p.fbFlt.lo));

            const Q23 toDelay = sat24 (mulImmediate15 (sat24 (hpIn + fbLp), 127, 96, 7) + dc);
            dc = sat24 (dc + mulImmediate15 (toDelay, -1, 64, 7));
            bufPtr[static_cast<std::size_t> (write)] = packDelay16 (toDelay);
            write = (write + 1) & mask;

            const Q23 tapL = mulSplit15 (tapLRaw, lvlSplit.hi, lvlSplit.lo);
            const Q23 tapR = mulSplit15 (tapRRaw, lvlSplit.hi, lvlSplit.lo);
            const Q23 combL = sat24 (tapL + mul8 (tapR, -90, 7));
            const Q23 combR = sat24 (tapR + mul8 (tapL, -90, 7));
            wetL = sat24 (mulSplit15 (combL, p.oneMinusOutFlt.hi, p.oneMinusOutFlt.lo)
                          + mulSplit15 (wetL, p.outFlt.hi, p.outFlt.lo));
            wetR = sat24 (mulSplit15 (combR, p.oneMinusOutFlt.hi, p.oneMinusOutFlt.lo)
                          + mulSplit15 (wetR, p.outFlt.hi, p.outFlt.lo));

            bufL[i] = sat24 (mul8 (dL, 116, 7) + mul8 (wetL, 116, 7));
            bufR[i] = sat24 (mul8 (dR, 116, 7) + mul8 (wetR, 116, 7));
        }

        write_ = write; phase_ = phase;
        lpIn_ = lpIn; fbLp_ = fbLp; dc_ = dc; wetL_ = wetL; wetR_ = wetR;
    }

    Stereo process (double monoDry) noexcept { return processMix ({ monoDry, monoDry }); }

    Stereo processMix (Stereo dry) noexcept
    {
        const StereoQ23 y = processMixQ23 ({ toQ23 (dry.l), toQ23 (dry.r) });
        return { fromQ23 (y.l), fromQ23 (y.r) };
    }

private:
    struct PresetQ23
    {
        Coeff15 inFlt, oneMinusInFlt;
        Coeff15 fbFlt, oneMinusFbFlt;
        Coeff15 fb;
        Coeff15 outFlt, oneMinusOutFlt;
        std::int32_t baseQ16, modQ16, dRQ16;
        Q23 phaseStep;
    };

    static constexpr PresetQ23 makePreset (int inFlt14, int fbFlt14, int fb14,
                                           int baseSmp, int modSmp, int dRSmp,
                                           Q23 phaseStep, int outFlt14) noexcept
    {
        return {
            splitCoeff15 (inFlt14 * 512), splitCoeff15 ((16384 - inFlt14) * 512),
            splitCoeff15 (fbFlt14 * 512), splitCoeff15 ((16384 - fbFlt14) * 512),
            splitCoeff15 (fb14 * 512),
            splitCoeff15 (outFlt14 * 512), splitCoeff15 ((16384 - outFlt14) * 512),
            baseSmp << 16, modSmp << 16, dRSmp << 16,
            phaseStep
        };
    }

    static const PresetQ23& preset (ChorusType type) noexcept
    {
        static constexpr std::array<PresetQ23, 12> table { {
            makePreset (16271,    0,      0, 1152, 1536,  2,  12,  8576),
            makePreset (16271,    0,      0,  512,  232,  2,  80,  8320),
            makePreset (15951,    0,      0, 1280,   44,  2, 768, 10175),
            makePreset (15360, 8704,  -5376, 1536, 1280,  2,  16,  4096),
            makePreset (15871, 8448,  12800,  128,  100,  0,  60,  4096),
            makePreset (16383, 8448,  13056,   64,  224,  0,  68,  4096),
            makePreset (15615, 8704,  11776,  128,   60,  0, 320,  6144),
            makePreset (16383, 8832, -15652,    0,   20,  1,  16,  4096),
            makePreset (15967, 8832, -11264,  224, 1280,  1,  16,  4096),
            makePreset (16383, 8832,  -9728, 1536,   40,  1,  32,  4096),
            makePreset (14352, 8896,  -6144, 1024,   10, 80,   0,  8192),
            makePreset (14352, 8896,  -3072, 3072,   10, 80,   0,  8192),
        } };
        const auto idx = std::min<std::size_t> (static_cast<std::size_t> (type), table.size() - 1);
        return table[idx];
    }

    TRANCERA_FORCE_INLINE Q23 processDistortionChannel (Q23 xQ23, double& hp, double& lp, double& dc) const noexcept
    {
        if (distLvl_ <= 0)
        {
            hp = lp = dc = 0.0;
            return 0;
        }
        const double x = fromQ23 (xQ23) * 12.0;
        hp += 0.01975 * (x - hp);
        if (fastAbs (hp) < 1e-24)
            hp = 0.0;
        const double shaped = (x - hp) + 0.25 * hp;

        const double u = static_cast<double> (distLvl_) * (1.0 / static_cast<double> (kQ23One));
        const double mix = u < 0.2 ? (u * 5.0) : 1.0;
        const double drive = 1.0 + u * (2.5 + 11.5 * u);
        const double w = shaped * drive;
        const double driven = (w >= 0.0) ? (w * 1.12) : (w * 0.88);
        const double bias = 0.14 * u;
        const double c0 = softClip (bias);
        const double sat = softClip (driven + bias) - c0;

        const double makeup = 0.80 - 0.20 * u;
        lp += 0.54325 * (sat * makeup - lp);
        if (fastAbs (lp) < 1e-24)
            lp = 0.0;
        dc += 0.001708 * (lp - dc);
        if (fastAbs (dc) < 1e-24)
            dc = 0.0;

        const double wetCh = (lp - dc) * (1.0 / 12.0);
        const double dryCh = fromQ23 (xQ23);
        const double delta = mix * (wetCh - dryCh);
        const double scaled = std::clamp (delta, -1.0, 1.0) * static_cast<double> (kQ23Max);
        return sat24 (static_cast<std::int32_t> (scaled + (scaled >= 0.0 ? 0.5 : -0.5)));
    }

    static TRANCERA_FORCE_INLINE Q23 readQ23At (const std::uint16_t* buf, int write, std::int32_t delayQ16) noexcept
    {
        const std::int32_t whole = delayQ16 >> 16;
        const std::int32_t frac = delayQ16 & 0xFFFF;
        const std::int32_t idx = (write - whole) & mask;
        const Q23 a = unpackDelay16 (buf[static_cast<std::size_t> (idx)]);
        const Q23 b = unpackDelay16 (buf[static_cast<std::size_t> ((idx - 1) & mask)]);
        return static_cast<Q23> (a + ((static_cast<std::int64_t> (b - a) * frac) >> 16));
    }

    Q23 readQ23 (std::int32_t delayQ16) const noexcept
    {
        return readQ23At (buf_.data(), write_, delayQ16);
    }

    enum class FadeState : std::uint8_t { Idle, FadeOut, FadeIn };

    static constexpr int size = 8192, mask = size - 1;
    std::array<std::uint16_t, size> buf_ {};
    int write_ { 0 };
    ChorusType type_ { ChorusType::SuperSlow }, pendingType_ { ChorusType::SuperSlow };
    int typeFade_ { 256 };
    FadeState fadeState_ { FadeState::Idle };
    Q23 lvl_ { 0 }, lvlTarget_ { 0 }, distLvl_ { 0 }, distLvlTarget_ { 0 }, phase_ { 0 };
    Coeff15 lvlSplit_ {};
    Q23 lpIn_ { 0 }, fbLp_ { 0 }, dc_ { 0 }, wetL_ { 0 }, wetR_ { 0 };
    double distHpL_ { 0.0 }, distHpR_ { 0.0 }, distLpL_ { 0.0 }, distLpR_ { 0.0 }, distDcL_ { 0.0 }, distDcR_ { 0.0 };
    bool lvlMoving_ { false }, fresh_ { true };
};
} // namespace trancera
