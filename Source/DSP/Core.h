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

#if defined(__clang__) || defined(__GNUC__)
struct Double2
{
    typedef double Lanes __attribute__((vector_size (16)));
    Lanes v;

    static constexpr Double2 make (double l, double r) noexcept { return { { l, r } }; }
    double operator[] (int lane) const noexcept { return v[lane]; }
} __attribute__((packed, aligned (8)));
static_assert (alignof (Double2) == 8 && sizeof (Double2) == 16, "unaligned 128-bit SIMD vector");
#else
struct Double2
{
    double v[2];

    static constexpr Double2 make (double l, double r) noexcept { return { { l, r } }; }
    double operator[] (int lane) const noexcept { return v[lane]; }
};
#endif

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

// Signed 24-bit Q23 fixed-point word representing [-1.0, +1.0 - 2^-23].
using Q23 = std::int32_t;
inline constexpr Q23 kQ23One = 1 << 23;
inline constexpr Q23 kQ23Max = kQ23One - 1;
inline constexpr Q23 kQ23Min = -kQ23One;

struct StereoQ23
{
    Q23 l { 0 }, r { 0 };
    friend constexpr StereoQ23 operator+ (StereoQ23 a, StereoQ23 b) noexcept { return { a.l + b.l, a.r + b.r }; }
    friend constexpr StereoQ23 operator- (StereoQ23 a, StereoQ23 b) noexcept { return { a.l - b.l, a.r - b.r }; }
};

inline constexpr Q23 sat24 (std::int64_t x) noexcept
{
    return x > kQ23Max ? kQ23Max : x < kQ23Min ? kQ23Min : static_cast<Q23> (x);
}

inline constexpr Q23 sat24 (std::int32_t x) noexcept
{
    return x > kQ23Max ? kQ23Max : x < kQ23Min ? kQ23Min : x;
}

// Wraps an integer accumulator to signed 24-bit range [-2^23, 2^23 - 1].
inline constexpr Q23 wrap24 (std::int32_t x) noexcept
{
    const std::uint32_t u = static_cast<std::uint32_t> (x) << 8;
    return static_cast<Q23> (static_cast<std::int32_t> (u) >> 8);
}

inline Q23 toQ23 (double x) noexcept
{
    return sat24 (static_cast<std::int64_t> (std::floor (std::clamp (x, -2.0, 2.0) * static_cast<double> (kQ23One))));
}

inline constexpr double fromQ23 (Q23 x) noexcept
{
    return static_cast<double> (x) * (1.0 / static_cast<double> (kQ23One));
}

// Floor-truncating Q23 multiplies matching 24-bit fixed-point accumulator scaling.
inline constexpr Q23 mulQ23 (Q23 a, Q23 b) noexcept
{
    return static_cast<Q23> ((static_cast<std::int64_t> (a) * b) >> 23);
}

inline constexpr Q23 mulQ23x2 (Q23 a, Q23 b) noexcept
{
    return static_cast<Q23> ((static_cast<std::int64_t> (a) * b) >> 22);
}

inline constexpr Q23 mul8 (Q23 a, std::int32_t k8, int shift) noexcept
{
    return (a * k8) >> shift;
}

// Pre-split 8+7-bit coefficient pair for repeated per-sample multiplies.
struct Coeff15
{
    std::int32_t hi { 0 };
    std::int32_t lo { 0 };
};

inline constexpr Coeff15 splitCoeff15 (Q23 c) noexcept
{
    return { c >> 16, (c >> 9) & 0x7f };
}

inline constexpr Coeff15 splitTable15 (std::int32_t q15) noexcept
{
    return { q15 >> 8, (q15 >> 1) & 0x7f };
}

inline constexpr Q23 mulSplit15 (Q23 a, std::int32_t hi8, std::int32_t lo7) noexcept
{
    return ((a * hi8) >> 7) + (((a >> 7) * lo7) >> 7);
}

inline constexpr Q23 mulSplit15x2 (Q23 a, std::int32_t hi8, std::int32_t lo7) noexcept
{
    return ((a * hi8) >> 6) + (((a >> 7) * lo7) >> 6);
}

// Two-pass 8x7-bit coefficient multiply equivalent to a 15-bit interpolated gain.
inline constexpr Q23 mulCoeff15 (Q23 a, Q23 c) noexcept
{
    return mulSplit15 (a, c >> 16, (c >> 9) & 0x7f);
}

inline constexpr Q23 mulCoeff15x2 (Q23 a, Q23 c) noexcept
{
    return mulSplit15x2 (a, c >> 16, (c >> 9) & 0x7f);
}

inline constexpr Q23 mulImmediate15 (Q23 a, std::int32_t hi8, std::int32_t lo7, int shift) noexcept
{
    return ((a * hi8) >> shift) + (((a >> 7) * lo7) >> shift);
}

// Compands a signed 24-bit sample into a 14-bit mantissa + 2-bit exponent word.
inline constexpr std::uint16_t packDelay16 (Q23 v) noexcept
{
    v = sat24 (v);
    for (unsigned exponent = 0; exponent < 3; ++exponent)
    {
        const std::int32_t mantissa = v >> (4 + 2 * static_cast<int> (exponent));
        if (mantissa >= -8192 && mantissa <= 8191)
            return static_cast<std::uint16_t> ((static_cast<std::uint32_t> (mantissa) & 0x3FFFu) | (exponent << 14));
    }
    const std::int32_t mantissa = std::clamp (v >> 10, -8192, 8191);
    return static_cast<std::uint16_t> ((static_cast<std::uint32_t> (mantissa) & 0x3FFFu) | (3u << 14));
}

inline constexpr Q23 unpackDelay16 (std::uint16_t code) noexcept
{
    const std::int32_t raw = static_cast<std::int32_t> (code & 0x3FFFu);
    const std::int32_t mantissa = (raw & 0x2000) != 0 ? (raw - 0x4000) : raw;
    const int shift = 4 + 2 * static_cast<int> (code >> 14);
    return static_cast<Q23> (static_cast<std::int64_t> (mantissa) * (1 << shift));
}
} // namespace trancera
