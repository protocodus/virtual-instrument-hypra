// The analogue path from DAC to line jack, solved from the schematic. Left
// out: pot loading, mute transistors, ferrites, the 100R/100p RF filter.
//
// Roland JP-8000 Service Notes, January 1997 (70559865):
//   https://archive.org/details/roland_JP-8000_SERVICE_NOTES
//   p.23 DAC: IC20 uPD63200GS, 18/16SEL high (18 bit). C95 10u / R36 100k,
//        then IC22A: R39 5.6k, R38 47k || C107 100p, C109 10u / R42 100k.
//   p.27 panel B: volume VR10, R34 47k / R35 22k, C28 marked 10000p, IC6A
//        as a follower.
//   p.30 jack board: C34 10u / R48 100k, then IC4A as a Sallen-Key filter
//        R47 10k, R45 10k, C26 470p to the output, C30 680p to ground. Its
//        gain leg is R43 47k against R41 100k || (R38 100k + C24 22n), a
//        bass shelf. C22 10u / R32 100k, R40 1.5k + R37 680, C33 1000p.
// Roland JP-8080 Service Notes, July 1998, pp.19, 21 and 27 repeat that
// network, except the volume follower, which is C601 10u / R601 100k with no
// shunt capacitor.
//
// So there are two nodes to choose from. As printed, the keyboard has a pole
// at 1.06 kHz in the main output (-19.5 dB at 10 kHz), which cannot be the
// whole story unless the DSP pre-emphasises or the marking is wrong. The
// rack shows the same DSP, DAC and jack stage flat, and recordings of both
// instruments fit it and not the printed pole. Rack is the default
// (Docs/research/hardware-ab-2026-11-22.md).
//
// Small signal, ideal op-amps, high-impedance load. Gains are normalised to
// unity at 1 kHz on the rack node. AnalogColor adds the DAC's truncation and
// ladder error and the op-amp's soft limit (on by default); no noise is added.
#pragma once

#include "Core.h"
#include "Oversampling.h"

#include <array>
#include <complex>

namespace trancera
{
// Digital is the DAC word itself, for comparing with an emulator's output.
enum class OutputNode : std::uint8_t { Rack, KeyboardAsPrinted, Digital };

// The DAC's and the op-amps' own colour, on top of the linear circuit: the
// DSP's word truncated rather than rounded into the 18-bit DAC, the R-2R
// ladder's bit-weight error (deterministic, zero at code 0 and zero mean), and
// the jack stage's op-amp rounding off peaks as they near its rail. No
// noise is added. Off, the stage is the ideal small-signal circuit above.
// [inferred] The headroom (1.35 x DAC full scale, from ~1.5 V of swing above a
// ~2.8 V peak DAC output) and the ladder error (0.75 LSB peak) are typical
// figures, not measured on a unit.
struct AnalogColor
{
    bool on { true };
    double opAmpHeadroom { 1.35 }; // relative to DAC full scale at the jack (1 kHz unity)
    double dacDnlLsb { 0.75 };     // peak bit-weight error of the upper 8 bits, 18-bit LSBs
};

class AnalogOutput
{
public:
    AnalogOutput() noexcept { setColor (color_); }

    void setColor (const AnalogColor& color) noexcept
    {
        color_ = color;
        rail_ = color_.on ? color_.opAmpHeadroom : 0.0;
        knee_ = 0.75 * rail_;
        buildLadder (ladderL_, 0x54524E43u, color.dacDnlLsb);
        buildLadder (ladderR_, 0x54524E44u, color.dacDnlLsb);
    }
    const AnalogColor& color() const noexcept { return color_; }

    void prepare (OutputNode node) noexcept
    {
        node_ = node;
        const double c = 2.0 * 8.0 * kEngineRate;
        buffer_.configure (bufferZero, bufferTau, c);
        jack_.configure (0.0, jackTau, c);
        panel_.configure (0.0, panelTau, c);

        const double t1 = d1 * c, t2 = d2 * c * c, t3 = d3 * c * c * c, u = n1 * c;
        const double a0 = 1.0 + t1 + t2 + t3;
        a_ = { (3.0 + t1 - t2 - 3.0 * t3) / a0,
               (3.0 - t1 - t2 + 3.0 * t3) / a0,
               (1.0 - t1 + t2 - t3) / a0 };
        b_ = { (n0 + u) / a0, (3.0 * n0 + u) / a0, (3.0 * n0 - u) / a0, (n0 - u) / a0 };
        normalise_ = 1.0 / std::abs (filters (OutputNode::Rack, 1000.0));
        for (std::size_t i = 0; i < b_.size(); ++i)
            bNorm_[i] = b_[i] * normalise_;

        const double h = 2.0 * kEngineRate * couplingTau;
        couplingPole_ = (h - 1.0) / (h + 1.0);
        couplingGain_ = h / (h + 1.0);
        reset();
    }

    void reset() noexcept
    {
        down_.reset();
        buffer_.reset();
        jack_.reset();
        panel_.reset();
        z_.fill (Double2::zero());
        couplingX_.fill (Double2::zero());
        couplingY_.fill (Double2::zero());
    }

    // One 88.2 kHz DAC word in, the jack voltage out at 88.2 kHz.
    Stereo process (Stereo word) noexcept
    {
        return finishAnalog (Double2::make (convert (word.l, ladderL_), convert (word.r, ladderR_)));
    }

    Stereo processQ23 (StereoQ23 word) noexcept
    {
        return finishAnalog (Double2::make (convertQ23 (word.l, ladderL_), convertQ23 (word.r, ladderR_)));
    }

    void processBlockQ23 (const StereoQ23* in, Stereo* out, int n) noexcept
    {
        if (node_ == OutputNode::Digital)
        {
            for (int i = 0; i < n; ++i)
                out[i] = { convertQ23 (in[i].l, ladderL_), convertQ23 (in[i].r, ladderR_) };
            return;
        }
        const bool hasPanel = node_ == OutputNode::KeyboardAsPrinted;
        const bool hasRail = color_.on && rail_ > 0.0;
        if (! hasPanel && hasRail)
            processAnalogBlock<false, true> (in, out, n);
        else if (! hasPanel && ! hasRail)
            processAnalogBlock<false, false> (in, out, n);
        else if (hasPanel && hasRail)
            processAnalogBlock<true, true> (in, out, n);
        else
            processAnalogBlock<true, false> (in, out, n);
    }

    // Continuous-time response, without the DAC's sample-and-hold droop.
    static std::complex<double> response (OutputNode node, double hz) noexcept
    {
        const std::complex<double> s (0.0, kTwoPi * hz);
        const auto hp = s * couplingTau / (1.0 + s * couplingTau);
        return hp * hp * hp * hp * filters (node, hz) / std::abs (filters (OutputNode::Rack, 1000.0));
    }

private:
    static constexpr double bufferTau = 47000.0 * 100.0e-12;
    static constexpr double bufferZero = bufferTau / (1.0 + 47000.0 / 5600.0);
    static constexpr double jackTau = (1500.0 + 680.0) * 1000.0e-12;
    static constexpr double panelTau = 47000.0 * 22000.0 / (47000.0 + 22000.0) * 10000.0e-12;
    static constexpr double couplingTau = 10.0e-6 * 100000.0; // four in series

    // Sallen-Key with gain K(s) = (n0 + n1 s) / (1 + shelf s), from nodal
    // analysis of R47, R45, C26, C30, R43, R41, R38 and C24.
    static constexpr double r1 = 10000.0, r2 = 10000.0, cFeedback = 470.0e-12, cGround = 680.0e-12;
    static constexpr double legRatio = 100000.0 / 47000.0, legTau = 100000.0 * 0.022e-6;
    static constexpr double n0 = 1.0 + legRatio, n1 = legTau * (2.0 + legRatio);
    static constexpr double shelf = 2.0 * legTau;
    static constexpr double p1 = cGround * (r1 + r2) + cFeedback * r1;
    static constexpr double p2 = r1 * r2 * cFeedback * cGround;
    static constexpr double d1 = shelf + p1 - cFeedback * r1 * n0;
    static constexpr double d2 = p2 + shelf * p1 - cFeedback * r1 * n1;
    static constexpr double d3 = shelf * p2;

    static std::complex<double> filters (OutputNode node, double hz) noexcept
    {
        const std::complex<double> s (0.0, kTwoPi * hz);
        auto h = (1.0 + s * bufferZero) / (1.0 + s * bufferTau) / (1.0 + s * jackTau)
                 * (n0 + s * n1) / (1.0 + s * (d1 + s * (d2 + s * d3)));
        if (node == OutputNode::KeyboardAsPrinted)
            h /= 1.0 + s * panelTau;
        return h;
    }

    struct FirstOrder
    {
        void configure (double zero, double pole, double c) noexcept
        {
            const double a0 = 1.0 + pole * c;
            a1 = (1.0 - pole * c) / a0;
            b0 = (1.0 + zero * c) / a0;
            b1 = (1.0 - zero * c) / a0;
        }
        void reset() noexcept { z = Double2::zero(); }
        double a1 { 0.0 }, b0 { 1.0 }, b1 { 0.0 };
        Double2 z {};
    };

    struct StageCoeffs
    {
        double bB0, bB1, bA1;
        double jB0, jB1, jA1;
        double pB0, pB1, pA1;
        double skB0, skB1, skB2, skB3;
        double skA0, skA1, skA2;
        double rail, knee;
    };

    StageCoeffs loadStageCoeffs() const noexcept
    {
        return {
            buffer_.b0, buffer_.b1, buffer_.a1,
            jack_.b0, jack_.b1, jack_.a1,
            panel_.b0, panel_.b1, panel_.a1,
            bNorm_[0], bNorm_[1], bNorm_[2], bNorm_[3],
            a_[0], a_[1], a_[2],
            rail_, knee_
        };
    }

    template <bool HasPanel, bool HasRail>
    TRANCERA_FORCE_INLINE Double2 stepAnalog8x (Double2 x, const StageCoeffs& c,
                                                Double2& bz, Double2& jz, Double2& pz,
                                                Double2& z0, Double2& z1, Double2& z2) noexcept
    {
        const Double2 bufPre0 = c.bB0 * x;
        const Double2 bufPre1 = c.bB1 * x;
        const Double2 bufSum = bufPre0 + bufPre1;
        Double2 xb = Double2::zero();

        Double2 outerResults[2] {};
    #if defined(__clang__)
    #pragma unroll
    #endif
        for (int i = 0; i < 2; ++i)
        {
            Double2 middleResults[2] {};
        #if defined(__clang__)
        #pragma unroll
        #endif
            for (int j = 0; j < 2; ++j)
            {
                Double2 sub[2] {};
            #if defined(__clang__)
            #pragma unroll
            #endif
                for (int k = 0; k < 2; ++k)
                {
                    xb = (i == 0 && j == 0 && k == 0) ? (bufPre0 + bz)
                                                      : Double2::mulSub (bufSum, c.bA1, xb);
                    Double2 xs = Double2::mulAdd (jz, c.jB0, xb);
                    jz = Double2::mulSub (c.jB1 * xb, c.jA1, xs);
                    if constexpr (HasPanel)
                    {
                        const Double2 xp = Double2::mulAdd (pz, c.pB0, xs);
                        pz = Double2::mulSub (c.pB1 * xs, c.pA1, xp);
                        xs = xp;
                    }
                    const Double2 y = Double2::mulAdd (z0, c.skB0, xs);
                    z0 = Double2::mulSub (Double2::mulAdd (z1, c.skB1, xs), c.skA0, y);
                    z1 = Double2::mulSub (Double2::mulAdd (z2, c.skB2, xs), c.skA1, y);
                    z2 = Double2::mulSub (c.skB3 * xs, c.skA2, y);
                    if constexpr (HasRail)
                    {
                        sub[k] = (fastAbs (y[0]) <= c.knee && fastAbs (y[1]) <= c.knee)
                                     ? y
                                     : Double2::make (swing (y[0], c.rail), swing (y[1], c.rail));
                    }
                    else
                    {
                        sub[k] = y;
                    }
                }
                middleResults[j] = down_.downsampleInner (sub[0], sub[1]);
            }
            outerResults[i] = down_.downsampleMiddle (middleResults[0], middleResults[1]);
        }
        bz = Double2::mulSub (bufPre1, c.bA1, xb);
        return down_.downsampleOuter (outerResults[0], outerResults[1]);
    }

    template <bool HasPanel, bool HasRail>
    Stereo runAnalogSample (Double2 v) noexcept
    {
        const double cGain = couplingGain_, cPole = couplingPole_;
        Double2 x = v;
        for (std::size_t i = 0; i < 4; ++i)
        {
            const Double2 y = cGain * (x - couplingX_[i]) + cPole * couplingY_[i];
            couplingX_[i] = x;
            couplingY_[i] = y;
            x = y;
        }
        const StageCoeffs c = loadStageCoeffs();
        Double2 bz = buffer_.z, jz = jack_.z, pz = panel_.z;
        Double2 z0 = z_[0], z1 = z_[1], z2 = z_[2];
        const Double2 out = stepAnalog8x<HasPanel, HasRail> (x, c, bz, jz, pz, z0, z1, z2);
        buffer_.z = bz;
        jack_.z = jz;
        if constexpr (HasPanel)
            panel_.z = pz;
        else
            (void) pz;
        z_[0] = z0; z_[1] = z1; z_[2] = z2;
        return { out[0], out[1] };
    }

    template <bool HasPanel, bool HasRail>
    void processAnalogBlock (const StereoQ23* in, Stereo* out, int n) noexcept
    {
        const double cGain = couplingGain_, cPole = couplingPole_;
        Double2 cx0 = couplingX_[0], cx1 = couplingX_[1], cx2 = couplingX_[2], cx3 = couplingX_[3];
        Double2 cy0 = couplingY_[0], cy1 = couplingY_[1], cy2 = couplingY_[2], cy3 = couplingY_[3];

        const StageCoeffs c = loadStageCoeffs();
        Double2 bz = buffer_.z, jz = jack_.z, pz = panel_.z;
        Double2 z0 = z_[0], z1 = z_[1], z2 = z_[2];

        auto stepCoupling = [cGain, cPole] (Double2 inVal, Double2& cx, Double2& cy) noexcept -> Double2
        {
            const Double2 outVal = cGain * (inVal - cx) + cPole * cy;
            cx = inVal;
            cy = outVal;
            return outVal;
        };

        for (int idx = 0; idx < n; ++idx)
        {
            const Double2 word = Double2::make (convertQ23 (in[idx].l, ladderL_), convertQ23 (in[idx].r, ladderR_));
            Double2 x = stepCoupling (word, cx0, cy0);
            x = stepCoupling (x, cx1, cy1);
            x = stepCoupling (x, cx2, cy2);
            x = stepCoupling (x, cx3, cy3);
            const Double2 yOut = stepAnalog8x<HasPanel, HasRail> (x, c, bz, jz, pz, z0, z1, z2);
            out[idx] = { yOut[0], yOut[1] };
        }

        couplingX_[0] = cx0; couplingX_[1] = cx1; couplingX_[2] = cx2; couplingX_[3] = cx3;
        couplingY_[0] = cy0; couplingY_[1] = cy1; couplingY_[2] = cy2; couplingY_[3] = cy3;
        buffer_.z = bz;
        jack_.z = jz;
        if constexpr (HasPanel)
            panel_.z = pz;
        else
            (void) pz;
        z_[0] = z0; z_[1] = z1; z_[2] = z2;
    }

    Stereo finishAnalog (Double2 x) noexcept
    {
        if (node_ == OutputNode::Digital)
            return { x[0], x[1] };
        const bool hasPanel = node_ == OutputNode::KeyboardAsPrinted;
        const bool hasRail = color_.on && rail_ > 0.0;
        if (! hasPanel && hasRail)
            return runAnalogSample<false, true> (x);
        if (! hasPanel && ! hasRail)
            return runAnalogSample<false, false> (x);
        if (hasPanel && hasRail)
            return runAnalogSample<true, true> (x);
        return runAnalogSample<true, false> (x);
    }

    static void buildLadder (std::array<double, 256>& table, std::uint32_t seed, double dnlLsb) noexcept
    {
        if (dnlLsb <= 0.0)
        {
            table.fill (0.0);
            return;
        }
        // Binary-weighted upper-8-bit R-2R mismatch profile with zero mean and exact code zero.
        std::array<double, 8> w {};
        std::uint32_t state = seed ? seed : 1u;
        for (std::size_t b = 0; b < 8; ++b)
        {
            state ^= state << 13; state ^= state >> 17; state ^= state << 5;
            const double u = static_cast<double> (state & 0xffffu) / 32767.5 - 1.0;
            w[b] = u / std::sqrt (static_cast<double> (1u << (7u - b)));
        }
        for (std::size_t s = 0; s < table.size(); ++s)
        {
            double sum = 0.0;
            for (std::size_t b = 0; b < 8; ++b)
                sum += ((s >> b) & 1u) != 0u ? w[b] : -w[b];
            table[s] = sum;
        }
        const double zeroShift = table[128] / 255.0;
        double peak = 0.0;
        for (std::size_t s = 0; s < table.size(); ++s)
        {
            table[s] = (s == 128) ? 0.0 : (table[s] + zeroShift);
            peak = std::max (peak, std::abs (table[s]));
        }
        if (peak <= 0.0)
        {
            table.fill (0.0);
            return;
        }
        const double scale = (dnlLsb / 131072.0) / peak;
        for (double& e : table)
            e *= scale;
    }

    double convert (double x, const std::array<double, 256>& ladder) const noexcept
    {
        if (! color_.on || node_ == OutputNode::Digital)
            return std::clamp (std::nearbyint (x * 131072.0), -131072.0, 131071.0) / 131072.0;
        // The DSP's 24-bit word framed to 18 bits by dropping its low bits (a
        // floor), and the ladder's error for the code's upper eight bits.
        // [inferred] A word under one DAC step either side is the DSP's settled
        // zero: the fixed-point DSP's own tails end at zero, while this double
        // engine's only approach it, and a floor would hold them at -1 step
        // (about -108 dBFS, through the coupling caps for some 20 s).
        const double scaled = x * 131072.0;
        if (fastAbs (scaled) < 1.0)
            return 0.0;
        const double code = std::clamp (fastFloor (scaled), -131072.0, 131071.0);
        const auto segment = static_cast<std::size_t> ((static_cast<std::int32_t> (code) + 131072) >> 10);
        return code / 131072.0 + ladder[segment];
    }

    double convertQ23 (Q23 w, const std::array<double, 256>& ladder) const noexcept
    {
        if (! color_.on || node_ == OutputNode::Digital)
        {
            const std::int32_t rounded = w >= 0 ? (w + 32) / 64 : -((-w + 32) / 64);
            return static_cast<double> (std::clamp (rounded, -131072, 131071)) * (1.0 / 131072.0);
        }
        // Frame the 24-bit signed word to 18 bits via arithmetic truncation.
        if (w > -64 && w < 64)
            return 0.0;
        const std::int32_t code = std::clamp (w >> 6, -131072, 131071);
        const auto segment = static_cast<std::size_t> ((code + 131072) >> 10);
        return static_cast<double> (code) * (1.0 / 131072.0) + ladder[segment];
    }

    static double swing (double u, double rail) noexcept
    {
        const double knee = 0.75 * rail, a = fastAbs (u);
        if (a <= knee)
            return u;
        const double span = rail - knee;
        return std::copysign (knee + span * softClip ((a - knee) / span), u);
    }

    oversampling::Downsampler8<Double2> down_;
    FirstOrder buffer_, jack_, panel_;
    OutputNode node_ { OutputNode::Rack };
    std::array<double, 3> a_ {};
    std::array<double, 4> b_ { 1.0, 0.0, 0.0, 0.0 }, bNorm_ { 1.0, 0.0, 0.0, 0.0 };
    std::array<Double2, 3> z_ {};
    std::array<Double2, 4> couplingX_ {}, couplingY_ {};
    double normalise_ { 1.0 }, couplingPole_ { 1.0 }, couplingGain_ { 1.0 };
    AnalogColor color_;
    double rail_ { 1.35 }, knee_ { 0.75 * 1.35 };
    std::array<double, 256> ladderL_ {}, ladderR_ {};
};
} // namespace trancera
