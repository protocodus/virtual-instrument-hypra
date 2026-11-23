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
    const Q23 abs2w = static_cast<Q23> (std::abs (static_cast<std::int64_t> (w) * 2 - kQ23One));
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
};

inline SawWindowCoeffs makeSawWindowCoeffs (Q23 inc, std::int32_t mult) noexcept
{
    const Q23 a = sat24 (-static_cast<std::int64_t> (mult) * std::abs (inc));
    const Q23 b = sat24 (static_cast<std::int64_t> (a) * 3);
    const Q23 c = sat24 (((static_cast<std::int64_t> (b) * 5) >> 1) + 786432);
    const Q23 slope = sat24 (kQ23Max + mul8 (a, 10, 5) + mul8 (b, 22, 5));
    return { kQ23Max + c, static_cast<std::int64_t> (slope) * 16 + kQ23One, hfCompQ23 (inc) };
}

inline Q23 osc2SawFastQ23 (Q23 p, const SawWindowCoeffs& w) noexcept
{
    const Q23 dist = std::max<Q23> (0, kQ23Max - std::abs (p));
    const Q23 win = sat24 (w.base + ((static_cast<std::int64_t> (dist) * w.slope) >> 23));
    const Q23 base = sat24 (mulQ23 (p >> 1, win));
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
        detuneMoving_ = mixMoving_ = false;
        fresh_ = true;
    }

    void configure (double hz, double detune, double mix) noexcept
    {
        hz_ = std::max (hz, 1.0);
        centreStep_ = static_cast<std::int32_t> (std::floor (std::clamp (hz_ / kEngineRate, 0.0, 0.499) * 16777216.0));
        detuneTarget_ = toQ23 (mapping::superDetuneReg (detune));
        mixTarget_ = toQ23 (mapping::superMixSide (mix));
        const double inc = std::clamp (2.0 * hz_ / kEngineRate, 1.0e-5, 0.99);
        fQ23_ = toQ23 (std::min (0.99, 1.53125 * inc));
        fSplit_ = splitCoeff15 (fQ23_);
        if (fresh_)
        {
            detuneCur_ = detuneTarget_;
            mixCur_ = mixTarget_;
            updateMixCache();
            detuneMoving_ = mixMoving_ = false;
            fresh_ = false;
        }
        else
        {
            detuneMoving_ = detuneCur_ != detuneTarget_;
            mixMoving_ = mixCur_ != mixTarget_;
        }
        updateSteps (detuneCur_);
    }

    Q23 processQ23() noexcept
    {
        if (detuneMoving_)
        {
            const Q23 nextD = sat24 (detuneCur_ + mul8 (detuneTarget_ - detuneCur_, 112, 7));
            if (nextD == detuneCur_)
                detuneMoving_ = false;
            else
            {
                detuneCur_ = nextD;
                updateSteps (detuneCur_);
            }
        }
        if (mixMoving_)
        {
            const Q23 nextM = sat24 (mixCur_ + mul8 (mixTarget_ - mixCur_, 112, 7));
            if (nextM == mixCur_)
                mixMoving_ = false;
            else
            {
                mixCur_ = nextM;
                updateMixCache();
            }
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

    double process() noexcept { return fromQ23 (processQ23()); }

    bool wrapped() const noexcept { return wrapped_; }
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
    std::int32_t centreStep_ { 0 }, mix8_ { 0 };
    Q23 detuneCur_ { 0 }, detuneTarget_ { 0 };
    Q23 mixCur_ { 0 }, mixTarget_ { 0 }, fQ23_ { 8192 };
    Coeff15 fSplit_ { splitCoeff15 (8192) }, attenSplit_ { splitCoeff15 (kQ23Max) };
    Q23 low1_ { 0 }, band1_ { 0 }, low2_ { 0 }, band2_ { 0 };
    bool wrapped_ { false }, detuneMoving_ { false }, mixMoving_ { false }, fresh_ { true };
};

class NoiseOsc
{
public:
    void reset() noexcept
    {
        s1_ = s2_ = low1_ = band1_ = low2_ = band2_ = 0;
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
            const Q23 nextF = sat24 (f_ + ((fTarget_ - f_) >> 11));
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
            const Q23 nextQ = sat24 (qHalf_ + ((qHalfTarget_ - qHalf_) >> 11));
            if (nextQ == qHalf_)
                qMoving_ = false;
            else
            {
                qHalf_ = nextQ;
                qc_ = splitCoeff15 (qHalf_);
            }
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
        d1Q16_ += (d1TargetQ16_ - d1Q16_) >> 7;
        h_ = sat24 (h_ + ((hTarget_ - h_) >> 11));
        amt_ = sat24 (amt_ + ((amtTarget_ - amt_) >> 11));

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
            shapeMoving_ = pw2Moving_ = balMoving_ = false;
            freshSmooth_ = true;
        }
        if (wave == Wave1::FeedbackOsc)
            feedback_.retrigger();
    }

    void configure (const Controls& c) noexcept
    {
        c_ = c;
        inc1_ = toQ23 (std::clamp (2.0 * c.hz1 / kEngineRate, -0.998, 0.998));
        inc2_ = toQ23 (std::clamp (2.0 * c.hz2 / kEngineRate, -0.998, 0.998));
        osc2SawWin_ = osc::makeSawWindowCoeffs (inc2_, 4);
        ratioQ16_ = static_cast<std::int64_t> (std::clamp (c.hz2 / std::max (c.hz1, 1.0), 0.0, 2000.0) * 65536.0);
        if (c.wave1 == Wave1::SuperSaw)
            super_.configure (c.hz1, c.ctrl1, c.ctrl2);
        else if (c.wave1 == Wave1::Noise)
            noise_.configure (c.hz1, c.ctrl1, c.ctrl2);
        else if (c.wave1 == Wave1::FeedbackOsc)
            feedback_.configure (c.hz1, c.ctrl1, c.ctrl2);

        const double st = c.wave1 == Wave1::TriangleMod ? mapping::triangleModOffset (c.ctrl1)
                        : c.wave1 == Wave1::Square      ? mapping::pulseWidth (c.ctrl1)
                                                        : mapping::waveShape (c.ctrl1);
        shapeTarget_ = toQ23 (st);
        pw2Target_ = toQ23 (mapping::pulseWidth (c.width2));
        balTarget_ = sat24 (toQ23 (std::clamp (c.gain2, 0.0, 1.0)));
        if (freshSmooth_)
        {
            shape_ = shapeTarget_;
            pw2_ = pw2Target_;
            bal_ = balTarget_;
            balSplit_ = splitCoeff15 (bal_);
            invBalSplit_ = splitCoeff15 (kQ23Max - bal_);
            shapeMoving_ = pw2Moving_ = balMoving_ = false;
            freshSmooth_ = false;
        }
        else
        {
            shapeMoving_ = shape_ != shapeTarget_;
            pw2Moving_ = pw2_ != pw2Target_;
            balMoving_ = bal_ != balTarget_;
        }

        fmGain_ = toQ23 (mapping::crossModGain (c.xmod));
        fm_ = c.xmod > 0.0 && c.wave1 != Wave1::SuperSaw && c.wave1 != Wave1::Noise;
    }

    Q23 processQ23 (Random& random) noexcept
    {
        if (shapeMoving_)
        {
            const Q23 nextShape = sat24 (shape_ + ((shapeTarget_ - shape_) >> 11));
            if (nextShape == shape_)
                shapeMoving_ = false;
            else
                shape_ = nextShape;
        }
        if (pw2Moving_)
        {
            const Q23 nextPw = sat24 (pw2_ + ((pw2Target_ - pw2_) >> 11));
            if (nextPw == pw2_)
                pw2Moving_ = false;
            else
                pw2_ = nextPw;
        }
        if (balMoving_)
        {
            const Q23 nextBal = sat24 (bal_ + ((balTarget_ - bal_) >> 6));
            if (nextBal == bal_)
                balMoving_ = false;
            else
            {
                bal_ = nextBal;
                balSplit_ = splitCoeff15 (bal_);
                invBalSplit_ = splitCoeff15 (kQ23Max - bal_);
            }
        }

        Q23 first = 0;
        Q23 masterPhase = 0;
        const Q23 inc1Eff = fm_ ? std::clamp<Q23> (inc1_ + mulQ23 (fmGain_, last2_), -8371830, 8371830) : inc1_;

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

    double process (Random& random) noexcept { return fromQ23 (processQ23 (random)); }

private:
    Controls c_;
    Q23 p1_ { 0 }, p2_ { 0 }, inc1_ { 0 }, inc2_ { 0 };
    osc::SawWindowCoeffs osc2SawWin_;
    Q23 bal_ { 0 }, balTarget_ { 0 };
    Coeff15 balSplit_ { splitCoeff15 (0) }, invBalSplit_ { splitCoeff15 (kQ23Max) };
    Q23 shape_ { 0 }, shapeTarget_ { 0 };
    Q23 pw2_ { 0 }, pw2Target_ { 0 };
    Q23 last2_ { 0 }, fmGain_ { 0 }, dc_ { 0 };
    std::int64_t ratioQ16_ { 65536 };
    bool fm_ { false }, shapeMoving_ { false }, pw2Moving_ { false }, balMoving_ { false }, freshSmooth_ { true };
    SuperSaw super_;
    NoiseOsc noise_;
    FeedbackOsc feedback_;
};
} // namespace trancera
