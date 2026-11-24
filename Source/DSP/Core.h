// Shared numeric helpers. Everything in Source/DSP is C++17, JUCE-free and
// free of allocation, locks and I/O once Engine::prepare has run.
#pragma once

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstring>

namespace trancera
{
#if defined(__clang__) || defined(__GNUC__)
#define TRANCERA_FORCE_INLINE __attribute__((always_inline)) inline
TRANCERA_FORCE_INLINE double fastAbs (double x) noexcept { return __builtin_fabs (x); }
TRANCERA_FORCE_INLINE double fastFloor (double x) noexcept { return __builtin_floor (x); }
TRANCERA_FORCE_INLINE double fastSqrt (double x) noexcept
{
#if defined(__has_builtin) && __has_builtin(__builtin_elementwise_sqrt)
    return __builtin_elementwise_sqrt (x);
#else
    return __builtin_sqrt (x);
#endif
}
#else
#define TRANCERA_FORCE_INLINE inline
inline double fastAbs (double x) noexcept { return std::abs (x); }
inline double fastFloor (double x) noexcept { return std::floor (x); }
inline double fastSqrt (double x) noexcept { return std::sqrt (x); }
#endif

inline constexpr std::int64_t abs64 (std::int64_t x) noexcept { return x < 0 ? -x : x; }

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

    static constexpr Double2 zero() noexcept { return { { 0.0, 0.0 } }; }
    static constexpr Double2 splat (double x) noexcept { return { { x, x } }; }
    static constexpr Double2 make (double l, double r) noexcept { return { { l, r } }; }
    double operator[] (int lane) const noexcept { return v[lane]; }
    friend Double2 operator+ (Double2 a, Double2 b) noexcept { return { a.v + b.v }; }
    friend Double2 operator- (Double2 a, Double2 b) noexcept { return { a.v - b.v }; }
    friend Double2 operator* (double k, Double2 a) noexcept { return { k * a.v }; }
    Double2& operator+= (Double2 b) noexcept { v += b.v; return *this; }
    static TRANCERA_FORCE_INLINE Double2 mulAdd (Double2 addend, double k, Double2 x) noexcept { return { addend.v + k * x.v }; }
    static TRANCERA_FORCE_INLINE Double2 mulSub (Double2 minuend, double k, Double2 x) noexcept { return { minuend.v - k * x.v }; }
} __attribute__((packed, aligned (8)));
static_assert (alignof (Double2) == 8 && sizeof (Double2) == 16, "unaligned 128-bit SIMD vector");

struct Int32x4
{
    typedef std::int32_t Lanes __attribute__((vector_size (16)));
    Lanes v;

    static constexpr Int32x4 zero() noexcept { return { { 0, 0, 0, 0 } }; }
    static constexpr Int32x4 splat (std::int32_t x) noexcept { return { { x, x, x, x } }; }
    static constexpr Int32x4 make (std::int32_t a, std::int32_t b, std::int32_t c, std::int32_t d) noexcept
    {
        return { { a, b, c, d } };
    }
    std::int32_t operator[] (int lane) const noexcept { return v[lane]; }
    friend Int32x4 operator+ (Int32x4 a, Int32x4 b) noexcept { return { a.v + b.v }; }
    friend Int32x4 operator- (Int32x4 a) noexcept { return { -a.v }; }
    friend Int32x4 operator- (Int32x4 a, Int32x4 b) noexcept { return { a.v - b.v }; }
    friend Int32x4 operator* (Int32x4 a, Int32x4 b) noexcept { return { a.v * b.v }; }
    friend Int32x4 operator>> (Int32x4 a, int s) noexcept { return { a.v >> s }; }
    friend Int32x4 operator& (Int32x4 a, Int32x4 b) noexcept { return { a.v & b.v }; }
} __attribute__((packed, aligned (8)));
static_assert (alignof (Int32x4) == 8 && sizeof (Int32x4) == 16, "unaligned 128-bit SIMD vector");

struct Float4
{
    typedef float Lanes __attribute__((vector_size (16)));
    Lanes v;

    static constexpr Float4 zero() noexcept { return { { 0.0f, 0.0f, 0.0f, 0.0f } }; }
    static constexpr Float4 splat (float x) noexcept { return { { x, x, x, x } }; }
    static constexpr Float4 make (float a, float b, float c, float d) noexcept { return { { a, b, c, d } }; }
    static TRANCERA_FORCE_INLINE Float4 load (const float* ptr) noexcept
    {
        Lanes r;
        std::memcpy (&r, ptr, sizeof (r));
        return { r };
    }
    float operator[] (int lane) const noexcept { return v[lane]; }
    friend Float4 operator+ (Float4 a, Float4 b) noexcept { return { a.v + b.v }; }
    friend Float4 operator* (Float4 a, Float4 b) noexcept { return { a.v * b.v }; }
    Float4& operator+= (Float4 b) noexcept { v += b.v; return *this; }
    static TRANCERA_FORCE_INLINE Float4 mulAdd (Float4 addend, Float4 a, Float4 b) noexcept { return { addend.v + a.v * b.v }; }
} __attribute__((packed, aligned (8)));
static_assert (alignof (Float4) == 8 && sizeof (Float4) == 16, "unaligned 128-bit SIMD vector");
#else
struct Double2
{
    double v[2];

    static constexpr Double2 zero() noexcept { return { { 0.0, 0.0 } }; }
    static constexpr Double2 splat (double x) noexcept { return { { x, x } }; }
    static constexpr Double2 make (double l, double r) noexcept { return { { l, r } }; }
    double operator[] (int lane) const noexcept { return v[lane]; }
    friend Double2 operator+ (Double2 a, Double2 b) noexcept { return make (a[0] + b[0], a[1] + b[1]); }
    friend Double2 operator- (Double2 a, Double2 b) noexcept { return make (a[0] - b[0], a[1] - b[1]); }
    friend Double2 operator* (double k, Double2 a) noexcept { return make (k * a[0], k * a[1]); }
    Double2& operator+= (Double2 b) noexcept { v[0] += b[0]; v[1] += b[1]; return *this; }
    static inline Double2 mulAdd (Double2 addend, double k, Double2 x) noexcept { return make (addend[0] + k * x[0], addend[1] + k * x[1]); }
    static inline Double2 mulSub (Double2 minuend, double k, Double2 x) noexcept { return make (minuend[0] - k * x[0], minuend[1] - k * x[1]); }
};

struct Int32x4
{
    std::int32_t v[4];

    static constexpr Int32x4 zero() noexcept { return { { 0, 0, 0, 0 } }; }
    static constexpr Int32x4 splat (std::int32_t x) noexcept { return { { x, x, x, x } }; }
    static constexpr Int32x4 make (std::int32_t a, std::int32_t b, std::int32_t c, std::int32_t d) noexcept
    {
        return { { a, b, c, d } };
    }
    std::int32_t operator[] (int lane) const noexcept { return v[lane]; }
    friend Int32x4 operator+ (Int32x4 a, Int32x4 b) noexcept { return make (a[0] + b[0], a[1] + b[1], a[2] + b[2], a[3] + b[3]); }
    friend Int32x4 operator- (Int32x4 a) noexcept { return make (-a[0], -a[1], -a[2], -a[3]); }
    friend Int32x4 operator- (Int32x4 a, Int32x4 b) noexcept { return make (a[0] - b[0], a[1] - b[1], a[2] - b[2], a[3] - b[3]); }
    friend Int32x4 operator* (Int32x4 a, Int32x4 b) noexcept { return make (a[0] * b[0], a[1] * b[1], a[2] * b[2], a[3] * b[3]); }
    friend Int32x4 operator>> (Int32x4 a, int s) noexcept { return make (a[0] >> s, a[1] >> s, a[2] >> s, a[3] >> s); }
    friend Int32x4 operator& (Int32x4 a, Int32x4 b) noexcept { return make (a[0] & b[0], a[1] & b[1], a[2] & b[2], a[3] & b[3]); }
};

struct Float4
{
    float v[4];

    static constexpr Float4 zero() noexcept { return { { 0.0f, 0.0f, 0.0f, 0.0f } }; }
    static constexpr Float4 splat (float x) noexcept { return { { x, x, x, x } }; }
    static constexpr Float4 make (float a, float b, float c, float d) noexcept { return { { a, b, c, d } }; }
    static inline Float4 load (const float* ptr) noexcept { return make (ptr[0], ptr[1], ptr[2], ptr[3]); }
    float operator[] (int lane) const noexcept { return v[lane]; }
    friend Float4 operator+ (Float4 a, Float4 b) noexcept { return make (a[0] + b[0], a[1] + b[1], a[2] + b[2], a[3] + b[3]); }
    friend Float4 operator* (Float4 a, Float4 b) noexcept { return make (a[0] * b[0], a[1] * b[1], a[2] * b[2], a[3] * b[3]); }
    Float4& operator+= (Float4 b) noexcept { for (int i = 0; i < 4; ++i) v[i] += b[i]; return *this; }
    static inline Float4 mulAdd (Float4 addend, Float4 a, Float4 b) noexcept
    {
        return make (addend[0] + a[0] * b[0], addend[1] + a[1] * b[1], addend[2] + a[2] * b[2], addend[3] + a[3] * b[3]);
    }
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
    if (static_cast<std::uint32_t> (v + 131072) < 262144u)
        return static_cast<std::uint16_t> (static_cast<std::uint32_t> (v >> 4) & 0x3FFFu);
    if (static_cast<std::uint32_t> (v + 524288) < 1048576u)
        return static_cast<std::uint16_t> ((static_cast<std::uint32_t> (v >> 6) & 0x3FFFu) | 0x4000u);
    if (static_cast<std::uint32_t> (v + 2097152) < 4194304u)
        return static_cast<std::uint16_t> ((static_cast<std::uint32_t> (v >> 8) & 0x3FFFu) | 0x8000u);
    const std::int32_t mantissa = std::clamp (v >> 10, -8192, 8191);
    return static_cast<std::uint16_t> ((static_cast<std::uint32_t> (mantissa) & 0x3FFFu) | 0xC000u);
}

inline constexpr Q23 unpackDelay16 (std::uint16_t code) noexcept
{
    const std::int32_t raw = static_cast<std::int32_t> (code & 0x3FFFu);
    const std::int32_t mantissa = (raw ^ 0x2000) - 0x2000;
    const int shift = 4 + 2 * static_cast<int> (code >> 14);
    return static_cast<Q23> (static_cast<std::uint32_t> (mantissa) << shift);
}

inline Int32x4 wrap24x4 (Int32x4 x) noexcept
{
#if defined(__clang__) || defined(__GNUC__)
    typedef std::uint32_t ULanes __attribute__((vector_size (16)));
    ULanes u;
    std::memcpy (&u, &x.v, sizeof (u));
    u <<= 8;
    Int32x4::Lanes s;
    std::memcpy (&s, &u, sizeof (s));
    return { s >> 8 };
#else
    return Int32x4::make (wrap24 (x[0]), wrap24 (x[1]), wrap24 (x[2]), wrap24 (x[3]));
#endif
}

inline Int32x4 sat24x4 (Int32x4 x) noexcept
{
#if defined(__clang__) || defined(__GNUC__)
    const Int32x4::Lanes hi { kQ23Max, kQ23Max, kQ23Max, kQ23Max };
    const Int32x4::Lanes lo { kQ23Min, kQ23Min, kQ23Min, kQ23Min };
    const Int32x4::Lanes c = x.v > hi ? hi : x.v;
    return { c < lo ? lo : c };
#else
    return Int32x4::make (sat24 (x[0]), sat24 (x[1]), sat24 (x[2]), sat24 (x[3]));
#endif
}

inline Int32x4 mulSplit15x4 (Int32x4 a, Int32x4 hi8, Int32x4 lo7) noexcept
{
#if defined(__clang__) || defined(__GNUC__)
    return { ((a.v * hi8.v) >> 7) + (((a.v >> 7) * lo7.v) >> 7) };
#else
    return Int32x4::make (mulSplit15 (a[0], hi8[0], lo7[0]), mulSplit15 (a[1], hi8[1], lo7[1]),
                          mulSplit15 (a[2], hi8[2], lo7[2]), mulSplit15 (a[3], hi8[3], lo7[3]));
#endif
}

inline Int32x4 mulSplit15x2x4 (Int32x4 a, Int32x4 hi8, Int32x4 lo7) noexcept
{
#if defined(__clang__) || defined(__GNUC__)
    return { ((a.v * hi8.v) >> 6) + (((a.v >> 7) * lo7.v) >> 6) };
#else
    return Int32x4::make (mulSplit15x2 (a[0], hi8[0], lo7[0]), mulSplit15x2 (a[1], hi8[1], lo7[1]),
                          mulSplit15x2 (a[2], hi8[2], lo7[2]), mulSplit15x2 (a[3], hi8[3], lo7[3]));
#endif
}
} // namespace trancera
