// Linear-phase 8x oversampling, copied unchanged from Protocodus Septum
// (Source/DSP/AnalogOutput.h at f4e6377, same owner and licence). These are
// numerical rate-conversion filters, not claims about the modelled hardware.
#pragma once

#include <array>
#include <cstddef>

namespace trancera
{
namespace oversampling
{
// Two channels in lockstep. Each lane does exactly the scalar arithmetic, so
// a stereo filter is bit-identical to two mono ones, while the compiler can
// issue both lanes as one SIMD operation.
struct Stereo
{
    double l { 0.0 }, r { 0.0 };
    friend Stereo operator+ (Stereo a, Stereo b) noexcept { return { a.l + b.l, a.r + b.r }; }
    friend Stereo operator- (Stereo a, Stereo b) noexcept { return { a.l - b.l, a.r - b.r }; }
    friend Stereo operator* (double k, Stereo a) noexcept { return { k * a.l, k * a.r }; }
    Stereo& operator+= (Stereo b) noexcept { l += b.l; r += b.r; return *this; }
};
// Folded odd taps of DC-normalised Parks-McClellan half-band filters.
// Reproducible design: scipy.signal.remez(N, [0, edge, .5-edge, .5],
// [1, 0], fs=1); keep odd taps, centre=.5, scale folded odd sum to .25.
// These filters are numerical sample-rate conversion, not hardware claims.
// N=129, edge=.227: <.00024 dB ripple, >91 dB stopband attenuation.
inline constexpr std::array<double, 32> outerTaps {
    -1.538892892836719e-05, 2.5050850992617275e-05,
    -4.4762568448125e-05, 7.4030935096448862e-05,
    -0.00011585890895102736, 0.00017382487498333096,
    -0.0002521275827128851, 0.00035563811291140963,
    -0.00048992777601916866, 0.00066132425117102086,
    -0.0008769688919648132, 0.0011448816955751818,
    -0.0014740350513820126, 0.0018745238968436546,
    -0.0023577683098338319, 0.0029367956774011775,
    -0.0036267168808547406, 0.0044454650926277969,
    -0.0054147856830518756, 0.0065619725988216929,
    -0.0079223446361525087, 0.0095433895626549099,
    -0.011491481840524818, 0.013863449524886539,
    -0.016807460710853199, 0.020563199902335121,
    -0.025545913930314919, 0.032542489547400306,
    -0.043243621046925848, 0.062052964935443822,
    -0.1051326254064316, 0.31799278669420478
};
// N=33, edge=.18: <.00255 dB ripple, >70 dB stopband attenuation.
inline constexpr std::array<double, 8> middleTaps {
    -0.0005509614757843159, 0.0020627687465320558,
    -0.0055179759817263361, 0.012256819385295844,
    -0.024444365442790196, 0.046709333516118633,
    -0.095074927306226714, 0.314559308558581
};
// N=17, edge=.10: <.00060 dB ripple, >83 dB stopband attenuation.
inline constexpr std::array<double, 4> innerTaps {
    -0.0024718290140259523, 0.016959158839556787,
    -0.067673764721470173, 0.30318643489593938
};

// Each history is stored twice, at slot and slot + size, so every tap reads
// one contiguous window behind the write head instead of a masked index.
// The sums keep their order, so the output is bit-identical.
template <std::size_t Pairs, const std::array<double, Pairs>& Taps, class V>
class HalfBand
{
public:
    void reset() noexcept
    {
        up_.fill (V {});
        even_.fill (V {});
        odd_.fill (V {});
        upWrite_ = downWrite_ = 0;
    }

    std::array<V, 2> upsample (V x) noexcept
    {
        up_[upWrite_] = up_[upWrite_ + size] = x;
        const V* head = &up_[upWrite_ + size];
        const V even = head[-static_cast<std::ptrdiff_t> (Pairs)];
        V odd {};
        for (std::size_t i = 0; i < Pairs; ++i)
            odd += Taps[i] * (*(head - i) + *(head - (2 * Pairs - 1 - i)));
        upWrite_ = (upWrite_ + 1) & mask;
        return { even, 2.0 * odd };
    }

    V downsample (V even, V odd) noexcept
    {
        even_[downWrite_] = even_[downWrite_ + size] = even;
        odd_[downWrite_] = odd_[downWrite_ + size] = odd;
        const V* evenHead = &even_[downWrite_ + size];
        const V* oddHead = &odd_[downWrite_ + size];
        V y = 0.5 * evenHead[-static_cast<std::ptrdiff_t> (Pairs)];
        for (std::size_t i = 0; i < Pairs; ++i)
            y += Taps[i] * (*(oddHead - 1 - i) + *(oddHead - (2 * Pairs - i)));
        downWrite_ = (downWrite_ + 1) & mask;
        return y;
    }

private:
    // The downsampler needs a sample 2*Pairs places behind its write head.
    // Keep more than that many slots so writing cannot replace that sample.
    static constexpr std::size_t size = 2 * Pairs + 1 <= 16 ? 16
                                       : 2 * Pairs + 1 <= 32 ? 32
                                       : 2 * Pairs + 1 <= 64 ? 64 : 128;
    static constexpr std::size_t mask = size - 1;
    static_assert (2 * Pairs < size);
    std::array<V, 2 * size> up_ {}, even_ {}, odd_ {};
    std::size_t upWrite_ { 0 }, downWrite_ { 0 };
};

template <class V>
class Oversampling8
{
public:
    // 64 samples for the outer round trip, 16/2 for the middle, 8/4 inner.
    static constexpr int latencySamples = 74;

    void reset() noexcept { outer_.reset(); middle_.reset(); inner_.reset(); }

    template <class Circuit>
    V process (V x, Circuit&& circuit) noexcept
    {
        const auto a = outer_.upsample (x);
        std::array<V, 2> outerResults {};
        for (std::size_t i = 0; i < 2; ++i)
        {
            const auto b = middle_.upsample (a[i]);
            std::array<V, 2> middleResults {};
            for (std::size_t j = 0; j < 2; ++j)
            {
                const auto c = inner_.upsample (b[j]);
                // Explicit evaluation order preserves the circuit state.
                const V first = circuit (c[0]);
                const V second = circuit (c[1]);
                middleResults[j] = inner_.downsample (first, second);
            }
            outerResults[i] = middle_.downsample (middleResults[0], middleResults[1]);
        }
        return outer_.downsample (outerResults[0], outerResults[1]);
    }

private:
    HalfBand<32, outerTaps, V> outer_;
    HalfBand<8, middleTaps, V> middle_;
    HalfBand<4, innerTaps, V> inner_;
};
} // namespace oversampling
} // namespace trancera
