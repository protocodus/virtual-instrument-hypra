// Patch parameters, with the JP-8000's own ranges (Owner's Manual 1996,
// Parameter List pp.112-113).
#pragma once

#include <cstdint>

namespace hypra
{
enum class Wave1 : std::uint8_t { SuperSaw, TriangleMod, Noise, FeedbackOsc, Square, Saw, Triangle };
enum class Wave2 : std::uint8_t { Square, Saw, Triangle };
enum class FilterType : std::uint8_t { HighPass, BandPass, LowPass };
enum class LfoWave : std::uint8_t { Triangle, Saw, Square, SampleHold };
enum class ModDestination : std::uint8_t { Osc1And2, Osc2, XMod };
enum class PanMode : std::uint8_t { Off, Auto, Manual };
enum class ChorusType : std::uint8_t
{
    SuperSlow, SuperMid, SuperFast, SuperClear, FlangerSlow, FlangerDeep,
    FlangerFast, DeepPhasing, JetPhasing, Twisting, Freeze1, Freeze2
};
enum class DelayType : std::uint8_t { PanLR, PanRL, PanShort, MonoShort, MonoLong };

struct Osc1Params
{
    Wave1 wave { Wave1::SuperSaw };
    int ctrl1 { 64 }; // detune, offset, cutoff, harmonics, width or shape
    int ctrl2 { 0 };  // mix, LFO 1 depth, resonance, feedback amount or PWM depth
};

struct Osc2Params
{
    Wave2 wave { Wave2::Saw };
    bool sync { false };
    int range { 0 }; // -25 is -WIDE, +25 is +WIDE
    int fine { 0 };  // cents; inside WIDE it spans four octaves
    int pulseWidth { 0 };
    int pwmDepth { 0 };
};

struct EnvParams
{
    int attack { 0 }, decay { 64 }, sustain { 127 }, release { 5 };
};

struct FilterParams
{
    FilterType type { FilterType::LowPass };
    bool steep { true };
    int cutoff { 127 }, resonance { 0 };
    int keyFollow { 0 }, lfoDepth { 0 }, envDepth { 0 };
    EnvParams env;
};

struct Lfo1Params
{
    LfoWave wave { LfoWave::Triangle };
    int rate { 48 }, fade { 0 };
};

struct Lfo2Params
{
    int rate { 60 };
    int pitch { 8 }, filter { 0 }, amp { 0 }; // scaled by the modulation lever
};

struct EffectParams
{
    int bass { 0 }, treble { 0 };
    ChorusType chorusType { ChorusType::SuperSlow };
    int chorus { 0 };
    DelayType delayType { DelayType::PanLR };
    int delayTime { 64 }, feedback { 40 }, delay { 0 };
};

struct Patch
{
    Osc1Params osc1;
    Osc2Params osc2;
    bool ring { false };
    int xmod { 0 };
    int balance { -64 };
    ModDestination destination { ModDestination::Osc1And2 };
    int pitchLfo { 0 }, pitchEnvDepth { 0 }, pitchAttack { 0 }, pitchDecay { 40 };
    FilterParams filter;
    EnvParams amp;
    int level { 100 };
    PanMode pan { PanMode::Off };
    int ampLfo { 0 }; // also the manual pan position
    Lfo1Params lfo1;
    Lfo2Params lfo2;
    EffectParams effects;
    bool mono { false }, legato { false }, portamento { false };
    int portamentoTime { 11 };
    int oscShift { 0 };
    int bendUp { 2 }, bendDown { 2 };
    int velocityAmp { 32 }, velocityFilter { 0 };
    // JP-8080 only: the JP-8000 has no unison switch.
    bool unison { false };
    int unisonDetune { 12 }; // cents
};
} // namespace hypra
