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
        sustain_ = static_cast<std::int32_t> (std::llround (std::clamp (sustain, 0.0, 1.0) * static_cast<double> (kOne)));
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
        value_ = 0;
        tickCount_ = kEnvelopeInterval;
        nextSpeed_ = 11;
        smootherAlphaQ23_ = mapping::envSmootherAlphaQ23 (11);
    }

    bool idle() const noexcept { return stage_ == Stage::Idle; }
    double value() const noexcept { return static_cast<double> (value_) * (1.0 / static_cast<double> (kOne)); }
    std::int32_t valueQ14() const noexcept { return value_ >> 10; }

    // 88.2 kHz one-pole coefficient matching the active segment and modulation speed.
    double smootherAlpha() const noexcept { return fromQ23 (smootherAlphaQ23_); }
    double filterSmootherAlpha() const noexcept { return smootherAlpha(); }
    Q23 smootherAlphaQ23() const noexcept { return smootherAlphaQ23_; }

    double process() noexcept
    {
        if (--tickCount_ <= 0)
        {
            tickCount_ = kEnvelopeInterval;
            tick100Hz();
        }
        return value();
    }

    void tick100Hz (bool lfoActive = false, bool lfoEdge = false, bool useEnvSpeed = true) noexcept
    {
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
        const auto expStep = [] (std::int32_t diff, std::uint16_t rate) noexcept -> std::int32_t
        {
            const std::int32_t delta = static_cast<std::int32_t> ((static_cast<std::int64_t> (diff) * rate) >> 13);
            return std::max<std::int32_t> (kMinStep, delta);
        };

        switch (stage_)
        {
            case Stage::Attack:
                if (attackRate_ > 4864)
                {
                    value_ = kOne;
                    stage_ = Stage::Decay;
                    envSpeed = fastSpeedForRate (attackRate_);
                }
                else
                {
                    value_ += expStep (kAttackTarget - value_, attackRate_);
                    if (value_ >= kOne)
                    {
                        value_ = kOne;
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
                        value_ = std::max (sustain_, value_ - expStep (value_ - sustain_, decayRate_));
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
                        value_ = std::min (sustain_, value_ + expStep (sustain_ - value_, decayRate_));
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
                    value_ = 0;
                    stage_ = Stage::Idle;
                    envSpeed = fastSpeedForRate (releaseRate_);
                }
                else
                {
                    const std::int32_t step = expStep (value_, releaseRate_);
                    if (value_ <= step)
                    {
                        value_ = 0;
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
                value_ = 0;
                break;
        }

        if (useEnvSpeed && envSpeed < speed && speed < 12)
            speed = envSpeed;

        if (speed == 10)
        {
            smootherAlphaQ23_ = mapping::envSmootherAlphaQ23 (10);
            nextSpeed_ = 11;
        }
        else if (stage_ == Stage::Attack)
        {
            if (speed < 12)
            {
                smootherAlphaQ23_ = mapping::envSmootherAlphaQ23 (speed);
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
                smootherAlphaQ23_ = mapping::envSmootherAlphaQ23 (speed);
            nextSpeed_ = 11;
        }
    }

private:
    static constexpr std::int32_t kOne = 1 << 24;
    static constexpr std::int32_t kAttackTarget = 20971520; // 1.25 * kOne
    static constexpr std::int32_t kMinStep = 1 << 10;

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
    std::int32_t value_ { 0 }, sustain_ { kOne };
    Q23 smootherAlphaQ23_ { mapping::envSmootherAlphaQ23 (11) };
};
} // namespace trancera
