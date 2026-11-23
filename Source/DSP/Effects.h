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

        negBBSplit_ = splitTable15 (-static_cast<std::int32_t> (mapping::toneBassB[bi]));
        aBSplit_ = splitTable15 (mapping::toneBassA[bi]);
        negBTSplit_ = splitTable15 (-static_cast<std::int32_t> (mapping::toneTrebleB[ti]));
        aTSplit_ = splitTable15 (mapping::toneTrebleA[ti]);
        gCutSplit_ = splitTable15 (mapping::toneTrebleCut[ti]);
        gBoostSplit_ = splitTable15 (mapping::toneTrebleBoost[ti]);
    }

    // Internal hardware gain staging with rail clipping and -12 dB flat headroom.
    Q23 processSaturatingQ23 (Q23 x) noexcept
    {
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
    double bB_ { 31743.0 / 32768.0 }, aB_ { 31743.0 / 32768.0 };
    double bT_ { 30589.0 / 32768.0 }, aT_ { 30589.0 / 32768.0 };
    double gCut_ { 1.0 }, gBoost_ { 0.25 };
    double xB1_ { 0.0 }, yB1_ { 0.0 }, xT1_ { 0.0 }, yT1_ { 0.0 };

    Coeff15 negBBSplit_ { splitTable15 (-31743) }, aBSplit_ { splitTable15 (31743) };
    Coeff15 negBTSplit_ { splitTable15 (-30589) }, aTSplit_ { splitTable15 (30589) };
    Coeff15 gCutSplit_ { splitTable15 (16384) }, gBoostSplit_ { splitTable15 (4096) };
    Q23 xB1Q_ { 0 }, yB1Q_ { 0 }, yBoutPrevQ_ { 0 }, xT1Q_ { 0 }, yT1Q_ { 0 };
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

    void configure (DelayType type, int time, int feedback, int level) noexcept
    {
        type_ = type;
        d1TargetQ16_ = static_cast<std::int64_t> (std::llround (std::clamp (mapping::delaySamples (type, time), 2.0, static_cast<double> (size - 3)) * 65536.0));
        fbTarget_ = mapping::levelCurveQ23 (feedback);
        lvlTarget_ = mapping::levelCurveQ23 (level);
        fbMoving_ = fb_ != fbTarget_;
        lvlMoving_ = lvl_ != lvlTarget_;
        switch (type)
        {
            case DelayType::PanLR:
            case DelayType::PanRL:
                lpfK8_ = 103;
                break;
            case DelayType::PanShort:
            case DelayType::MonoShort:
                lpfK8_ = 107;
                break;
            case DelayType::MonoLong:
                lpfK8_ = 96;
                break;
        }
        lpfOneMinusK8_ = 128 - lpfK8_;
    }

    void snap() noexcept
    {
        d1Q16_ = d1TargetQ16_;
        fb_ = fbTarget_;
        lvl_ = lvlTarget_;
        fbSplit_ = splitCoeff15 (fb_);
        lvlSplit_ = splitCoeff15 (lvl_);
        fbMoving_ = lvlMoving_ = false;
    }

    StereoQ23 processQ23 (Q23 x) noexcept
    {
        const std::int64_t dDiff = d1TargetQ16_ - d1Q16_;
        d1Q16_ = std::abs (dDiff) < 4096 ? d1TargetQ16_ : (d1Q16_ + (dDiff >> 12));
        if (fbMoving_)
        {
            const Q23 nextFb = sat24 (fb_ + ((fbTarget_ - fb_) >> 7));
            if (nextFb == fb_)
                fbMoving_ = false;
            else
            {
                fb_ = nextFb;
                fbSplit_ = splitCoeff15 (fb_);
            }
        }
        if (lvlMoving_)
        {
            const Q23 nextLvl = sat24 (lvl_ + ((lvlTarget_ - lvl_) >> 8));
            if (nextLvl == lvl_)
                lvlMoving_ = false;
            else
            {
                lvl_ = nextLvl;
                lvlSplit_ = splitCoeff15 (lvl_);
            }
        }

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
    Q23 readQ23 (std::int64_t delayQ16) const noexcept
    {
        if (buf_.empty())
            return 0;
        const std::int32_t whole = static_cast<std::int32_t> (delayQ16 >> 16);
        const std::int32_t frac = static_cast<std::int32_t> (delayQ16 & 0xFFFF);
        const std::int32_t idx = (write_ - whole) & mask;
        const Q23 a = unpackDelay16 (buf_[static_cast<std::size_t> (idx)]);
        const Q23 b = unpackDelay16 (buf_[static_cast<std::size_t> ((idx - 1) & mask)]);
        return static_cast<Q23> (a + ((static_cast<std::int64_t> (b - a) * frac) >> 16));
    }

    static constexpr int size = 1 << 17, mask = size - 1;
    std::vector<std::uint16_t> buf_;
    int write_ { 0 };
    DelayType type_ { DelayType::PanLR };
    std::int64_t d1Q16_ { 17640LL << 16 }, d1TargetQ16_ { 17640LL << 16 };
    Q23 fb_ { 0 }, fbTarget_ { 0 };
    Q23 lvl_ { 0 }, lvlTarget_ { 0 };
    Coeff15 fbSplit_ {}, lvlSplit_ {};
    std::int32_t lpfK8_ { 103 }, lpfOneMinusK8_ { 25 };
    Q23 lp_ { 0 }, dc_ { 0 };
    bool fbMoving_ { false }, lvlMoving_ { false };
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
        snap();
    }

    void configure (ChorusType type, int level) noexcept
    {
        if (type != type_)
        {
            type_ = type;
            phase_ = 0;
            lpIn_ = fbLp_ = dc_ = wetL_ = wetR_ = 0;
        }
        lvlTarget_ = mapping::levelCurveQ23 (level);
        lvlMoving_ = lvl_ != lvlTarget_;
    }

    void snap() noexcept
    {
        lvl_ = lvlTarget_;
        lvlSplit_ = splitCoeff15 (lvl_);
        lvlMoving_ = false;
    }

    StereoQ23 processMixQ23 (StereoQ23 dry) noexcept
    {
        const PresetQ23& p = preset (type_);
        if (lvlMoving_)
        {
            const Q23 nextLvl = sat24 (lvl_ + ((lvlTarget_ - lvl_) >> 7));
            if (nextLvl == lvl_)
                lvlMoving_ = false;
            else
            {
                lvl_ = nextLvl;
                lvlSplit_ = splitCoeff15 (lvl_);
            }
        }

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

        return { sat24 (mul8 (dry.l, 116, 7) + mul8 (wetL_, 116, 7)),
                 sat24 (mul8 (dry.r, 116, 7) + mul8 (wetR_, 116, 7)) };
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
        return table[static_cast<std::size_t> (type)];
    }

    Q23 readQ23 (std::int32_t delayQ16) const noexcept
    {
        const std::int32_t whole = delayQ16 >> 16;
        const std::int32_t frac = delayQ16 & 0xFFFF;
        const std::int32_t idx = (write_ - whole) & mask;
        const Q23 a = unpackDelay16 (buf_[static_cast<std::size_t> (idx)]);
        const Q23 b = unpackDelay16 (buf_[static_cast<std::size_t> ((idx - 1) & mask)]);
        return static_cast<Q23> (a + ((static_cast<std::int64_t> (b - a) * frac) >> 16));
    }

    static constexpr int size = 8192, mask = size - 1;
    std::array<std::uint16_t, size> buf_ {};
    int write_ { 0 };
    ChorusType type_ { ChorusType::SuperSlow };
    Q23 lvl_ { 0 }, lvlTarget_ { 0 }, phase_ { 0 };
    Coeff15 lvlSplit_ {};
    Q23 lpIn_ { 0 }, fbLp_ { 0 }, dc_ { 0 }, wetL_ { 0 }, wetR_ { 0 };
    bool lvlMoving_ { false };
};
} // namespace trancera
