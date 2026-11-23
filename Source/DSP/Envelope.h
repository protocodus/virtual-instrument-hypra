// [measured] JE-8086: every stage is one exponential with the time constant
// of mapping::envelopeTau. The attack heads for 1.2525 and stops at 1 (fit
// to within 0.2 % of full level from 40 to 127); decay and release fall to
// SUSTAIN and to 0.
#pragma once

#include "Core.h"

namespace hypra
{
class Envelope
{
public:
    // Time constants in seconds for each stage.
    void configure (double attack, double decay, double sustain, double release) noexcept
    {
        attack_ = coefficient (attack);
        decay_ = coefficient (decay);
        release_ = coefficient (release);
        sustain_ = sustain;
    }

    static constexpr double attackTarget = 1.2525;

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
                value_ = attackTarget + (value_ - attackTarget) * attack_;
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

    static double coefficient (double tau) noexcept
    {
        return std::exp (-1.0 / std::max (0.25, tau * kEngineRate));
    }

    Stage stage_ { Stage::Idle };
    double value_ { 0.0 }, attack_ { 0.0 }, decay_ { 0.0 }, release_ { 0.0 }, sustain_ { 1.0 };
};
} // namespace hypra
