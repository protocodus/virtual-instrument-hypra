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
        const double cutoff = std::min ({ 0.5 * hostRate - 700.0, 40000.0 }) / kEngineRate;
        std::vector<float> rows (static_cast<std::size_t> ((phases + 1) * taps));
        for (int phase = 0; phase <= phases; ++phase)
        {
            double sum = 0.0;
            float* row = rows.data() + phase * taps;
            for (int i = 0; i < taps; ++i)
            {
                const double x = i - (half - 1) - static_cast<double> (phase) / phases;
                const double u = std::clamp (x / half, -1.0, 1.0);
                const double angle = kTwoPi * cutoff * x;
                const double sinc = std::abs (angle) < 1.0e-9 ? 1.0 : std::sin (angle) / angle;
                const double v = sinc * bessel (beta * std::sqrt (1.0 - u * u));
                row[i] = static_cast<float> (v);
                sum += v;
            }
            for (int i = 0; i < taps; ++i)
                row[i] = static_cast<float> (row[i] / sum);
        }
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
        reset();
    }

    void reset() noexcept
    {
        history_.fill (Double2::make (0.0, 0.0));
        position_ = 0.0;
        write_ = 0;
    }

    template <class Source>
    Stereo process (Source&& source) noexcept
    {
        position_ += step_;
        while (position_ >= 1.0)
        {
            position_ -= 1.0;
            const Stereo s = source();
            history_[write_] = history_[write_ + taps] = Double2::make (s.l, s.r);
            write_ = (write_ + 1) & (taps - 1);
        }
        const double at = position_ * phases;
        const int phase = std::min (static_cast<int> (at), phases - 1);
        const float f32 = static_cast<float> (at - phase);
        const float* const a = aTable_.data() + phase * taps;
        const float* const d = dTable_.data() + phase * taps;
        const Double2* const h = history_.data() + write_;
#if defined(__clang__) || defined(__GNUC__)
        Double2::Lanes s0 { 0.0, 0.0 }, s1 { 0.0, 0.0 }, s2 { 0.0, 0.0 }, s3 { 0.0, 0.0 };
        for (int i = 0; i < taps; i += 4)
        {
            s0 += static_cast<double> (a[i]     + f32 * d[i])     * h[i].v;
            s1 += static_cast<double> (a[i + 1] + f32 * d[i + 1]) * h[i + 1].v;
            s2 += static_cast<double> (a[i + 2] + f32 * d[i + 2]) * h[i + 2].v;
            s3 += static_cast<double> (a[i + 3] + f32 * d[i + 3]) * h[i + 3].v;
        }
        const Double2::Lanes out = (s0 + s1) + (s2 + s3);
        return { out[0], out[1] };
#else
        double outL = 0.0, outR = 0.0;
        for (int i = 0; i < taps; ++i)
        {
            const double w = static_cast<double> (a[i] + f32 * d[i]);
            outL += w * h[i][0];
            outR += w * h[i][1];
        }
        return { outL, outR };
#endif
    }

private:
    static constexpr int taps = 128, half = taps / 2, phases = 512;
    static constexpr double beta = 8.0;

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

    std::vector<float> aTable_, dTable_;
    std::array<Double2, 2 * taps> history_ {};
    double position_ { 0.0 }, step_ { 1.0 };
    int write_ { 0 };
};
} // namespace trancera
