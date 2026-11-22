// Shared numeric helpers. Everything in Source/DSP is C++17, JUCE-free and
// free of allocation, locks and I/O once Engine::prepare has run.
#pragma once

#include <algorithm>
#include <cmath>
#include <cstdint>

namespace hypra
{
inline constexpr double kPi = 3.14159265358979323846;
inline constexpr double kTwoPi = 2.0 * kPi;

// Service Notes JP-8080 (1998) p.13 and p.19: the 67.7376 MHz crystal is
// divided by 768 into "SYNC = 2Fs = 88.2 kHz" for every TC170C140 DSP and
// into the DAC word clock. The JP-8000 has the same crystal, DSP and DAC.
inline constexpr double kEngineRate = 67737600.0 / 768.0;

// [inferred] How often the modulation reaches a voice. The hardware's rate
// is not published; 32 samples is 0.36 ms.
inline constexpr int kControlInterval = 32;
inline constexpr double kControlRate = kEngineRate / kControlInterval;

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
} // namespace hypra
