// 100 Hz stepped exponential envelope generator with 12-level smoother control.
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
        attackRate_ = mapping::envRateTable[static_cast<std::size_t> (attackVal_)];
        decayRate_ = mapping::envRateTable[static_cast<std::size_t> (decayVal_)];
        releaseRate_ = mapping::envRateTable[static_cast<std::size_t> (releaseVal_)];
        sustain_ = std::clamp (sustain, 0.0, 1.0);
    }

    void trigger (bool lfoActive = false, bool lfoEdge = false, bool useEnvSpeed = true) noexcept
    {
        stage_ = Stage::Attack;
        tickCount_ = kEnvelopeInterval;
        nextSpeed_ = 11;
        tick100Hz (lfoActive, lfoEdge, useEnvSpeed);
    }

    void release() noexcept
    {
        if (stage_ != Stage::Idle)
            stage_ = Stage::Release;
    }

    void reset() noexcept
    {
        stage_ = Stage::Idle;
        value_ = 0.0;
        tickCount_ = kEnvelopeInterval;
        nextSpeed_ = 11;
        smootherAlpha_ = mapping::envSmootherAlpha (11);
    }

    bool idle() const noexcept { return stage_ == Stage::Idle; }
    double value() const noexcept { return value_; }

    // 88.2 kHz one-pole coefficient matching the active segment and modulation speed.
    double smootherAlpha() const noexcept { return smootherAlpha_; }
    double filterSmootherAlpha() const noexcept { return smootherAlpha_; }

    double process() noexcept
    {
        if (--tickCount_ <= 0)
        {
            tickCount_ = kEnvelopeInterval;
            tick100Hz();
        }
        return value_;
    }

    void tick100Hz (bool lfoActive = false, bool lfoEdge = false, bool useEnvSpeed = true) noexcept
    {
        constexpr double kMinStep = 1.0 / 16384.0;
        int speed = nextSpeed_;
        if (lfoActive)
        {
            if (lfoEdge)
            {
                if (speed > 2 && speed <= 11)
                    speed = 2;
            }
            else if (speed == 11)
            {
                speed = 10;
            }
        }

        int envSpeed = speed;
        switch (stage_)
        {
            case Stage::Attack:
                if (attackRate_ > 4864)
                {
                    value_ = 1.0;
                    stage_ = Stage::Decay;
                    envSpeed = fastSpeedForRate (attackRate_);
                }
                else
                {
                    const double step = std::max (kMinStep, (attackTarget - value_) * (static_cast<double> (attackRate_) / 8192.0));
                    value_ += step;
                    if (value_ >= 1.0)
                    {
                        value_ = 1.0;
                        stage_ = Stage::Decay;
                    }
                    if (attackRate_ > 1536)
                        envSpeed = 10;
                }
                break;

            case Stage::Decay:
                if (value_ > sustain_)
                {
                    if (decayRate_ > 4864)
                    {
                        value_ = sustain_;
                        stage_ = Stage::Sustain;
                        envSpeed = fastSpeedForRate (decayRate_);
                    }
                    else
                    {
                        const double step = std::max (kMinStep, (value_ - sustain_) * (static_cast<double> (decayRate_) / 8192.0));
                        value_ = std::max (sustain_, value_ - step);
                        if (value_ <= sustain_)
                        {
                            value_ = sustain_;
                            stage_ = Stage::Sustain;
                        }
                        if (decayRate_ > 1536)
                            envSpeed = 10;
                    }
                }
                else if (value_ < sustain_)
                {
                    if (decayRate_ > 4864)
                    {
                        value_ = sustain_;
                        stage_ = Stage::Sustain;
                        envSpeed = fastSpeedForRate (decayRate_);
                    }
                    else
                    {
                        const double step = std::max (kMinStep, (sustain_ - value_) * (static_cast<double> (decayRate_) / 8192.0));
                        value_ = std::min (sustain_, value_ + step);
                        if (value_ >= sustain_)
                        {
                            value_ = sustain_;
                            stage_ = Stage::Sustain;
                        }
                        if (decayRate_ > 1536)
                            envSpeed = 10;
                    }
                }
                else
                {
                    stage_ = Stage::Sustain;
                }
                break;

            case Stage::Sustain:
                value_ = sustain_;
                break;

            case Stage::Release:
                if (releaseRate_ > 4864)
                {
                    value_ = 0.0;
                    stage_ = Stage::Idle;
                    envSpeed = fastSpeedForRate (releaseRate_);
                }
                else
                {
                    const double step = std::max (kMinStep, value_ * (static_cast<double> (releaseRate_) / 8192.0));
                    if (value_ <= step)
                    {
                        value_ = 0.0;
                        stage_ = Stage::Idle;
                    }
                    else
                    {
                        value_ -= step;
                    }
                    if (releaseRate_ > 1536)
                        envSpeed = 10;
                }
                break;

            case Stage::Idle:
                value_ = 0.0;
                break;
        }

        if (useEnvSpeed && envSpeed < speed && speed < 12)
            speed = envSpeed;

        if (speed == 10)
        {
            smootherAlpha_ = mapping::envSmootherAlpha (10);
            nextSpeed_ = 11;
        }
        else if (stage_ == Stage::Attack)
        {
            if (speed < 12)
            {
                smootherAlpha_ = mapping::envSmootherAlpha (speed);
                nextSpeed_ = (speed == 0 || speed == 11) ? 11 : 13;
            }
            else
            {
                nextSpeed_ = (speed == 13) ? 12 : 11;
            }
        }
        else
        {
            if (speed < 12)
                smootherAlpha_ = mapping::envSmootherAlpha (speed);
            nextSpeed_ = 11;
        }
    }

private:
    static constexpr int fastSpeedForRate (std::uint16_t rate) noexcept
    {
        if (rate >= 8192) return 0;
        if (rate > 7680)  return 1;
        if (rate > 7168)  return 2;
        if (rate > 6656)  return 3;
        if (rate > 6400)  return 4;
        if (rate > 6144)  return 5;
        if (rate > 5632)  return 6;
        if (rate > 5376)  return 7;
        if (rate > 5120)  return 8;
        return 9;
    }

    enum class Stage { Idle, Attack, Decay, Sustain, Release };

    Stage stage_ { Stage::Idle };
    int attackVal_ { 0 }, decayVal_ { 64 }, releaseVal_ { 5 };
    std::uint16_t attackRate_ { 8192 }, decayRate_ { 256 }, releaseRate_ { 6174 };
    int tickCount_ { kEnvelopeInterval };
    int nextSpeed_ { 11 };
    double value_ { 0.0 }, sustain_ { 1.0 };
    double smootherAlpha_ { mapping::envSmootherAlpha (11) };
};
} // namespace trancera
