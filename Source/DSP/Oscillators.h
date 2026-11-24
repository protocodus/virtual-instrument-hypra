// OSC 1 (seven waveforms) and OSC 2 (three waveforms, sync, ring, X-Mod) at 88.2 kHz.
#pragma once

#include "Core.h"
#include "TranceraPatch.h"
#include "Mapping.h"

#include <array>
#include <vector>

namespace trancera
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

inline constexpr Q23 wrap24_64 (std::int64_t x) noexcept
{
    const std::uint32_t u = static_cast<std::uint32_t> (static_cast<std::uint64_t> (x)) << 8;
    return static_cast<Q23> (static_cast<std::int32_t> (u) >> 8);
}

inline Q23 aaWindowQ23 (Q23 dist, Q23 inc, std::int32_t mult) noexcept
{
    const Q23 a = sat24 (-static_cast<std::int64_t> (mult) * std::abs (inc));
    const Q23 b = sat24 (static_cast<std::int64_t> (a) * 3);
    const Q23 c = sat24 (((static_cast<std::int64_t> (b) * 5) >> 1) + 786432);
    const Q23 slope = sat24 (kQ23Max + mul8 (a, 10, 5) + mul8 (b, 22, 5));
    const std::int64_t s16p1 = static_cast<std::int64_t> (slope) * 16 + kQ23One;
    return sat24 (kQ23Max + c + ((static_cast<std::int64_t> (dist) * s16p1) >> 23));
}

inline Q23 hfCompQ23 (Q23 inc) noexcept
{
    return sat24 ((kQ23One >> 3) + mulQ23x2 (inc, inc));
}

inline Q23 shapedSawQ23 (Q23 p, Q23 inc, Q23 s) noexcept
{
    const Q23 win = aaWindowQ23 (std::max<Q23> (0, kQ23Max - std::abs (p)), inc, 4);
    const Q23 p2 = mulQ23 (p, p);
    const Q23 p3 = mulQ23 (p2, p);
    const std::int64_t s4 = static_cast<std::int64_t> (s) * 4;
    const std::int64_t c1 = (kQ23One + (kQ23One >> 1)) - s4;
    const std::int64_t c3 = s4 - kQ23One;
    const Q23 poly = sat24 ((c1 * p + c3 * p3) >> 23);
    const Q23 windowed = sat24 (mulQ23 (poly, win));
    const Q23 lifted = sat24 ((static_cast<std::int64_t> (windowed) * hfCompQ23 (inc)) >> 21);
    return mul8 (lifted, 23, 5);
}

inline Q23 shapedTriangleQ23 (Q23 p, Q23 s) noexcept
{
    const Q23 u = (p >> 1) + (kQ23One >> 1);
    const Q23 u2 = mulQ23 (u, u);
    const Q23 u3 = mulQ23 (u2, u);
    const Q23 w = sat24 (u + mulQ23 (s, u3 - u));
    const Q23 abs2w = static_cast<Q23> (abs64 (static_cast<std::int64_t> (w) * 2 - kQ23One));
    const Q23 t = sat24 (static_cast<std::int64_t> (abs2w) * 2 - kQ23One);
    const Q23 t3 = mulQ23 (mulQ23 (t, t), t);
    const Q23 y = sat24 (t + ((static_cast<std::int64_t> (s) * (t3 - t)) >> 21));
    const Q23 atten = kQ23Max - (s >> 1);
    return mul8 (mulQ23 (y, atten), 39, 7);
}

inline Q23 pulseQ23 (Q23 p, Q23 inc, Q23 pw) noexcept
{
    const Q23 tri = sat24 (kQ23Max - (static_cast<std::int64_t> (std::abs (p)) * 2));
    const Q23 aInc = std::abs (inc);
    const Q23 dc = sat24 (static_cast<std::int64_t> (aInc) * 2);
    const Q23 dcOff = mulQ23 (pw, sat24 (kQ23Max - mulQ23 (dc, dc)));
    const Q23 raw = sat24 (tri + dcOff);

    const Q23 a1 = sat24 (-static_cast<std::int64_t> (aInc) * 4);
    const Q23 w1 = sat24 (a1 + kQ23Max);
    const Q23 a2 = sat24 (static_cast<std::int64_t> (a1) * 4);
    const Q23 w2 = sat24 (a2 + kQ23Max);
    const Q23 a3 = sat24 (static_cast<std::int64_t> (a2) * 4);
    const Q23 w3 = sat24 (a3 + kQ23Max);

    const Q23 raw4 = sat24 (static_cast<std::int64_t> (raw) * 4);
    const std::int64_t t1 = (static_cast<std::int64_t> (raw) * w1) >> 21;
    const std::int64_t t2 = (static_cast<std::int64_t> (raw4) * w2) >> 21;
    const Q23 t3 = raw >= 0 ? w3 : -w3;
    const Q23 pulse = sat24 (static_cast<std::int64_t> (raw) + t1 + t2 + t3);
    return mul8 (pulse - dcOff, 19, 7);
}

inline Q23 triangleModQ23 (Q23 p, Q23 inc, Q23 off) noexcept
{
    const Q23 tri = sat24 (kQ23Max - (static_cast<std::int64_t> (std::abs (p)) * 2));
    const std::int64_t gain = (static_cast<std::int64_t> (9) << 20) + off;
    const Q23 wrapped = wrap24_64 ((static_cast<std::int64_t> (tri) * gain) >> 23);
    const Q23 win = aaWindowQ23 (std::max<Q23> (0, kQ23Max - std::abs (wrapped)), inc, 8);
    return mul8 (mulQ23 (wrapped, win), 20, 7);
}

inline Q23 osc2SawQ23 (Q23 p, Q23 inc) noexcept
{
    const Q23 win = aaWindowQ23 (std::max<Q23> (0, kQ23Max - std::abs (p)), inc, 4);
    const Q23 base = sat24 (mulQ23 (p >> 1, win));
    return sat24 ((static_cast<std::int64_t> (base) * hfCompQ23 (inc)) >> 21);
}

struct SawWindowCoeffs
{
    Q23 base { kQ23Max };
    std::int64_t slope { 16LL * kQ23Max + kQ23One };
    Q23 hfComp { kQ23One >> 3 };
    Q23 linearLimit { kQ23Max };
};

inline SawWindowCoeffs makeSawWindowCoeffs (Q23 inc, std::int32_t mult) noexcept
{
    const Q23 a = sat24 (-static_cast<std::int64_t> (mult) * std::abs (inc));
    const Q23 b = sat24 (static_cast<std::int64_t> (a) * 3);
    const Q23 c = sat24 (((static_cast<std::int64_t> (b) * 5) >> 1) + 786432);
    const Q23 slope = sat24 (kQ23Max + mul8 (a, 10, 5) + mul8 (b, 22, 5));
    const Q23 base = kQ23Max + c;
    const std::int64_t slope64 = static_cast<std::int64_t> (slope) * 16 + kQ23One;
    const std::int64_t need = static_cast<std::int64_t> (kQ23Max) - base;
    Q23 linearLimit = kQ23Max;
    if (need > 0 && slope64 > 0)
    {
        const std::int64_t minDist = ((need << 23) + slope64 - 1) / slope64 + 1;
        linearLimit = static_cast<Q23> (std::max<std::int64_t> (-1, static_cast<std::int64_t> (kQ23Max) - minDist));
    }
    return { base, slope64, hfCompQ23 (inc), linearLimit };
}

inline Q23 osc2SawFastQ23 (Q23 p, const SawWindowCoeffs& w) noexcept
{
    const std::int32_t ap = p < 0 ? -static_cast<std::int32_t> (p) : p;
    Q23 base;
    if (ap <= w.linearLimit)
    {
        const Q23 half = p >> 1;
        base = half - (half > 0 ? 1 : 0);
    }
    else
    {
        const Q23 dist = kQ23Max > ap ? (kQ23Max - ap) : 0;
        const Q23 win = sat24 (w.base + ((static_cast<std::int64_t> (dist) * w.slope) >> 23));
        base = sat24 (mulQ23 (p >> 1, win));
    }
    return sat24 ((static_cast<std::int64_t> (base) * w.hfComp) >> 21);
}

inline Q23 osc2TriangleQ23 (Q23 p) noexcept
{
    return mul8 (std::abs (p) - (kQ23One >> 1), 5, 3);
}

inline Q23 syncSawQ23 (Q23 p1, Q23 inc1, Q23 inc2, std::int64_t ratioQ16) noexcept
{
    const Q23 win1 = aaWindowQ23 (std::max<Q23> (0, kQ23Max - std::abs (p1)), inc1, 4);
    const Q23 p2 = wrap24_64 (((static_cast<std::int64_t> (p1) + kQ23One) * ratioQ16 >> 16) - kQ23One);
    const Q23 win2 = aaWindowQ23 (std::max<Q23> (0, kQ23Max - std::abs (p2)), inc2, 4);
    const Q23 base = sat24 (mulQ23 (p2 >> 1, win2));
    const std::int64_t lifted = (static_cast<std::int64_t> (base) * hfCompQ23 (inc2)) >> 21;
    return sat24 ((lifted * win1) >> 23);
}

inline Q23 syncTriangleQ23 (Q23 p1, Q23 inc1, std::int64_t ratioQ16) noexcept
{
    const Q23 win1 = aaWindowQ23 (std::max<Q23> (0, kQ23Max - std::abs (p1)), inc1, 4);
    const Q23 p2a = wrap24_64 (((static_cast<std::int64_t> (p1) + kQ23One) * ratioQ16 >> 16) - kQ23One);
    const Q23 p2b = wrap24_64 ((-(static_cast<std::int64_t> (kQ23One) - p1) * ratioQ16 >> 16) - kQ23One);
    const Q23 tA = std::abs (p2a) - (kQ23One >> 1);
    const Q23 tB = std::abs (p2b) - (kQ23One >> 1);
    const Q23 folded = sat24 (mulQ23 (tA - tB, win1) + tB);
    return mul8 (folded, 5, 3);
}

inline Q23 syncPulseQ23 (Q23 p1, Q23 inc1, Q23 inc2, std::int64_t ratioQ16, Q23 pw) noexcept
{
    const Q23 win1 = aaWindowQ23 (std::max<Q23> (0, kQ23Max - std::abs (p1)), inc1, 4);
    const Q23 p2 = wrap24_64 (((static_cast<std::int64_t> (p1) + kQ23One) * ratioQ16 >> 16) - kQ23One);
    return mulQ23 (pulseQ23 (p2, inc2, pw), win1);
}
} // namespace osc

class SuperSaw
{
public:
    void trigger (Random& random) noexcept
    {
        for (auto& phase : phase_)
            phase = wrap24 (static_cast<std::int32_t> (random.next() >> 8));
        low1_ = band1_ = low2_ = band2_ = 0;
        wrapped_ = false;
        detuneMoving_ = mixMoving_ = pitchMoving_ = false;
        fresh_ = true;
    }

    void snapNext() noexcept
    {
        low1_ = band1_ = low2_ = band2_ = 0;
        fresh_ = true;
    }

    void configure (double hz, double detune, double mix) noexcept
    {
        hz_ = std::max (hz, 1.0);
        centreStepTarget_ = static_cast<std::int32_t> (std::floor (std::clamp (hz_ / kEngineRate, 0.0, 0.499) * 16777216.0));
        detuneTarget_ = toQ23 (mapping::superDetuneReg (detune));
        mixTarget_ = toQ23 (mapping::superMixSide (mix));
        const double inc = std::clamp (2.0 * hz_ / kEngineRate, 1.0e-5, 0.99);
        fTarget_ = toQ23 (std::min (0.99, 1.53125 * inc));
        if (fresh_)
        {
            centreStep_ = centreStepTarget_;
            fQ23_ = fTarget_;
            fSplit_ = splitCoeff15 (fQ23_);
            detuneCur_ = detuneTarget_;
            mixCur_ = mixTarget_;
            updateMixCache();
            updateSteps (detuneCur_);
            detuneMoving_ = mixMoving_ = pitchMoving_ = false;
            fresh_ = false;
        }
        else
        {
            pitchMoving_ = centreStep_ != centreStepTarget_ || fQ23_ != fTarget_;
            detuneMoving_ = detuneCur_ != detuneTarget_;
            mixMoving_ = mixCur_ != mixTarget_;
        }
    }

    Q23 processQ23() noexcept
    {
        bool dirtySteps = false;
        if (pitchMoving_)
        {
            centreStep_ = smoothStepQ23 (centreStep_, centreStepTarget_, 6);
            fQ23_ = smoothStepQ23 (fQ23_, fTarget_, 6);
            fSplit_ = splitCoeff15 (fQ23_);
            dirtySteps = true;
            pitchMoving_ = centreStep_ != centreStepTarget_ || fQ23_ != fTarget_;
        }
        if (detuneMoving_)
        {
            detuneCur_ = smoothStepQ23 (detuneCur_, detuneTarget_, 7);
            dirtySteps = true;
            detuneMoving_ = detuneCur_ != detuneTarget_;
        }
        if (dirtySteps)
            updateSteps (detuneCur_);
        if (mixMoving_)
        {
            mixCur_ = smoothStepQ23 (mixCur_, mixTarget_, 7);
            updateMixCache();
            mixMoving_ = mixCur_ != mixTarget_;
        }

        const Q23 p0 = wrap24 (phase_[0] + step_[0]); phase_[0] = p0;
        const Q23 p1 = wrap24 (phase_[1] + step_[1]); phase_[1] = p1;
        const Q23 p2 = wrap24 (phase_[2] + step_[2]); phase_[2] = p2;
        const Q23 before = phase_[3];
        const Q23 p3 = wrap24 (before + step_[3]); phase_[3] = p3;
        wrapped_ = p3 < before;
        const Q23 p4 = wrap24 (phase_[4] + step_[4]); phase_[4] = p4;
        const Q23 p5 = wrap24 (phase_[5] + step_[5]); phase_[5] = p5;
        const Q23 p6 = wrap24 (phase_[6] + step_[6]); phase_[6] = p6;

        const std::int32_t sides = mul8 (p0, mix8_, 7) + mul8 (p1, mix8_, 7) + mul8 (p2, mix8_, 7)
                                 + mul8 (p4, mix8_, 7) + mul8 (p5, mix8_, 7) + mul8 (p6, mix8_, 7);
        const Q23 sumSat = sat24 (mul8 (p3, 25, 7) + sides);
        const Q23 hpIn = sat24 (mulSplit15x2 (sumSat, attenSplit_.hi, attenSplit_.lo));

        low1_ = sat24 (low1_ + mulSplit15 (band1_, fSplit_.hi, fSplit_.lo));
        const Q23 high1 = sat24 ((hpIn >> 1) - sat24 (low1_ + band1_));
        band1_ = sat24 (band1_ + mulSplit15x2 (high1, fSplit_.hi, fSplit_.lo));

        low2_ = sat24 (low2_ + mulSplit15x2 (band2_, fSplit_.hi, fSplit_.lo));
        const Q23 high2 = sat24 (mul8 (high1, 20, 6) - sat24 (low2_ + band2_));
        band2_ = sat24 (band2_ + mulSplit15x2 (high2, fSplit_.hi, fSplit_.lo));

        return sat24 (mul8 (high2, 116, 6));
    }

    void processBlockQ23 (Q23* out, int n) noexcept
    {
        int i = 0;
        while (i < n && (detuneMoving_ || mixMoving_))
            out[i++] = processQ23();
        if (i >= n)
            return;

        Int32x4 phLo = Int32x4::make (phase_[0], phase_[1], phase_[2], phase_[3]);
        Int32x4 phHi = Int32x4::make (phase_[4], phase_[5], phase_[6], 0);
        const Int32x4 stLo = Int32x4::make (step_[0], step_[1], step_[2], step_[3]);
        const Int32x4 stHi = Int32x4::make (step_[4], step_[5], step_[6], 0);
        const Int32x4 mxLo = Int32x4::make (mix8_, mix8_, mix8_, 25);
        const Int32x4 mxHi = Int32x4::make (mix8_, mix8_, mix8_, 0);
        const std::int32_t attHi = attenSplit_.hi, attLo = attenSplit_.lo;
        const std::int32_t fHi = fSplit_.hi, fLo = fSplit_.lo;
        Q23 low1 = low1_, band1 = band1_, low2 = low2_, band2 = band2_;
        bool wrapped = wrapped_;

        for (; i < n; ++i)
        {
            const Q23 before = phLo[3];
            phLo = wrap24x4 (phLo + stLo);
            phHi = wrap24x4 (phHi + stHi);
            wrapped = phLo[3] < before;

            const Int32x4 wSum = ((phLo * mxLo) >> 7) + ((phHi * mxHi) >> 7);
            const Q23 sumSat = sat24 (wSum[0] + wSum[1] + wSum[2] + wSum[3]);
            const Q23 hpIn = sat24 (mulSplit15x2 (sumSat, attHi, attLo));

            low1 = sat24 (low1 + mulSplit15 (band1, fHi, fLo));
            const Q23 high1 = sat24 ((hpIn >> 1) - sat24 (low1 + band1));
            band1 = sat24 (band1 + mulSplit15x2 (high1, fHi, fLo));

            low2 = sat24 (low2 + mulSplit15x2 (band2, fHi, fLo));
            const Q23 high2 = sat24 (mul8 (high1, 20, 6) - sat24 (low2 + band2));
            band2 = sat24 (band2 + mulSplit15x2 (high2, fHi, fLo));

            out[i] = sat24 (mul8 (high2, 116, 6));
        }

        phase_[0] = phLo[0]; phase_[1] = phLo[1]; phase_[2] = phLo[2]; phase_[3] = phLo[3];
        phase_[4] = phHi[0]; phase_[5] = phHi[1]; phase_[6] = phHi[2];
        low1_ = low1; band1_ = band1; low2_ = low2; band2_ = band2;
        wrapped_ = wrapped;
    }

    double process() noexcept { return fromQ23 (processQ23()); }

    bool wrapped() const noexcept { return wrapped_; }
    bool isMoving() const noexcept { return detuneMoving_ || mixMoving_ || pitchMoving_; }

    static void process4BlockQ23 (SuperSaw* const s[4], Q23* const out[4], int n) noexcept
    {
        if (n <= 0)
            return;

        Int32x4 ph0 = Int32x4::make (s[0]->phase_[0], s[1]->phase_[0], s[2]->phase_[0], s[3]->phase_[0]);
        Int32x4 ph1 = Int32x4::make (s[0]->phase_[1], s[1]->phase_[1], s[2]->phase_[1], s[3]->phase_[1]);
        Int32x4 ph2 = Int32x4::make (s[0]->phase_[2], s[1]->phase_[2], s[2]->phase_[2], s[3]->phase_[2]);
        Int32x4 ph3 = Int32x4::make (s[0]->phase_[3], s[1]->phase_[3], s[2]->phase_[3], s[3]->phase_[3]);
        Int32x4 ph4 = Int32x4::make (s[0]->phase_[4], s[1]->phase_[4], s[2]->phase_[4], s[3]->phase_[4]);
        Int32x4 ph5 = Int32x4::make (s[0]->phase_[5], s[1]->phase_[5], s[2]->phase_[5], s[3]->phase_[5]);
        Int32x4 ph6 = Int32x4::make (s[0]->phase_[6], s[1]->phase_[6], s[2]->phase_[6], s[3]->phase_[6]);

        const Int32x4 st0 = Int32x4::make (s[0]->step_[0], s[1]->step_[0], s[2]->step_[0], s[3]->step_[0]);
        const Int32x4 st1 = Int32x4::make (s[0]->step_[1], s[1]->step_[1], s[2]->step_[1], s[3]->step_[1]);
        const Int32x4 st2 = Int32x4::make (s[0]->step_[2], s[1]->step_[2], s[2]->step_[2], s[3]->step_[2]);
        const Int32x4 st3 = Int32x4::make (s[0]->step_[3], s[1]->step_[3], s[2]->step_[3], s[3]->step_[3]);
        const Int32x4 st4 = Int32x4::make (s[0]->step_[4], s[1]->step_[4], s[2]->step_[4], s[3]->step_[4]);
        const Int32x4 st5 = Int32x4::make (s[0]->step_[5], s[1]->step_[5], s[2]->step_[5], s[3]->step_[5]);
        const Int32x4 st6 = Int32x4::make (s[0]->step_[6], s[1]->step_[6], s[2]->step_[6], s[3]->step_[6]);

        const Int32x4 mix8 = Int32x4::make (s[0]->mix8_, s[1]->mix8_, s[2]->mix8_, s[3]->mix8_);
        const Int32x4 attHi = Int32x4::make (s[0]->attenSplit_.hi, s[1]->attenSplit_.hi, s[2]->attenSplit_.hi, s[3]->attenSplit_.hi);
        const Int32x4 attLo = Int32x4::make (s[0]->attenSplit_.lo, s[1]->attenSplit_.lo, s[2]->attenSplit_.lo, s[3]->attenSplit_.lo);
        const Int32x4 fHi = Int32x4::make (s[0]->fSplit_.hi, s[1]->fSplit_.hi, s[2]->fSplit_.hi, s[3]->fSplit_.hi);
        const Int32x4 fLo = Int32x4::make (s[0]->fSplit_.lo, s[1]->fSplit_.lo, s[2]->fSplit_.lo, s[3]->fSplit_.lo);

        Int32x4 low1 = Int32x4::make (s[0]->low1_, s[1]->low1_, s[2]->low1_, s[3]->low1_);
        Int32x4 band1 = Int32x4::make (s[0]->band1_, s[1]->band1_, s[2]->band1_, s[3]->band1_);
        Int32x4 low2 = Int32x4::make (s[0]->low2_, s[1]->low2_, s[2]->low2_, s[3]->low2_);
        Int32x4 band2 = Int32x4::make (s[0]->band2_, s[1]->band2_, s[2]->band2_, s[3]->band2_);

        const Int32x4 c25 = Int32x4::splat (25);
        const Int32x4 c20 = Int32x4::splat (20);
        const Int32x4 c116 = Int32x4::splat (116);
        Q23* const o0 = out[0]; Q23* const o1 = out[1]; Q23* const o2 = out[2]; Q23* const o3 = out[3];
        Int32x4 before3 = ph3;

        for (int i = 0; i < n; ++i)
        {
            before3 = ph3;
            ph0 = wrap24x4 (ph0 + st0);
            ph1 = wrap24x4 (ph1 + st1);
            ph2 = wrap24x4 (ph2 + st2);
            ph3 = wrap24x4 (ph3 + st3);
            ph4 = wrap24x4 (ph4 + st4);
            ph5 = wrap24x4 (ph5 + st5);
            ph6 = wrap24x4 (ph6 + st6);

            const Int32x4 sides012 = ((ph0 * mix8) >> 7) + ((ph1 * mix8) >> 7) + ((ph2 * mix8) >> 7);
            const Int32x4 sides456 = ((ph4 * mix8) >> 7) + ((ph5 * mix8) >> 7) + ((ph6 * mix8) >> 7);
            const Int32x4 sum = ((ph3 * c25) >> 7) + sides012 + sides456;
            const Int32x4 sumSat = sat24x4 (sum);
            const Int32x4 hpIn = sat24x4 (mulSplit15x2x4 (sumSat, attHi, attLo));

            low1 = sat24x4 (low1 + mulSplit15x4 (band1, fHi, fLo));
            const Int32x4 high1 = sat24x4 ((hpIn >> 1) - sat24x4 (low1 + band1));
            band1 = sat24x4 (band1 + mulSplit15x2x4 (high1, fHi, fLo));

            low2 = sat24x4 (low2 + mulSplit15x2x4 (band2, fHi, fLo));
            const Int32x4 high2 = sat24x4 (((high1 * c20) >> 6) - sat24x4 (low2 + band2));
            band2 = sat24x4 (band2 + mulSplit15x2x4 (high2, fHi, fLo));

            const Int32x4 y = sat24x4 ((high2 * c116) >> 6);
            o0[i] = y[0]; o1[i] = y[1]; o2[i] = y[2]; o3[i] = y[3];
        }

        for (int k = 0; k < 4; ++k)
        {
            s[k]->phase_[0] = ph0[k]; s[k]->phase_[1] = ph1[k]; s[k]->phase_[2] = ph2[k];
            s[k]->phase_[3] = ph3[k]; s[k]->phase_[4] = ph4[k]; s[k]->phase_[5] = ph5[k];
            s[k]->phase_[6] = ph6[k];
            s[k]->low1_ = low1[k]; s[k]->band1_ = band1[k];
            s[k]->low2_ = low2[k]; s[k]->band2_ = band2[k];
            s[k]->wrapped_ = ph3[k] < before3[k];
        }
    }
    Q23 centrePhaseQ23() const noexcept { return phase_[3]; }
    double centrePhase() const noexcept { return fromQ23 (phase_[3]); }

    int stepsFromCentre (int saw) const noexcept
    {
        return step_[static_cast<std::size_t> (saw)] - step_[3];
    }

private:
    void updateSteps (Q23 detuneReg) noexcept
    {
        const Q23 baseDetune = mulCoeff15 (centreStep_, detuneReg);
        step_[0] = std::min<std::int32_t> (kQ23Max, centreStep_ + mul8 (baseDetune, -45, 3));
        step_[1] = std::min<std::int32_t> (kQ23Max, centreStep_ + mul8 (baseDetune, -103, 5));
        step_[2] = std::min<std::int32_t> (kQ23Max, centreStep_ - baseDetune);
        step_[3] = centreStep_;
        step_[4] = std::min<std::int32_t> (kQ23Max, centreStep_ + baseDetune);
        step_[5] = std::min<std::int32_t> (kQ23Max, centreStep_ + mul8 (baseDetune, 102, 5));
        step_[6] = std::min<std::int32_t> (kQ23Max, centreStep_ + mul8 (baseDetune, 44, 3));
    }

    void updateMixCache() noexcept
    {
        mix8_ = mixCur_ >> 16;
        attenSplit_ = splitCoeff15 (kQ23Max - sat24 (mul8 (mixCur_, 70, 5)));
    }

    std::array<Q23, 7> phase_ {}, step_ {};
    double hz_ { 440.0 };
    std::int32_t centreStep_ { 0 }, centreStepTarget_ { 0 }, mix8_ { 0 };
    Q23 detuneCur_ { 0 }, detuneTarget_ { 0 };
    Q23 mixCur_ { 0 }, mixTarget_ { 0 }, fQ23_ { 8192 }, fTarget_ { 8192 };
    Coeff15 fSplit_ { splitCoeff15 (8192) }, attenSplit_ { splitCoeff15 (kQ23Max) };
    Q23 low1_ { 0 }, band1_ { 0 }, low2_ { 0 }, band2_ { 0 };
    bool wrapped_ { false }, detuneMoving_ { false }, mixMoving_ { false }, pitchMoving_ { false }, fresh_ { true };
};

class NoiseOsc
{
public:
    void snapNext() noexcept { fresh_ = true; }

    void reset() noexcept
    {
        s1_ = s2_ = low1_ = band1_ = low2_ = band2_ = 0;
        fMoving_ = qMoving_ = false;
        fresh_ = true;
    }

    void reset (Random& random) noexcept
    {
        s1_ = wrap24 (static_cast<std::int32_t> (random.next() >> 8));
        s2_ = wrap24 (static_cast<std::int32_t> (random.next() >> 8)) >> 1;
        low1_ = band1_ = low2_ = band2_ = 0;
        fMoving_ = qMoving_ = false;
        fresh_ = true;
    }

    void configure (double hz, double cutoff, double resonance) noexcept
    {
        const double f = mapping::noiseCoefficient (hz, cutoff);
        fTarget_ = toQ23 (f);
        qHalfTarget_ = sat24 (toQ23 (mapping::noiseResonanceQHalf (resonance, f)));
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

    Q23 processQ23 (Random&) noexcept
    {
        if (fMoving_)
        {
            f_ = smoothStepQ23 (f_, fTarget_, 11);
            fc_ = splitCoeff15 (f_);
            fMoving_ = f_ != fTarget_;
        }
        if (qMoving_)
        {
            qHalf_ = smoothStepQ23 (qHalf_, qHalfTarget_, 11);
            qc_ = splitCoeff15 (qHalf_);
            qMoving_ = qHalf_ != qHalfTarget_;
        }

        // 24-bit wrapping chaotic noise recurrence producing [-0.5, +0.5].
        const Q23 nextS1 = wrap24 (0x3E7900 + mulImmediate15 (s1_, -116, 83, 6) + mulImmediate15 (s2_, 127, 62, 7));
        const Q23 nextS2 = wrap24 (mulImmediate15 (nextS1, -127, 58, 6) + mulImmediate15 (s2_, 126, 39, 6)) >> 1;
        s1_ = nextS1;
        s2_ = nextS2;

        const Q23 x1 = sat24 (mulSplit15 (nextS2, qc_.hi, qc_.lo) + (nextS2 >> 9));
        const Q23 low1Prev = low1_;
        const Q23 low2Prev = low2_;

        low1_ = sat24 (low1_ + mulSplit15 (band1_, fc_.hi, fc_.lo));
        const Q23 h1 = sat24 (x1 - sat24 (low1_ + mulSplit15x2 (band1_, qc_.hi, qc_.lo)));
        band1_ = sat24 (band1_ + mulSplit15 (h1, fc_.hi, fc_.lo));

        const Q23 x2 = sat24 (mulImmediate15 (low1Prev, 53, 97, 6));
        low2_ = sat24 (low2_ + mulSplit15 (band2_, fc_.hi, fc_.lo));
        const Q23 h2 = sat24 (x2 - sat24 (low2_ + mulSplit15x2 (band2_, qc_.hi, qc_.lo)));
        band2_ = sat24 (band2_ + mulSplit15 (h2, fc_.hi, fc_.lo));

        const Q23 comp = sat24 (sat24 (mul8 (qHalf_, 38, 3)) + 1509888);
        return mul8 (sat24 (mulCoeff15x2 (low2Prev, comp)), 30, 6);
    }

    double process (Random& random) noexcept { return fromQ23 (processQ23 (random)); }

private:
    Q23 s1_ { 0 }, s2_ { 0 };
    Q23 f_ { 167772 }, fTarget_ { 167772 };
    Q23 qHalf_ { kQ23One >> 1 }, qHalfTarget_ { kQ23One >> 1 };
    Coeff15 fc_ { splitCoeff15 (167772) }, qc_ { splitCoeff15 (kQ23One >> 1) };
    Q23 low1_ { 0 }, band1_ { 0 }, low2_ { 0 }, band2_ { 0 };
    bool fMoving_ { false }, qMoving_ { false }, fresh_ { true };
};

// Two-tap fractional comb feedback oscillator without a separate resonant filter.
class FeedbackOsc
{
public:
    void prepare()
    {
        buffer_.assign (size, 0u);
        reset();
    }

    void reset() noexcept
    {
        std::fill (buffer_.begin(), buffer_.end(), std::uint16_t { 0 });
        write_ = 0;
        dc_ = 0;
        fresh_ = true;
    }

    void retrigger() noexcept {}

    void configure (double hz, double harmonics, double amount) noexcept
    {
        const double d = std::clamp (kEngineRate / std::max (hz, 15.0), 2.0, static_cast<double> (size - 3));
        d1TargetQ16_ = static_cast<std::int32_t> (std::llround (d * 65536.0));
        hTarget_ = toQ23 (mapping::feedbackHarmonics (harmonics));
        amtTarget_ = toQ23 (mapping::feedbackAmount (amount));
        if (fresh_)
        {
            d1Q16_ = d1TargetQ16_;
            h_ = hTarget_;
            amt_ = amtTarget_;
            fresh_ = false;
        }
    }

    Q23 processQ23 (Q23 rawSawPhase) noexcept
    {
        const std::int32_t dDiff = d1TargetQ16_ - d1Q16_;
        if (dDiff != 0)
        {
            std::int32_t step = dDiff >> 7;
            if (step == 0)
                step = dDiff > 0 ? 1 : -1;
            d1Q16_ += step;
        }
        h_ = smoothStepQ23 (h_, hTarget_, 11);
        amt_ = smoothStepQ23 (amt_, amtTarget_, 11);

        const std::int64_t hScale = (static_cast<std::int64_t> (kQ23One) >> 1) + h_;
        const std::int32_t d2Q16 = static_cast<std::int32_t> (std::max<std::int64_t> (65536, (static_cast<std::int64_t> (d1Q16_) * hScale) >> 23));
        const Q23 tap1 = readLerpQ23 (d1Q16_);
        const Q23 tap2 = readLerpQ23 (d2Q16);

        const Q23 loop = sat24 (mul8 (rawSawPhase, 35, 6) + mul8 (tap1, 40, 6) + mul8 (mulCoeff15 (tap2, amt_), 102, 6));
        const Q23 hp = sat24 (mulImmediate15 (loop, 127, 96, 7) + dc_);
        dc_ = sat24 (dc_ + mulImmediate15 (hp, -1, 64, 7));

        if (! buffer_.empty())
            buffer_[static_cast<std::size_t> (write_)] = packDelay16 (mul8 (hp, 42, 6));
        write_ = (write_ + 1) & mask;
        return mul8 (hp, 23, 6);
    }

    double process (double rawSawPhase) noexcept { return fromQ23 (processQ23 (toQ23 (rawSawPhase))); }

private:
    Q23 readLerpQ23 (std::int32_t delayQ16) const noexcept
    {
        if (buffer_.empty())
            return 0;
        const std::int32_t whole = delayQ16 >> 16;
        const std::int32_t frac = delayQ16 & 0xFFFF;
        const std::int32_t idx = (write_ - whole) & mask;
        const Q23 a = unpackDelay16 (buffer_[static_cast<std::size_t> (idx)]);
        const Q23 b = unpackDelay16 (buffer_[static_cast<std::size_t> ((idx - 1) & mask)]);
        return static_cast<Q23> (a + ((static_cast<std::int64_t> (b - a) * frac) >> 16));
    }

    static constexpr int size = 8192, mask = size - 1;
    std::vector<std::uint16_t> buffer_;
    int write_ { 0 };
    std::int32_t d1Q16_ { 100 << 16 }, d1TargetQ16_ { 100 << 16 };
    Q23 h_ { 0 }, hTarget_ { 0 }, amt_ { 0 }, amtTarget_ { 0 }, dc_ { 0 };
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

    void prepare()
    {
        feedback_.prepare();
    }

    void trigger (Random& random, Wave1 wave, bool fresh) noexcept
    {
        super_.trigger (random);
        if (fresh)
        {
            p1_ = wrap24 (static_cast<std::int32_t> (random.next() >> 8));
            p2_ = wrap24 (static_cast<std::int32_t> (random.next() >> 8));
            last2_ = 0;
            dc_ = 0;
            noise_.reset (random);
            feedback_.reset();
            shapeMoving_ = pw2Moving_ = balMoving_ = pitchMoving_ = fmMoving_ = false;
            modeFade_ = 0;
            freshSmooth_ = true;
        }
        if (wave == Wave1::FeedbackOsc)
            feedback_.retrigger();
    }

    void configure (const Controls& c) noexcept
    {
        if (freshSmooth_)
        {
            c_ = prevC_ = c;
            modeFade_ = 0;
        }
        else if (c.wave1 != c_.wave1 || c.wave2 != c_.wave2 || c.sync != c_.sync || c.ring != c_.ring)
        {
            if (modeFade_ > 0 && c.wave1 == prevC_.wave1 && c.wave2 == prevC_.wave2
                && c.sync == prevC_.sync && c.ring == prevC_.ring)
            {
                std::swap (c_, prevC_);
                modeFade_ = 256 - modeFade_;
            }
            else
            {
                prevC_ = c_;
                if (c.wave1 != c_.wave1)
                {
                    if (c.wave1 == Wave1::FeedbackOsc)
                        feedback_.reset();
                    else if (c.wave1 == Wave1::SuperSaw)
                        super_.snapNext();
                    else if (c.wave1 == Wave1::Noise)
                        noise_.snapNext();
                }
                c_ = c;
                modeFade_ = 256;
            }
        }
        else
        {
            c_ = c;
        }

        inc1Target_ = toQ23 (std::clamp (2.0 * c.hz1 / kEngineRate, -0.998, 0.998));
        inc2Target_ = toQ23 (std::clamp (2.0 * c.hz2 / kEngineRate, -0.998, 0.998));
        exactRatioQ16_ = static_cast<std::int64_t> (std::clamp (c.hz2 / std::max (c.hz1, 1.0), 0.0, 2000.0) * 65536.0);

        auto configureWave1 = [&] (Wave1 w) noexcept
        {
            if (w == Wave1::SuperSaw)
                super_.configure (c.hz1, c.ctrl1, c.ctrl2);
            else if (w == Wave1::Noise)
                noise_.configure (c.hz1, c.ctrl1, c.ctrl2);
            else if (w == Wave1::FeedbackOsc)
                feedback_.configure (c.hz1, c.ctrl1, c.ctrl2);
        };
        configureWave1 (c_.wave1);
        if (modeFade_ > 0 && prevC_.wave1 != c_.wave1)
            configureWave1 (prevC_.wave1);

        const double st = c.wave1 == Wave1::TriangleMod ? mapping::triangleModOffset (c.ctrl1)
                        : c.wave1 == Wave1::Square      ? mapping::pulseWidth (c.ctrl1)
                                                        : mapping::waveShape (c.ctrl1);
        shapeTarget_ = toQ23 (st);
        pw2Target_ = toQ23 (mapping::pulseWidth (c.width2));
        balTarget_ = sat24 (toQ23 (std::clamp (c.gain2, 0.0, 1.0)));
        fmTarget_ = toQ23 (mapping::crossModGain (c.xmod));
        if (freshSmooth_)
        {
            inc1_ = inc1Target_;
            inc2_ = inc2Target_;
            osc2SawWin_ = osc::makeSawWindowCoeffs (inc2_, 4);
            ratioQ16_ = exactRatioQ16_;
            shape_ = shapeTarget_;
            pw2_ = pw2Target_;
            bal_ = balTarget_;
            balSplit_ = splitCoeff15 (bal_);
            invBalSplit_ = splitCoeff15 (kQ23Max - bal_);
            fmGain_ = fmTarget_;
            shapeMoving_ = pw2Moving_ = balMoving_ = pitchMoving_ = fmMoving_ = false;
            freshSmooth_ = false;
        }
        else
        {
            pitchMoving_ = inc1_ != inc1Target_ || inc2_ != inc2Target_;
            if (! pitchMoving_)
                ratioQ16_ = exactRatioQ16_;
            shapeMoving_ = shape_ != shapeTarget_;
            pw2Moving_ = pw2_ != pw2Target_;
            balMoving_ = bal_ != balTarget_;
            fmMoving_ = fmGain_ != fmTarget_;
        }

        fm_ = (fmGain_ > 0 || fmTarget_ > 0) && c_.wave1 != Wave1::SuperSaw && c_.wave1 != Wave1::Noise;
    }

    Q23 processQ23 (Random& random) noexcept
    {
        if (shapeMoving_)
        {
            shape_ = smoothStepQ23 (shape_, shapeTarget_, 11);
            shapeMoving_ = shape_ != shapeTarget_;
        }
        if (pw2Moving_)
        {
            pw2_ = smoothStepQ23 (pw2_, pw2Target_, 11);
            pw2Moving_ = pw2_ != pw2Target_;
        }
        if (balMoving_)
        {
            bal_ = smoothStepQ23 (bal_, balTarget_, 8);
            balSplit_ = splitCoeff15 (bal_);
            invBalSplit_ = splitCoeff15 (kQ23Max - bal_);
            balMoving_ = bal_ != balTarget_;
        }
        if (fmMoving_)
        {
            fmGain_ = smoothStepQ23 (fmGain_, fmTarget_, 8);
            fmMoving_ = fmGain_ != fmTarget_;
            if (! fmMoving_)
                fm_ = fmGain_ > 0 && c_.wave1 != Wave1::SuperSaw && c_.wave1 != Wave1::Noise;
        }
        if (pitchMoving_)
        {
            inc1_ = smoothStepQ23 (inc1_, inc1Target_, 6);
            inc2_ = smoothStepQ23 (inc2_, inc2Target_, 6);
            osc2SawWin_ = osc::makeSawWindowCoeffs (inc2_, 4);
            pitchMoving_ = inc1_ != inc1Target_ || inc2_ != inc2Target_;
            if (pitchMoving_)
            {
                ratioQ16_ = inc1_ > 0
                    ? std::clamp<std::int64_t> (
                          (static_cast<std::int64_t> (std::max<Q23> (0, inc2_)) * 65536LL) / inc1_, 0, 2000LL << 16)
                    : 65536;
            }
            else
            {
                ratioQ16_ = exactRatioQ16_;
            }
        }

        const Q23 inc1Fm = std::clamp<Q23> (inc1_ + mulQ23 (fmGain_, last2_), -8371830, 8371830);
        if (modeFade_ > 0)
        {
            const bool needSuper = c_.wave1 == Wave1::SuperSaw || prevC_.wave1 == Wave1::SuperSaw;
            const bool needNoise = c_.wave1 == Wave1::Noise || prevC_.wave1 == Wave1::Noise;
            const bool needFb = c_.wave1 == Wave1::FeedbackOsc || prevC_.wave1 == Wave1::FeedbackOsc;
            const bool needP1 = c_.wave1 != Wave1::SuperSaw || prevC_.wave1 != Wave1::SuperSaw;
            const Q23 p1Inc = (fm_ && c_.wave1 != Wave1::SuperSaw && c_.wave1 != Wave1::Noise) ? inc1Fm : inc1_;
            if (needP1)
                p1_ = wrap24 (p1_ + p1Inc);
            const Q23 superVal = needSuper ? super_.processQ23() : 0;
            const Q23 noiseVal = needNoise ? noise_.processQ23 (random) : 0;
            const Q23 fbVal = needFb ? feedback_.processQ23 (p1_) : 0;
            if (! c_.sync || ! prevC_.sync)
                p2_ = wrap24 (p2_ + inc2_);

            auto evalWave1 = [&] (Wave1 w, Q23 effInc) noexcept -> std::pair<Q23, Q23>
            {
                if (w == Wave1::SuperSaw)
                    return { superVal, super_.centrePhaseQ23() };
                Q23 f = 0;
                switch (w)
                {
                    case Wave1::TriangleMod: f = osc::triangleModQ23 (p1_, effInc, shape_); break;
                    case Wave1::Noise:       f = noiseVal; break;
                    case Wave1::FeedbackOsc: f = fbVal; break;
                    case Wave1::Square:      f = osc::pulseQ23 (p1_, effInc, shape_); break;
                    case Wave1::Saw:         f = osc::shapedSawQ23 (p1_, effInc, shape_); break;
                    case Wave1::Triangle:    f = osc::shapedTriangleQ23 (p1_, shape_); break;
                    case Wave1::SuperSaw:    break;
                }
                return { f, p1_ };
            };
            const Q23 effOld = (fmGain_ > 0 && prevC_.wave1 != Wave1::SuperSaw && prevC_.wave1 != Wave1::Noise) ? inc1Fm : inc1_;
            const Q23 effNew = (fmGain_ > 0 && c_.wave1 != Wave1::SuperSaw && c_.wave1 != Wave1::Noise) ? inc1Fm : inc1_;
            const auto [firstOld, masterOld] = evalWave1 (prevC_.wave1, effOld);
            const auto [firstNew, masterNew] = evalWave1 (c_.wave1, effNew);

            auto evalWave2 = [&] (const Controls& cfg, Q23 firstSig, Q23 masterPh, Q23 effInc) noexcept -> std::pair<Q23, Q23>
            {
                Q23 sec = 0;
                if (cfg.sync)
                {
                    switch (cfg.wave2)
                    {
                        case Wave2::Square:   sec = osc::syncPulseQ23 (masterPh, effInc, inc2_, ratioQ16_, pw2_); break;
                        case Wave2::Saw:      sec = osc::syncSawQ23 (masterPh, effInc, inc2_, ratioQ16_); break;
                        case Wave2::Triangle: sec = osc::syncTriangleQ23 (masterPh, effInc, ratioQ16_); break;
                    }
                }
                else
                {
                    switch (cfg.wave2)
                    {
                        case Wave2::Square:   sec = osc::pulseQ23 (p2_, inc2_, pw2_); break;
                        case Wave2::Saw:      sec = osc::osc2SawFastQ23 (p2_, osc2SawWin_); break;
                        case Wave2::Triangle: sec = osc::osc2TriangleQ23 (p2_); break;
                    }
                }
                const Q23 outSec = cfg.ring ? sat24 ((static_cast<std::int64_t> (firstSig) * sec) >> 21) : sec;
                const Q23 mixed = sat24 (mulSplit15 (firstSig, invBalSplit_.hi, invBalSplit_.lo)
                                         + mulSplit15 (outSec, balSplit_.hi, balSplit_.lo));
                return { sec, mixed };
            };
            const auto [raw2Old, mixOld] = evalWave2 (prevC_, firstOld, masterOld, effOld);
            const auto [raw2New, mixNew] = evalWave2 (c_, firstNew, masterNew, effNew);
            (void) raw2Old;
            last2_ = c_.wave2 == Wave2::Triangle ? sat24 ((static_cast<std::int64_t> (raw2New) * 8) / 5) : raw2New;
            const Q23 mix = static_cast<Q23> ((static_cast<std::int64_t> (mixOld) * modeFade_
                                               + static_cast<std::int64_t> (mixNew) * (256 - modeFade_)) >> 8);
            --modeFade_;
            const Q23 y = sat24 (mix + dc_);
            dc_ = sat24 (dc_ + mulImmediate15 (y, -1, 120, 7));
            return y;
        }

        Q23 first = 0;
        Q23 masterPhase = 0;
        const Q23 inc1Eff = fm_ ? inc1Fm : inc1_;

        if (c_.wave1 == Wave1::SuperSaw)
        {
            first = super_.processQ23();
            masterPhase = super_.centrePhaseQ23();
        }
        else
        {
            p1_ = wrap24 (p1_ + inc1Eff);
            masterPhase = p1_;
            switch (c_.wave1)
            {
                case Wave1::SuperSaw: break;
                case Wave1::TriangleMod: first = osc::triangleModQ23 (p1_, inc1Eff, shape_); break;
                case Wave1::Noise: first = noise_.processQ23 (random); break;
                case Wave1::FeedbackOsc: first = feedback_.processQ23 (p1_); break;
                case Wave1::Square: first = osc::pulseQ23 (p1_, inc1Eff, shape_); break;
                case Wave1::Saw: first = osc::shapedSawQ23 (p1_, inc1Eff, shape_); break;
                case Wave1::Triangle: first = osc::shapedTriangleQ23 (p1_, shape_); break;
            }
        }

        Q23 second = 0;
        if (c_.sync)
        {
            switch (c_.wave2)
            {
                case Wave2::Square:   second = osc::syncPulseQ23 (masterPhase, inc1Eff, inc2_, ratioQ16_, pw2_); break;
                case Wave2::Saw:      second = osc::syncSawQ23 (masterPhase, inc1Eff, inc2_, ratioQ16_); break;
                case Wave2::Triangle: second = osc::syncTriangleQ23 (masterPhase, inc1Eff, ratioQ16_); break;
            }
        }
        else
        {
            p2_ = wrap24 (p2_ + inc2_);
            switch (c_.wave2)
            {
                case Wave2::Square:   second = osc::pulseQ23 (p2_, inc2_, pw2_); break;
                case Wave2::Saw:      second = osc::osc2SawFastQ23 (p2_, osc2SawWin_); break;
                case Wave2::Triangle: second = osc::osc2TriangleQ23 (p2_); break;
            }
        }
        last2_ = c_.wave2 == Wave2::Triangle ? sat24 ((static_cast<std::int64_t> (second) * 8) / 5) : second;

        if (c_.ring)
            second = sat24 ((static_cast<std::int64_t> (first) * second) >> 21);

        const Q23 mix = sat24 (mulSplit15 (first, invBalSplit_.hi, invBalSplit_.lo)
                               + mulSplit15 (second, balSplit_.hi, balSplit_.lo));
        const Q23 y = sat24 (mix + dc_);
        dc_ = sat24 (dc_ + mulImmediate15 (y, -1, 120, 7));
        return y;
    }

    void processBlockQ23 (Q23* out, int n, Random& random) noexcept
    {
        int i = 0;
        if (shapeMoving_ || pw2Moving_ || balMoving_ || pitchMoving_ || fmMoving_
            || modeFade_ > 0 || fm_ || c_.sync || c_.ring)
        {
            while (i < n && (shapeMoving_ || pw2Moving_ || balMoving_ || pitchMoving_ || fmMoving_
                             || modeFade_ > 0 || fm_ || c_.sync || c_.ring))
                out[i++] = processQ23 (random);
            if (i >= n)
                return;
        }

        const std::int32_t invHi = invBalSplit_.hi, invLo = invBalSplit_.lo;
        const Q23 inc1 = inc1_, shape = shape_;
        const bool osc1Only = bal_ == 0;

        while (i < n)
        {
            const int sub = std::min (n - i, 64);
            Q23 firstBuf[64];
            if (c_.wave1 == Wave1::SuperSaw)
            {
                super_.processBlockQ23 (firstBuf, sub);
            }
            else if (c_.wave1 == Wave1::Noise)
            {
                for (int k = 0; k < sub; ++k)
                {
                    p1_ = wrap24 (p1_ + inc1);
                    firstBuf[k] = noise_.processQ23 (random);
                }
            }
            else if (c_.wave1 == Wave1::FeedbackOsc)
            {
                Q23 p1 = p1_;
                for (int k = 0; k < sub; ++k)
                {
                    p1 = wrap24 (p1 + inc1);
                    firstBuf[k] = feedback_.processQ23 (p1);
                }
                p1_ = p1;
            }
            else
            {
                Q23 p1 = p1_;
                switch (c_.wave1)
                {
                    case Wave1::Saw:
                        for (int k = 0; k < sub; ++k) { p1 = wrap24 (p1 + inc1); firstBuf[k] = osc::shapedSawQ23 (p1, inc1, shape); }
                        break;
                    case Wave1::Square:
                        for (int k = 0; k < sub; ++k) { p1 = wrap24 (p1 + inc1); firstBuf[k] = osc::pulseQ23 (p1, inc1, shape); }
                        break;
                    case Wave1::Triangle:
                        for (int k = 0; k < sub; ++k) { p1 = wrap24 (p1 + inc1); firstBuf[k] = osc::shapedTriangleQ23 (p1, shape); }
                        break;
                    case Wave1::TriangleMod:
                        for (int k = 0; k < sub; ++k) { p1 = wrap24 (p1 + inc1); firstBuf[k] = osc::triangleModQ23 (p1, inc1, shape); }
                        break;
                    case Wave1::SuperSaw:
                    case Wave1::Noise:
                    case Wave1::FeedbackOsc:
                        break;
                }
                p1_ = p1;
            }

            if (osc1Only)
            {
                advanceOsc2Idle (sub);
                Q23 dc = dc_;
                for (int k = 0; k < sub; ++k)
                {
                    const Q23 mix = sat24 (mulSplit15 (firstBuf[k], invHi, invLo));
                    const Q23 y = sat24 (mix + dc);
                    dc = sat24 (dc + mulImmediate15 (y, -1, 120, 7));
                    out[i + k] = y;
                }
                dc_ = dc;
            }
            else
            {
                mixOsc2Block (firstBuf, out + i, sub);
            }
            i += sub;
        }
    }

    static bool canProcess4SuperSaw (OscillatorPair* const ops[4]) noexcept
    {
        for (int k = 0; k < 4; ++k)
        {
            const OscillatorPair* const op = ops[k];
            if (op->c_.wave1 != Wave1::SuperSaw
                || op->shapeMoving_ || op->pw2Moving_ || op->balMoving_
                || op->pitchMoving_ || op->fmMoving_ || op->modeFade_ > 0
                || op->fm_ || op->c_.sync || op->c_.ring || op->super_.isMoving())
                return false;
        }
        return true;
    }

    static void process4SuperSawBlockQ23 (OscillatorPair* const ops[4], Q23* const out[4], int n) noexcept
    {
        const bool allOsc1Only = ops[0]->bal_ == 0 && ops[1]->bal_ == 0
                                 && ops[2]->bal_ == 0 && ops[3]->bal_ == 0;
        const bool allWave2Saw = ! allOsc1Only
                                 && ops[0]->bal_ != 0 && ops[1]->bal_ != 0 && ops[2]->bal_ != 0 && ops[3]->bal_ != 0
                                 && ops[0]->c_.wave2 == Wave2::Saw && ops[1]->c_.wave2 == Wave2::Saw
                                 && ops[2]->c_.wave2 == Wave2::Saw && ops[3]->c_.wave2 == Wave2::Saw;

        const Int32x4 invHi = Int32x4::make (ops[0]->invBalSplit_.hi, ops[1]->invBalSplit_.hi, ops[2]->invBalSplit_.hi, ops[3]->invBalSplit_.hi);
        const Int32x4 invLo = Int32x4::make (ops[0]->invBalSplit_.lo, ops[1]->invBalSplit_.lo, ops[2]->invBalSplit_.lo, ops[3]->invBalSplit_.lo);
        const Int32x4 balHi = Int32x4::make (ops[0]->balSplit_.hi, ops[1]->balSplit_.hi, ops[2]->balSplit_.hi, ops[3]->balSplit_.hi);
        const Int32x4 balLo = Int32x4::make (ops[0]->balSplit_.lo, ops[1]->balSplit_.lo, ops[2]->balSplit_.lo, ops[3]->balSplit_.lo);
        const Int32x4 c120 = Int32x4::splat (120);

        SuperSaw* const supers[4] { &ops[0]->super_, &ops[1]->super_, &ops[2]->super_, &ops[3]->super_ };
        Q23* const o0 = out[0]; Q23* const o1 = out[1]; Q23* const o2 = out[2]; Q23* const o3 = out[3];

        int i = 0;
        while (i < n)
        {
            const int sub = std::min (n - i, 64);
            Q23 first[4][64];
            Q23* const firstPtrs[4] { first[0], first[1], first[2], first[3] };
            SuperSaw::process4BlockQ23 (supers, firstPtrs, sub);

            if (allOsc1Only)
            {
                for (int lane = 0; lane < 4; ++lane)
                    ops[lane]->advanceOsc2Idle (sub);
                Int32x4 dc = Int32x4::make (ops[0]->dc_, ops[1]->dc_, ops[2]->dc_, ops[3]->dc_);
                for (int k = 0; k < sub; ++k)
                {
                    const Int32x4 fVec = Int32x4::make (first[0][k], first[1][k], first[2][k], first[3][k]);
                    const Int32x4 mix = sat24x4 (mulSplit15x4 (fVec, invHi, invLo));
                    const Int32x4 y = sat24x4 (mix + dc);
                    dc = sat24x4 (dc + ((-y) >> 7) + (((y >> 7) * c120) >> 7));
                    o0[i + k] = y[0]; o1[i + k] = y[1]; o2[i + k] = y[2]; o3[i + k] = y[3];
                }
                for (int lane = 0; lane < 4; ++lane)
                    ops[lane]->dc_ = dc[lane];
            }
            else if (allWave2Saw)
            {
                Int32x4 p2 = Int32x4::make (ops[0]->p2_, ops[1]->p2_, ops[2]->p2_, ops[3]->p2_);
                const Int32x4 inc2 = Int32x4::make (ops[0]->inc2_, ops[1]->inc2_, ops[2]->inc2_, ops[3]->inc2_);
                Int32x4 dc = Int32x4::make (ops[0]->dc_, ops[1]->dc_, ops[2]->dc_, ops[3]->dc_);
                Int32x4 lastSec = Int32x4::zero();
                for (int k = 0; k < sub; ++k)
                {
                    p2 = wrap24x4 (p2 + inc2);
                    const Int32x4 sec = Int32x4::make (
                        osc::osc2SawFastQ23 (p2[0], ops[0]->osc2SawWin_),
                        osc::osc2SawFastQ23 (p2[1], ops[1]->osc2SawWin_),
                        osc::osc2SawFastQ23 (p2[2], ops[2]->osc2SawWin_),
                        osc::osc2SawFastQ23 (p2[3], ops[3]->osc2SawWin_));
                    lastSec = sec;
                    const Int32x4 fVec = Int32x4::make (first[0][k], first[1][k], first[2][k], first[3][k]);
                    const Int32x4 mix = sat24x4 (mulSplit15x4 (fVec, invHi, invLo) + mulSplit15x4 (sec, balHi, balLo));
                    const Int32x4 y = sat24x4 (mix + dc);
                    dc = sat24x4 (dc + ((-y) >> 7) + (((y >> 7) * c120) >> 7));
                    o0[i + k] = y[0]; o1[i + k] = y[1]; o2[i + k] = y[2]; o3[i + k] = y[3];
                }
                for (int lane = 0; lane < 4; ++lane)
                {
                    ops[lane]->p2_ = p2[lane];
                    ops[lane]->dc_ = dc[lane];
                    ops[lane]->last2_ = lastSec[lane];
                }
            }
            else
            {
                for (int lane = 0; lane < 4; ++lane)
                    ops[lane]->mixOsc2Block (first[lane], out[lane] + i, sub);
            }
            i += sub;
        }
    }

    double process (Random& random) noexcept { return fromQ23 (processQ23 (random)); }

private:
    TRANCERA_FORCE_INLINE void advanceOsc2Idle (int sub) noexcept
    {
        p2_ = wrap24 (static_cast<std::int32_t> (static_cast<std::int64_t> (p2_)
                                                + static_cast<std::int64_t> (sub) * inc2_));
        Q23 second = 0;
        switch (c_.wave2)
        {
            case Wave2::Square:   second = osc::pulseQ23 (p2_, inc2_, pw2_); break;
            case Wave2::Saw:      second = osc::osc2SawFastQ23 (p2_, osc2SawWin_); break;
            case Wave2::Triangle: second = osc::osc2TriangleQ23 (p2_); break;
        }
        last2_ = c_.wave2 == Wave2::Triangle ? sat24 ((static_cast<std::int64_t> (second) * 8) / 5) : second;
    }

    TRANCERA_FORCE_INLINE void mixOsc2Block (const Q23* src, Q23* dst, int sub) noexcept
    {
        const std::int32_t iHi = invBalSplit_.hi, iLo = invBalSplit_.lo;
        const std::int32_t bHi = balSplit_.hi, bLo = balSplit_.lo;
        Q23 dc = dc_;
        if (bal_ == 0)
        {
            advanceOsc2Idle (sub);
            for (int k = 0; k < sub; ++k)
            {
                const Q23 mix = sat24 (mulSplit15 (src[k], iHi, iLo));
                const Q23 y = sat24 (mix + dc);
                dc = sat24 (dc + mulImmediate15 (y, -1, 120, 7));
                dst[k] = y;
            }
            dc_ = dc;
            return;
        }

        Q23 p2 = p2_;
        const Q23 inc2 = inc2_;
        const Q23 pw2 = pw2_;
        const osc::SawWindowCoeffs& osc2Win = osc2SawWin_;
        Q23 lastSec = 0;
        switch (c_.wave2)
        {
            case Wave2::Square:
                for (int k = 0; k < sub; ++k)
                {
                    p2 = wrap24 (p2 + inc2);
                    lastSec = osc::pulseQ23 (p2, inc2, pw2);
                    const Q23 mix = sat24 (mulSplit15 (src[k], iHi, iLo) + mulSplit15 (lastSec, bHi, bLo));
                    const Q23 y = sat24 (mix + dc);
                    dc = sat24 (dc + mulImmediate15 (y, -1, 120, 7));
                    dst[k] = y;
                }
                break;
            case Wave2::Saw:
                for (int k = 0; k < sub; ++k)
                {
                    p2 = wrap24 (p2 + inc2);
                    lastSec = osc::osc2SawFastQ23 (p2, osc2Win);
                    const Q23 mix = sat24 (mulSplit15 (src[k], iHi, iLo) + mulSplit15 (lastSec, bHi, bLo));
                    const Q23 y = sat24 (mix + dc);
                    dc = sat24 (dc + mulImmediate15 (y, -1, 120, 7));
                    dst[k] = y;
                }
                break;
            case Wave2::Triangle:
                for (int k = 0; k < sub; ++k)
                {
                    p2 = wrap24 (p2 + inc2);
                    lastSec = osc::osc2TriangleQ23 (p2);
                    const Q23 mix = sat24 (mulSplit15 (src[k], iHi, iLo) + mulSplit15 (lastSec, bHi, bLo));
                    const Q23 y = sat24 (mix + dc);
                    dc = sat24 (dc + mulImmediate15 (y, -1, 120, 7));
                    dst[k] = y;
                }
                break;
        }
        p2_ = p2;
        dc_ = dc;
        last2_ = c_.wave2 == Wave2::Triangle ? sat24 ((static_cast<std::int64_t> (lastSec) * 8) / 5) : lastSec;
    }

private:
    Controls c_, prevC_;
    Q23 p1_ { 0 }, p2_ { 0 }, inc1_ { 0 }, inc2_ { 0 }, inc1Target_ { 0 }, inc2Target_ { 0 };
    osc::SawWindowCoeffs osc2SawWin_;
    Q23 bal_ { 0 }, balTarget_ { 0 };
    Coeff15 balSplit_ { splitCoeff15 (0) }, invBalSplit_ { splitCoeff15 (kQ23Max) };
    Q23 shape_ { 0 }, shapeTarget_ { 0 };
    Q23 pw2_ { 0 }, pw2Target_ { 0 };
    Q23 last2_ { 0 }, fmGain_ { 0 }, fmTarget_ { 0 }, dc_ { 0 };
    std::int64_t ratioQ16_ { 65536 }, exactRatioQ16_ { 65536 };
    int modeFade_ { 0 };
    bool fm_ { false }, shapeMoving_ { false }, pw2Moving_ { false }, balMoving_ { false };
    bool pitchMoving_ { false }, fmMoving_ { false }, freshSmooth_ { true };
    SuperSaw super_;
    NoiseOsc noise_;
    FeedbackOsc feedback_;
};
} // namespace trancera
