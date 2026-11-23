// 100 Hz stepped exponential envelope generator with per-stage smoother rates.
#pragma once

#include "Core.h"
#include "Mapping.h"

namespace trancera
{
class Envelope
{
public:
    static constexpr double attackTarget = 1.25;

    void configure (int attack, int decay, double sustain, int release) noexcept
    {
        attackVal_ = std::clamp (attack, 0, 127);
        decayVal_ = std::clamp (decay, 0, 127);
        releaseVal_ = std::clamp (release, 0, 127);
        attackStep_ = mapping::envelopeStep100Hz (attackVal_);
        decayStep_ = mapping::envelopeStep100Hz (decayVal_);
        releaseStep_ = mapping::envelopeStep100Hz (releaseVal_);
        sustain_ = std::clamp (sustain, 0.0, 1.0);
    }

    void trigger() noexcept
    {
        stage_ = Stage::Attack;
        tickCount_ = kEnvelopeInterval;
        attackHoldSmoother_ = true;
        value_ += (attackTarget - value_) * attackStep_;
        if (value_ >= 1.0)
        {
            value_ = 1.0;
            stage_ = Stage::Decay;
        }
    }

    void release() noexcept
    {
        if (stage_ != Stage::Idle)
        {
            attackHoldSmoother_ = false;
            stage_ = Stage::Release;
        }
    }

    void reset() noexcept
    {
        stage_ = Stage::Idle;
        value_ = 0.0;
        tickCount_ = kEnvelopeInterval;
        attackHoldSmoother_ = false;
    }

    bool idle() const noexcept { return stage_ == Stage::Idle; }
    double value() const noexcept { return value_; }

    // 88.2 kHz one-pole coefficient matching each segment's transition speed.
    double smootherAlpha() const noexcept
    {
        if (attackHoldSmoother_ || stage_ == Stage::Attack)
            return attackVal_ < 30 ? (1.0 / 8.0) : (1.0 / 2048.0);
        if (stage_ == Stage::Decay)
            return decayVal_ < 10 ? (1.0 / 128.0) : decayVal_ < 30 ? (1.0 / 512.0) : (1.0 / 2048.0);
        if (stage_ == Stage::Release)
            return releaseVal_ < 10 ? (1.0 / 128.0) : releaseVal_ < 30 ? (1.0 / 512.0) : (1.0 / 2048.0);
        return 1.0 / 2048.0;
    }

    double filterSmootherAlpha() const noexcept
    {
        if (attackHoldSmoother_ || stage_ == Stage::Attack)
            return attackVal_ == 0 ? (1.0 / 8.0) : attackVal_ < 30 ? (1.0 / 512.0) : (1.0 / 2048.0);
        if (stage_ == Stage::Decay)
            return decayVal_ == 0 ? (1.0 / 8.0) : decayVal_ < 10 ? (1.0 / 128.0) : decayVal_ < 30 ? (1.0 / 512.0) : (1.0 / 2048.0);
        if (stage_ == Stage::Release)
            return releaseVal_ == 0 ? (1.0 / 8.0) : releaseVal_ < 10 ? (1.0 / 128.0) : releaseVal_ < 30 ? (1.0 / 512.0) : (1.0 / 2048.0);
        return 1.0 / 512.0;
    }

    double process() noexcept
    {
        if (stage_ == Stage::Idle)
            return 0.0;
        if (--tickCount_ <= 0)
        {
            tickCount_ = kEnvelopeInterval;
            tick100Hz();
        }
        return value_;
    }

    void tick100Hz() noexcept
    {
        attackHoldSmoother_ = false;
        switch (stage_)
        {
            case Stage::Attack:
                value_ += (attackTarget - value_) * attackStep_;
                if (value_ >= 1.0)
                {
                    value_ = 1.0;
                    stage_ = Stage::Decay;
                }
                break;
            case Stage::Decay:
                value_ += (sustain_ - value_) * decayStep_;
                break;
            case Stage::Release:
                value_ -= value_ * releaseStep_;
                if (value_ < 1.0e-5)
                {
                    value_ = 0.0;
                    stage_ = Stage::Idle;
                }
                break;
            case Stage::Idle:
                break;
        }
    }

private:
    enum class Stage { Idle, Attack, Decay, Release };

    Stage stage_ { Stage::Idle };
    int attackVal_ { 0 }, decayVal_ { 64 }, releaseVal_ { 5 };
    int tickCount_ { kEnvelopeInterval };
    double value_ { 0.0 };
    double attackStep_ { 1.0 }, decayStep_ { 1.0 }, releaseStep_ { 1.0 }, sustain_ { 1.0 };
    bool attackHoldSmoother_ { false };
};
} // namespace trancera
