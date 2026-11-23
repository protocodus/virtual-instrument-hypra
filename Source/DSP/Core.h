// Shared numeric helpers. Everything in Source/DSP is C++17, JUCE-free and
// free of allocation, locks and I/O once Engine::prepare has run.
#pragma once

#include <algorithm>
#include <cmath>
#include <cstdint>

namespace trancera
{
inline constexpr double kPi = 3.14159265358979323846;
inline constexpr double kTwoPi = 2.0 * kPi;

// Direct internal sample-rate math constants and envelope control rate.
inline constexpr double kEngineRate = 67737600.0 / 768.0;
inline constexpr double kEnvelopeRate = 100.0;
inline constexpr int kEnvelopeInterval = 882;

inline constexpr int kControlInterval = 32;
inline constexpr double kControlRate = kEngineRate / kControlInterval;

// Hard fixed-point rail saturation at [-1, +1].
inline double sat (double x) noexcept { return std::clamp (x, -1.0, 1.0); }

// Wraps a bipolar signal or phase into [-1.0, 1.0).
inline double wrap2 (double x) noexcept
{
    return x - 2.0 * std::floor (0.5 * (x + 1.0));
}

struct Stereo
{
    double l { 0.0 }, r { 0.0 };
    friend Stereo operator+ (Stereo a, Stereo b) noexcept { return { a.l + b.l, a.r + b.r }; }
    friend Stereo operator- (Stereo a, Stereo b) noexcept { return { a.l - b.l, a.r - b.r }; }
    friend Stereo operator* (double k, Stereo a) noexcept { return { k * a.l, k * a.r }; }
    Stereo& operator+= (Stereo b) noexcept { l += b.l; r += b.r; return *this; }
};

inline double unit (int value) noexcept { return std::clamp (value, 0, 127) / 127.0; }

// -64..+63, with both ends reaching full depth.
inline double bipolar (int value) noexcept
{
    const int v = std::clamp (value, -64, 63);
    return v < 0 ? v / 64.0 : v / 63.0;
}

// Bounded, smooth and exact at zero; used only inside feedback loops.
inline double softClip (double x) noexcept
{
    x = std::clamp (x, -3.0, 3.0);
    return x * (27.0 + x * x) / (27.0 + 9.0 * x * x);
}

inline double onePole (double hz, double rate = kEngineRate) noexcept
{
    return 1.0 - std::exp (-kTwoPi * hz / rate);
}

class Random
{
public:
    Random() noexcept : Random (1u) {}
    explicit Random (std::uint32_t seed) noexcept : state_ (seed ? seed : 1u) {}

    std::uint32_t next() noexcept
    {
        state_ ^= state_ << 13;
        state_ ^= state_ >> 17;
        state_ ^= state_ << 5;
        return state_;
    }

    double bipolarValue() noexcept { return next() * (1.0 / 2147483648.0) - 1.0; }

private:
    std::uint32_t state_;
};
} // namespace trancera
