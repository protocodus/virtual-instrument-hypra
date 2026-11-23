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
    void reset() noexcept { xB1_ = yB1_ = xT1_ = yT1_ = 0.0; }

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
    }

    // Internal hardware gain staging with rail clipping and -12 dB flat headroom.
    double processSaturating (double x) noexcept
    {
        const double yB = sat (x - bB_ * xB1_ + aB_ * yB1_);
        xB1_ = x;
        yB1_ = yB;
        const double yBout = gCut_ * yB;
        const double yT = sat (yBout - bT_ * xT1_ + aT_ * yT1_);
        xT1_ = yBout;
        yT1_ = yT;
        return gBoost_ * yT;
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
};

class Delay
{
public:
    void prepare()
    {
        buf_.assign (size, 0.0f);
        write_ = 0;
        reset();
    }

    void reset() noexcept
    {
        std::fill (buf_.begin(), buf_.end(), 0.0f);
        write_ = 0;
        lp_ = dc_ = 0.0;
        snap();
    }

    void configure (DelayType type, int time, int feedback, int level) noexcept
    {
        type_ = type;
        d1Target_ = std::clamp (mapping::delaySamples (type, time), 2.0, static_cast<double> (size - 3));
        fbTarget_ = mapping::delayFeedback (feedback);
        lvlTarget_ = mapping::effectLevel (level);
        switch (type)
        {
            case DelayType::PanLR:
            case DelayType::PanRL:
                lpfK_ = 103.0 / 128.0;
                break;
            case DelayType::PanShort:
            case DelayType::MonoShort:
                lpfK_ = 107.0 / 128.0;
                break;
            case DelayType::MonoLong:
                lpfK_ = 96.0 / 128.0;
                break;
        }
    }

    void snap() noexcept
    {
        d1_ = d1Target_;
        fb_ = fbTarget_;
        lvl_ = lvlTarget_;
    }

    Stereo process (double x) noexcept
    {
        d1_ += 0.0002 * (d1Target_ - d1_);
        if (std::abs (d1Target_ - d1_) < 1.0e-4)
            d1_ = d1Target_;
        fb_ += (1.0 / 128.0) * (fbTarget_ - fb_);
        lvl_ += (1.0 / 256.0) * (lvlTarget_ - lvl_);

        const double d2 = 0.5 * d1_;
        const double tap1 = read (d1_);
        const double tap2 = read (d2);

        const double sIn = sat (0.75 * x + fb_ * tap1);
        lp_ = sat ((1.0 - lpfK_) * sIn + lpfK_ * lp_);
        const double hp = sat (0.998046875 * lp_ + dc_);
        dc_ = sat (dc_ - 0.00390625 * hp);
        buf_[static_cast<std::size_t> (write_)] = static_cast<float> (hp);
        write_ = (write_ + 1) & mask;

        const double w1 = lvl_ * tap1;
        const double w2 = lvl_ * tap2;
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

    Stereo processMix (double x) noexcept
    {
        const Stereo wet = process (x);
        return { sat (0.875 * x + wet.l), sat (0.875 * x + wet.r) };
    }

private:
    double read (double delaySamples) const noexcept
    {
        if (buf_.empty())
            return 0.0;
        const double position = static_cast<double> (write_) - delaySamples + size;
        const int index = static_cast<int> (position);
        const double fraction = position - index;
        const double a = buf_[static_cast<std::size_t> (index & mask)];
        const double b = buf_[static_cast<std::size_t> ((index + 1) & mask)];
        return a + fraction * (b - a);
    }

    static constexpr int size = 1 << 17, mask = size - 1;
    std::vector<float> buf_;
    int write_ { 0 };
    DelayType type_ { DelayType::PanLR };
    double d1_ { 17640.0 }, d1Target_ { 17640.0 };
    double fb_ { 0.0 }, fbTarget_ { 0.0 };
    double lvl_ { 0.0 }, lvlTarget_ { 0.0 };
    double lpfK_ { 103.0 / 128.0 }, lp_ { 0.0 }, dc_ { 0.0 };
};

// Unified stereo dual-tap Multi-FX engine across all twelve chorus, flanger, and phaser presets.
class Chorus
{
public:
    void reset() noexcept
    {
        buf_.fill (0.0f);
        write_ = 0;
        phase_ = 0.0;
        lpIn_ = fbLp_ = dc_ = wetL_ = wetR_ = 0.0;
        snap();
    }

    void configure (ChorusType type, int level) noexcept
    {
        if (type != type_)
        {
            type_ = type;
            phase_ = 0.0;
            lpIn_ = fbLp_ = dc_ = wetL_ = wetR_ = 0.0;
        }
        lvlTarget_ = mapping::effectLevel (level);
    }

    void snap() noexcept { lvl_ = lvlTarget_; }

    Stereo process (double monoDry) noexcept { return processMix ({ monoDry, monoDry }); }

    Stereo processMix (Stereo dry) noexcept
    {
        const Preset& p = presets[static_cast<std::size_t> (type_)];
        lvl_ += (1.0 / 128.0) * (lvlTarget_ - lvl_);

        phase_ = wrap2 (phase_ + 2.0 * p.rateHz / kEngineRate);
        const double triL = std::abs (phase_) - 0.5;
        const double triR = -triL;

        const double dL = std::max (1.0, (triL + 0.5) * p.modSmp + p.baseSmp);
        const double dR = std::max (1.0, (triR + 0.5) * p.modSmp + p.baseSmp + p.dRSmp);
        const double tapLRaw = read (dL);
        const double tapRRaw = read (dR);

        const double m = 0.5 * (dry.l + dry.r);
        lpIn_ = (1.0 - p.inFlt) * m + p.inFlt * lpIn_;
        const double hpIn = m - lpIn_;

        const double fbIn = p.fb * tapRRaw;
        fbLp_ = (1.0 - p.fbFlt) * fbIn + p.fbFlt * fbLp_;

        const double toDelay = sat (0.998046875 * (hpIn + fbLp_) + dc_);
        dc_ = sat (dc_ - 0.00390625 * toDelay);
        buf_[static_cast<std::size_t> (write_)] = static_cast<float> (toDelay);
        write_ = (write_ + 1) & mask;

        const double tapL = lvl_ * tapLRaw;
        const double tapR = lvl_ * tapRRaw;
        const double combL = tapL - 0.703125 * tapR;
        const double combR = tapR - 0.703125 * tapL;
        wetL_ = (1.0 - p.outFlt) * combL + p.outFlt * wetL_;
        wetR_ = (1.0 - p.outFlt) * combR + p.outFlt * wetR_;

        return { 0.90625 * sat (dry.l + wetL_), 0.90625 * sat (dry.r + wetR_) };
    }

private:
    struct Preset
    {
        double inFlt, fbFlt, fb, baseSmp, modSmp, dRSmp, rateHz, outFlt;
    };

    static constexpr std::array<Preset, 12> presets { {
        { 0.993103, 0.000000,  0.00000, 1152.0, 1536.0,  2.0, 0.06309, 0.52344 },
        { 0.993103, 0.000000,  0.00000,  512.0,  232.0,  2.0, 0.42057, 0.50781 },
        { 0.973572, 0.000000,  0.00000, 1280.0,   44.0,  2.0, 4.03748, 0.62103 },
        { 0.937500, 0.531250, -0.32812, 1536.0, 1280.0,  2.0, 0.08411, 0.25000 },
        { 0.968689, 0.515625,  0.78125,  128.0,  100.0,  0.0, 0.31543, 0.25000 },
        { 0.999939, 0.515625,  0.79688,   64.0,  224.0,  0.0, 0.35748, 0.25000 },
        { 0.953064, 0.531250,  0.71875,  128.0,   60.0,  0.0, 1.68228, 0.37500 },
        { 0.999939, 0.539062, -0.95532,    0.0,   20.0,  1.0, 0.08411, 0.25000 },
        { 0.974548, 0.539062, -0.68750,  224.0, 1280.0,  1.0, 0.08411, 0.25000 },
        { 0.999939, 0.539062, -0.59375, 1536.0,   40.0,  1.0, 0.16823, 0.25000 },
        { 0.875977, 0.542969, -0.37500, 1024.0,   10.0, 80.0, 0.00000, 0.50000 },
        { 0.875977, 0.542969, -0.18750, 3072.0,   10.0, 80.0, 0.00000, 0.50000 },
    } };

    double read (double delaySamples) const noexcept
    {
        const double position = static_cast<double> (write_) - delaySamples + size;
        const int index = static_cast<int> (position);
        const double fraction = position - index;
        const double a = buf_[static_cast<std::size_t> (index & mask)];
        const double b = buf_[static_cast<std::size_t> ((index + 1) & mask)];
        return a + fraction * (b - a);
    }

    static constexpr int size = 8192, mask = size - 1;
    std::array<float, size> buf_ {};
    int write_ { 0 };
    ChorusType type_ { ChorusType::SuperSlow };
    double lvl_ { 0.0 }, lvlTarget_ { 0.0 }, phase_ { 0.0 };
    double lpIn_ { 0.0 }, fbLp_ { 0.0 }, dc_ { 0.0 }, wetL_ { 0.0 }, wetR_ { 0.0 };
};
} // namespace trancera
