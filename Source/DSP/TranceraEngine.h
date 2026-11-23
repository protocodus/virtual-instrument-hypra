// The instrument: two parts sharing eight voices, at 88.2 kHz inside.
#pragma once

#include "AnalogOutput.h"
#include "Effects.h"
#include "Envelope.h"
#include "Filter.h"
#include "Oscillators.h"
#include "Resampler.h"

#include <array>

namespace trancera
{
enum class Part : std::uint8_t { Upper, Lower };
enum class KeyMode : std::uint8_t { Single, Dual };

class Engine
{
public:
    static constexpr int maxVoices = 8; // OM p.129

    // Allocates. Everything below is safe on the audio thread.
    void prepare (double hostRate, std::uint32_t seed = 8000);
    void reset() noexcept;

    void setPatch (Part part, const Patch& patch) noexcept;
    void setMode (KeyMode mode) noexcept;
    void setPartDetune (int cents) noexcept { partDetune_ = std::clamp (cents, -50, 50); }
    void setOutputNode (OutputNode node) noexcept { output_.prepare (node); }
    // The DAC's and op-amps' colour on the analogue nodes (on by default; see AnalogColor).
    void setAnalogColor (bool on) noexcept
    {
        AnalogColor c = output_.color();
        c.on = on;
        output_.setColor (c);
    }
    void setVolume (double gain) noexcept { volume_ = std::clamp (gain, 0.0, 2.0); }
    // A host's master tuning, in cents, on every note (held ones too).
    void setTune (double cents) noexcept { tune_ = std::clamp (cents, -100.0, 100.0) / 100.0; }

    void noteOn (int note, int velocity) noexcept;
    void noteOff (int note) noexcept;
    void allNotesOff() noexcept;
    void pitchBend (double value) noexcept { bend_ = std::clamp (value, -1.0, 1.0); }
    void modWheel (double value) noexcept { mod_ = std::clamp (value, 0.0, 1.0); }
    void sustain (bool down) noexcept;

    void render (float* left, float* right, int frames) noexcept;
    [[nodiscard]] int activeVoices() const noexcept;

private:
    struct Voice
    {
        OscillatorPair oscillators;
        Filter filter;
        Envelope ampEnv, filterEnv, pitchEnv;
        bool active { false }, held { false }, sustained { false };
        int note { 60 };
        std::uint64_t order { 0 };
        double velocity { 1.0 }, pitch { 60.0 }, target { 60.0 }, detune { 0.0 };
        double portaDelta { 0.0 };
        int portaRemain { 0 };
        int fadePhase { 16384 };
        int qSnapTicks { 0 };
        Q23 ampBaseQ23 { 0 }, ampTargetQ23 { 0 }, ampSmoothedQ23 { 0 };
        Coeff15 ampAlpha {}, ampSplit {};
        bool ampMoving { false };
        Patch patch; // the part's patch at this voice's velocity (withVelocity)
    };

    struct HeldNote { int note { 0 }, velocity { 0 }; };

    struct PartState
    {
        static constexpr Q23 kPanCenterQ23 = 22784 << 8;

        Patch patch;
        ToneControl tone;
        Delay delay;
        Chorus chorus;
        Random lfoRandom;
        std::array<HeldNote, 128> keys {};
        int keyCount { 0 };
        std::uint16_t lfo1Phase { 0 }, lfo2Phase { 0 };
        std::uint16_t lfo1Step { 0 }, lfo2Step { 0 };
        int effAmpLfo { 0 };
        double lfoHeld { 0.0 }, lfo1 { 0.0 }, lfo2 { 0.0 };
        double lastPitch { 60.0 };
        Q23 panLQ23 { kPanCenterQ23 }, panRQ23 { kPanCenterQ23 };
        Q23 panTargetLQ23 { kPanCenterQ23 }, panTargetRQ23 { kPanCenterQ23 };
        Coeff15 panLSplit { splitCoeff15 (kPanCenterQ23) }, panRSplit { splitCoeff15 (kPanCenterQ23) };
        bool hasPlayed { false }, lfoEdge { false };
    };

    struct Span { int first, count; };

    [[nodiscard]] Span span (int part) const noexcept;
    [[nodiscard]] int partCount() const noexcept { return mode_ == KeyMode::Dual ? 2 : 1; }
    void startPart (int part, int note, int velocity) noexcept;
    void startMono (int part, int note, int velocity, bool retrigger) noexcept;
    void trigger (Voice& voice, int part, int note, int velocity, bool retrigger, bool mayGlide) noexcept;
    void release (Voice& voice) noexcept;
    void control() noexcept;
    void controlVoice (Voice& voice, int part, bool advanceTick) noexcept;
    Stereo tick() noexcept;

    std::array<Voice, maxVoices> voices_ {};
    std::array<PartState, 2> parts_ {};
    AnalogOutput output_;
    Resampler resampler_;
    Random random_;
    KeyMode mode_ { KeyMode::Single };
    std::uint32_t seed_ { 8000 };
    std::uint64_t order_ { 0 };
    double bend_ { 0.0 }, mod_ { 0.0 }, volume_ { 1.0 }, tune_ { 0.0 };
    int partDetune_ { 0 }, untilControl_ { kEnvelopeInterval };
    bool sustain_ { false };
};
} // namespace trancera
