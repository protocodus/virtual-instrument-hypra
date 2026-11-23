// The analogue path from DAC to line jack, solved from the schematic. Left
// out: pot loading, mute transistors, ferrites, the 100R/100p RF filter.
//
// Roland JP-8000 Service Notes, January 1997 (70559865):
//   https://archive.org/details/roland_JP-8000_SERVICE_NOTES
//   p.23 DAC: IC20 uPD63200GS, 18/16SEL high (18 bit). C95 10u / R36 100k,
//        then IC22A: R39 5.6k, R38 47k || C107 100p, C109 10u / R42 100k.
//   p.27 panel B: volume VR10, R34 47k / R35 22k, C28 marked 10000p, IC6A
//        as a follower.
//   p.30 jack board: C34 10u / R48 100k, then IC4A as a Sallen-Key filter
//        R47 10k, R45 10k, C26 470p to the output, C30 680p to ground. Its
//        gain leg is R43 47k against R41 100k || (R38 100k + C24 22n), a
//        bass shelf. C22 10u / R32 100k, R40 1.5k + R37 680, C33 1000p.
// Roland JP-8080 Service Notes, July 1998, pp.19, 21 and 27 repeat that
// network, except the volume follower, which is C601 10u / R601 100k with no
// shunt capacitor.
//
// So there are two nodes to choose from. As printed, the keyboard has a pole
// at 1.06 kHz in the main output (-19.5 dB at 10 kHz), which cannot be the
// whole story unless the DSP pre-emphasises or the marking is wrong. The
// rack shows the same DSP, DAC and jack stage flat, and recordings of both
// instruments fit it and not the printed pole. Rack is the default
// (Docs/research/hardware-ab-2026-11-22.md).
//
// Small signal, ideal op-amps, high-impedance load. Gains are normalised to
// unity at 1 kHz on the rack node; no noise or op-amp clipping is invented.
#pragma once

#include "Core.h"
#include "Oversampling.h"

#include <array>
#include <complex>

namespace trancera
{
// Digital is the DAC word itself, for comparing with an emulator's output.
enum class OutputNode : std::uint8_t { Rack, KeyboardAsPrinted, Digital };

class AnalogOutput
{
public:
    void prepare (OutputNode node) noexcept
    {
        node_ = node;
        const double c = 2.0 * 8.0 * kEngineRate;
        buffer_.configure (bufferZero, bufferTau, c);
        jack_.configure (0.0, jackTau, c);
        panel_.configure (0.0, panelTau, c);

        const double t1 = d1 * c, t2 = d2 * c * c, t3 = d3 * c * c * c, u = n1 * c;
        const double a0 = 1.0 + t1 + t2 + t3;
        a_ = { (3.0 + t1 - t2 - 3.0 * t3) / a0,
               (3.0 - t1 - t2 + 3.0 * t3) / a0,
               (1.0 - t1 + t2 - t3) / a0 };
        b_ = { (n0 + u) / a0, (3.0 * n0 + u) / a0, (3.0 * n0 - u) / a0, (n0 - u) / a0 };
        normalise_ = 1.0 / std::abs (filters (OutputNode::Rack, 1000.0));

        const double h = 2.0 * kEngineRate * couplingTau;
        couplingPole_ = (h - 1.0) / (h + 1.0);
        couplingGain_ = h / (h + 1.0);
        reset();
    }

    void reset() noexcept
    {
        over_.reset();
        buffer_.reset();
        jack_.reset();
        panel_.reset();
        z_.fill ({});
        held_.fill ({});
        heldAt_ = 0;
        couplingX_.fill ({});
        couplingY_.fill ({});
    }

    // One 88.2 kHz DAC word in, the jack voltage out at 88.2 kHz.
    Stereo process (Stereo word) noexcept
    {
        Stereo x { convert (word.l), convert (word.r) };
        if (node_ == OutputNode::Digital)
            return x;
        for (std::size_t i = 0; i < couplingX_.size(); ++i)
        {
            const Stereo y = couplingGain_ * (x - couplingX_[i]) + couplingPole_ * couplingY_[i];
            couplingX_[i] = x;
            couplingY_[i] = y;
            x = y;
        }
        return over_.process (x, [this] (Stereo v) { return analogue (v); });
    }

    // Continuous-time response, without the DAC's sample-and-hold droop.
    static std::complex<double> response (OutputNode node, double hz) noexcept
    {
        const std::complex<double> s (0.0, kTwoPi * hz);
        const auto hp = s * couplingTau / (1.0 + s * couplingTau);
        return hp * hp * hp * hp * filters (node, hz) / std::abs (filters (OutputNode::Rack, 1000.0));
    }

private:
    static constexpr double bufferTau = 47000.0 * 100.0e-12;
    static constexpr double bufferZero = bufferTau / (1.0 + 47000.0 / 5600.0);
    static constexpr double jackTau = (1500.0 + 680.0) * 1000.0e-12;
    static constexpr double panelTau = 47000.0 * 22000.0 / (47000.0 + 22000.0) * 10000.0e-12;
    static constexpr double couplingTau = 10.0e-6 * 100000.0; // four in series

    // Sallen-Key with gain K(s) = (n0 + n1 s) / (1 + shelf s), from nodal
    // analysis of R47, R45, C26, C30, R43, R41, R38 and C24.
    static constexpr double r1 = 10000.0, r2 = 10000.0, cFeedback = 470.0e-12, cGround = 680.0e-12;
    static constexpr double legRatio = 100000.0 / 47000.0, legTau = 100000.0 * 0.022e-6;
    static constexpr double n0 = 1.0 + legRatio, n1 = legTau * (2.0 + legRatio);
    static constexpr double shelf = 2.0 * legTau;
    static constexpr double p1 = cGround * (r1 + r2) + cFeedback * r1;
    static constexpr double p2 = r1 * r2 * cFeedback * cGround;
    static constexpr double d1 = shelf + p1 - cFeedback * r1 * n0;
    static constexpr double d2 = p2 + shelf * p1 - cFeedback * r1 * n1;
    static constexpr double d3 = shelf * p2;

    static std::complex<double> filters (OutputNode node, double hz) noexcept
    {
        const std::complex<double> s (0.0, kTwoPi * hz);
        auto h = (1.0 + s * bufferZero) / (1.0 + s * bufferTau) / (1.0 + s * jackTau)
                 * (n0 + s * n1) / (1.0 + s * (d1 + s * (d2 + s * d3)));
        if (node == OutputNode::KeyboardAsPrinted)
            h /= 1.0 + s * panelTau;
        return h;
    }

    class FirstOrder
    {
    public:
        void configure (double zero, double pole, double c) noexcept
        {
            const double a0 = 1.0 + pole * c;
            a1_ = (1.0 - pole * c) / a0;
            b0_ = (1.0 + zero * c) / a0;
            b1_ = (1.0 - zero * c) / a0;
        }
        void reset() noexcept { z_ = {}; }
        Stereo process (Stereo x) noexcept
        {
            const Stereo y = b0_ * x + z_;
            z_ = b1_ * x - a1_ * y;
            return y;
        }

    private:
        double a1_ { 0.0 }, b0_ { 1.0 }, b1_ { 0.0 };
        Stereo z_;
    };

    static double convert (double x) noexcept
    {
        return std::clamp (std::nearbyint (x * 131072.0), -131072.0, 131071.0) / 131072.0;
    }

    Stereo analogue (Stereo v) noexcept
    {
        // The DAC holds each word for eight of these samples.
        held_[heldAt_] = v;
        heldAt_ = (heldAt_ + 1) % held_.size();
        Stereo hold;
        for (const Stereo& h : held_)
            hold += h;
        Stereo x = jack_.process (buffer_.process (0.125 * hold));
        if (node_ == OutputNode::KeyboardAsPrinted)
            x = panel_.process (x);
        const Stereo y = b_[0] * x + z_[0];
        z_[0] = b_[1] * x - a_[0] * y + z_[1];
        z_[1] = b_[2] * x - a_[1] * y + z_[2];
        z_[2] = b_[3] * x - a_[2] * y;
        return normalise_ * y;
    }

    oversampling::Oversampling8<Stereo> over_;
    FirstOrder buffer_, jack_, panel_;
    OutputNode node_ { OutputNode::Rack };
    std::array<double, 3> a_ {};
    std::array<double, 4> b_ { 1.0, 0.0, 0.0, 0.0 };
    std::array<Stereo, 3> z_ {};
    std::array<Stereo, 8> held_ {};
    std::size_t heldAt_ { 0 };
    std::array<Stereo, 4> couplingX_ {}, couplingY_ {};
    double normalise_ { 1.0 }, couplingPole_ { 1.0 }, couplingGain_ { 1.0 };
};
} // namespace trancera
