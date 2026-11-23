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
        table_.resize (static_cast<std::size_t> ((phases + 1) * taps));
        for (int phase = 0; phase <= phases; ++phase)
        {
            double sum = 0.0;
            float* row = table_.data() + phase * taps;
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
        reset();
    }

    void reset() noexcept
    {
        history_.fill ({});
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
            history_[write_] = history_[write_ + taps] = s;
            write_ = (write_ + 1) % taps;
        }
        const double at = position_ * phases;
        const int phase = std::min (static_cast<int> (at), phases - 1);
        const double f = at - phase;
        const float* a = table_.data() + phase * taps;
        const float* b = a + taps;
        const Stereo* h = history_.data() + write_;
        Stereo out;
        for (int i = 0; i < taps; ++i)
        {
            const double w = a[i] + f * (b[i] - a[i]);
            out.l += w * h[i].l;
            out.r += w * h[i].r;
        }
        return out;
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

    std::vector<float> table_;
    std::array<Stereo, 2 * taps> history_ {};
    double position_ { 0.0 }, step_ { 1.0 };
    int write_ { 0 };
};
} // namespace trancera
