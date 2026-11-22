// [inferred] Linear attack and exponential decay/release, the shapes drawn
// in OM pp.59-62. Segment times are in Mapping.h.
#pragma once

#include "Core.h"

namespace hypra
{
class Envelope
{
public:
    void configure (double attack, double decay, double sustain, double release) noexcept
    {
        attackStep_ = 1.0 / std::max (1.0, attack * kEngineRate);
        decay_ = coefficient (decay);
        release_ = coefficient (release);
        sustain_ = sustain;
    }

    void trigger() noexcept { stage_ = Stage::Attack; }
    void release() noexcept { if (stage_ != Stage::Idle) stage_ = Stage::Release; }
    void reset() noexcept { stage_ = Stage::Idle; value_ = 0.0; }

    bool idle() const noexcept { return stage_ == Stage::Idle; }
    double value() const noexcept { return value_; }

    double process() noexcept
    {
        switch (stage_)
        {
            case Stage::Attack:
                value_ += attackStep_;
                if (value_ >= 1.0)
                {
                    value_ = 1.0;
                    stage_ = Stage::Decay;
                }
                break;
            case Stage::Decay:
                value_ = sustain_ + (value_ - sustain_) * decay_;
                break;
            case Stage::Release:
                value_ *= release_;
                if (value_ < 1.0e-5)
                    reset();
                break;
            case Stage::Idle:
                break;
        }
        return value_;
    }

private:
    enum class Stage { Idle, Attack, Decay, Release };

    // Falls by 60 dB in the named time.
    static double coefficient (double seconds) noexcept
    {
        return std::exp (-6.90775527898 / std::max (1.0, seconds * kEngineRate));
    }

    Stage stage_ { Stage::Idle };
    double value_ { 0.0 }, attackStep_ { 1.0 }, decay_ { 0.0 }, release_ { 0.0 }, sustain_ { 1.0 };
};
} // namespace hypra
