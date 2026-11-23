// Transport from the 88.2 kHz model to the host rate. Not hardware.
#pragma once

#include "Core.h"

#include <array>
#include <vector>

namespace trancera
{
class Resampler
{
public:
    void prepare (double hostRate)
    {
        step_ = kEngineRate / hostRate;
        passthrough_ = fastAbs (hostRate - kEngineRate) < 1.0e-3;
        rational_ = false;
        ratNum_ = 1;
        ratDen_ = 1;
        if (! passthrough_)
        {
            static constexpr std::array<std::pair<int, int>, 4> exactRatios { {
                { 2, 1 },     // 44100 Hz
                { 147, 80 },  // 48000 Hz
                { 147, 160 }, // 96000 Hz
                { 147, 320 }  // 192000 Hz
            } };
            for (const auto& [num, den] : exactRatios)
            {
                if (fastAbs (hostRate - kEngineRate * static_cast<double> (den) / static_cast<double> (num)) < 1.0e-3)
                {
                    rational_ = true;
                    ratNum_ = num;
                    ratDen_ = den;
                    break;
                }
            }
        }

        const double cutoff = std::min ({ 0.5 * hostRate - 700.0, 40000.0 }) / kEngineRate;
        const auto buildRow = [cutoff] (double frac, float* row) noexcept
        {
            double sum = 0.0;
            for (int i = 0; i < taps; ++i)
            {
                const double x = i - (half - 1) - frac;
                const double u = std::clamp (x / half, -1.0, 1.0);
                const double angle = kTwoPi * cutoff * x;
                const double sinc = fastAbs (angle) < 1.0e-9 ? 1.0 : std::sin (angle) / angle;
                const double v = sinc * bessel (beta * fastSqrt (1.0 - u * u));
                row[i] = static_cast<float> (v);
                sum += v;
            }
            const double invSum = 1.0 / sum;
            for (int i = 0; i < taps; ++i)
                row[i] = static_cast<float> (row[i] * invSum);
        };

        if (rational_)
        {
            ratTable_.resize (static_cast<std::size_t> (ratDen_ * taps));
            for (int phase = 0; phase < ratDen_; ++phase)
                buildRow (static_cast<double> (phase) / static_cast<double> (ratDen_), ratTable_.data() + phase * taps);
        }
        else if (! passthrough_)
        {
            std::vector<float> rows (static_cast<std::size_t> ((phases + 1) * taps));
            for (int phase = 0; phase <= phases; ++phase)
                buildRow (static_cast<double> (phase) / phases, rows.data() + phase * taps);
            aTable_.resize (static_cast<std::size_t> (phases * taps));
            dTable_.resize (static_cast<std::size_t> (phases * taps));
            for (int phase = 0; phase < phases; ++phase)
            {
                const float* const r0 = rows.data() + phase * taps;
                const float* const r1 = r0 + taps;
                float* const aRow = aTable_.data() + phase * taps;
                float* const dRow = dTable_.data() + phase * taps;
                for (int i = 0; i < taps; ++i)
                {
                    aRow[i] = r0[i];
                    dRow[i] = r1[i] - r0[i];
                }
            }
        }
        reset();
    }

    void reset() noexcept
    {
        historyF_.fill (0.0f);
        position_ = 0.0;
        ratPhase_ = 0;
        write_ = 0;
    }

    bool isPassthrough() const noexcept { return passthrough_; }

    int ticksNeededForFrames (int frames) const noexcept
    {
        if (passthrough_)
            return frames;
        if (rational_)
        {
            const std::int64_t total = static_cast<std::int64_t> (ratPhase_)
                                     + static_cast<std::int64_t> (frames) * ratNum_;
            return static_cast<int> (total / ratDen_);
        }
        double p = position_;
        int needed = 0;
        for (int i = 0; i < frames; ++i)
        {
            p += step_;
            while (p >= 1.0)
            {
                p -= 1.0;
                ++needed;
            }
        }
        return needed;
    }

    int advanceIdle (int frames) noexcept
    {
        if (passthrough_)
            return frames;
        if (rational_)
        {
            const std::int64_t total = static_cast<std::int64_t> (ratPhase_)
                                     + static_cast<std::int64_t> (frames) * ratNum_;
            ratPhase_ = static_cast<int> (total % ratDen_);
            return static_cast<int> (total / ratDen_);
        }
        int needed = 0;
        for (int i = 0; i < frames; ++i)
        {
            position_ += step_;
            while (position_ >= 1.0)
            {
                position_ -= 1.0;
                ++needed;
            }
        }
        return needed;
    }

    template <class Source>
    Stereo process (Source&& source) noexcept
    {
        if (passthrough_)
            return source();
        if (rational_)
        {
            ratPhase_ += ratNum_;
            while (ratPhase_ >= ratDen_)
            {
                ratPhase_ -= ratDen_;
                pushSample (source());
            }
            return convolveRow (ratTable_.data() + ratPhase_ * taps);
        }
        position_ += step_;
        while (position_ >= 1.0)
        {
            position_ -= 1.0;
            pushSample (source());
        }
        return convolveInterpolated();
    }

    void processBlockFromTicks (const Stereo* ticks, float* outL, float* outR, int frames, double gain) noexcept
    {
        int ti = 0;
        if (rational_)
        {
            const float* const table = ratTable_.data();
            int phase = ratPhase_;
            const int num = ratNum_, den = ratDen_;
            for (int f = 0; f < frames; ++f)
            {
                phase += num;
                while (phase >= den)
                {
                    phase -= den;
                    pushSample (ticks[ti++]);
                }
                const Stereo out = convolveRow (table + phase * taps);
                outL[f] = static_cast<float> (gain * out.l);
                outR[f] = static_cast<float> (gain * out.r);
            }
            ratPhase_ = phase;
            return;
        }
        for (int f = 0; f < frames; ++f)
        {
            position_ += step_;
            while (position_ >= 1.0)
            {
                position_ -= 1.0;
                pushSample (ticks[ti++]);
            }
            const Stereo out = convolveInterpolated();
            outL[f] = static_cast<float> (gain * out.l);
            outR[f] = static_cast<float> (gain * out.r);
        }
    }

private:
    static constexpr int taps = 128, half = taps / 2, phases = 512;
    static constexpr double beta = 8.0;

    TRANCERA_FORCE_INLINE void pushSample (Stereo s) noexcept
    {
        const int w2 = write_ * 2;
        const float fl = static_cast<float> (s.l);
        const float fr = static_cast<float> (s.r);
        historyF_[static_cast<std::size_t> (w2)] = fl;
        historyF_[static_cast<std::size_t> (w2 + 1)] = fr;
        historyF_[static_cast<std::size_t> (w2 + 2 * taps)] = fl;
        historyF_[static_cast<std::size_t> (w2 + 2 * taps + 1)] = fr;
        write_ = (write_ + 1) & (taps - 1);
    }

    TRANCERA_FORCE_INLINE Stereo convolveRow (const float* const w) const noexcept
    {
        const float* const h = historyF_.data() + write_ * 2;
        Float4 s0 = Float4::zero(), s1 = Float4::zero(), s2 = Float4::zero(), s3 = Float4::zero();
        for (int i = 0; i < taps; i += 8)
        {
            s0 = Float4::mulAdd (s0, Float4::make (w[i],     w[i],     w[i + 1], w[i + 1]), Float4::load (h + 2 * i));
            s1 = Float4::mulAdd (s1, Float4::make (w[i + 2], w[i + 2], w[i + 3], w[i + 3]), Float4::load (h + 2 * i + 4));
            s2 = Float4::mulAdd (s2, Float4::make (w[i + 4], w[i + 4], w[i + 5], w[i + 5]), Float4::load (h + 2 * i + 8));
            s3 = Float4::mulAdd (s3, Float4::make (w[i + 6], w[i + 6], w[i + 7], w[i + 7]), Float4::load (h + 2 * i + 12));
        }
        const Float4 sum = (s0 + s1) + (s2 + s3);
        return { static_cast<double> (sum[0] + sum[2]), static_cast<double> (sum[1] + sum[3]) };
    }

    TRANCERA_FORCE_INLINE Stereo convolveInterpolated() const noexcept
    {
        const double at = position_ * phases;
        const int phase = std::min (static_cast<int> (at), phases - 1);
        const float f32 = static_cast<float> (at - phase);
        const float* const a = aTable_.data() + phase * taps;
        const float* const d = dTable_.data() + phase * taps;
        const float* const h = historyF_.data() + write_ * 2;
        Float4 s0 = Float4::zero(), s1 = Float4::zero(), s2 = Float4::zero(), s3 = Float4::zero();
        for (int i = 0; i < taps; i += 8)
        {
            const float w0 = a[i]     + f32 * d[i];
            const float w1 = a[i + 1] + f32 * d[i + 1];
            const float w2 = a[i + 2] + f32 * d[i + 2];
            const float w3 = a[i + 3] + f32 * d[i + 3];
            const float w4 = a[i + 4] + f32 * d[i + 4];
            const float w5 = a[i + 5] + f32 * d[i + 5];
            const float w6 = a[i + 6] + f32 * d[i + 6];
            const float w7 = a[i + 7] + f32 * d[i + 7];
            s0 = Float4::mulAdd (s0, Float4::make (w0, w0, w1, w1), Float4::load (h + 2 * i));
            s1 = Float4::mulAdd (s1, Float4::make (w2, w2, w3, w3), Float4::load (h + 2 * i + 4));
            s2 = Float4::mulAdd (s2, Float4::make (w4, w4, w5, w5), Float4::load (h + 2 * i + 8));
            s3 = Float4::mulAdd (s3, Float4::make (w6, w6, w7, w7), Float4::load (h + 2 * i + 12));
        }
        const Float4 sum = (s0 + s1) + (s2 + s3);
        return { static_cast<double> (sum[0] + sum[2]), static_cast<double> (sum[1] + sum[3]) };
    }

    static double bessel (double x) noexcept
    {
        double sum = 1.0, term = 1.0;
        for (int k = 1; k < 24; ++k)
        {
            term *= x * x / (4.0 * k * k);
            sum += term;
        }
        return sum;
    }

    std::vector<float> aTable_, dTable_, ratTable_;
    std::array<float, 4 * taps> historyF_ {};
    double position_ { 0.0 }, step_ { 1.0 };
    int ratPhase_ { 0 }, ratNum_ { 1 }, ratDen_ { 1 };
    int write_ { 0 };
    bool passthrough_ { false }, rational_ { false };
};
} // namespace trancera
