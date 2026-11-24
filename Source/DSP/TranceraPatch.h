// Patch parameters, with the JP-8000's own ranges (Owner's Manual 1996,
// Parameter List pp.112-113).
#pragma once

#include <cstdint>

namespace trancera
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
    FlangerFast, DeepPhasing, JetPhasing, Twisting, Freeze1, Freeze2, Distortion
};
enum class DelayType : std::uint8_t { PanLR, PanRL, PanShort, MonoShort, MonoLong };
enum class ArpMode : std::uint8_t { Up, Down, UpDown, Random, AsPlayed };

struct ArpParams
{
    bool on { false };
    ArpMode mode { ArpMode::Up };
    int range { 0 };     // 0..3 -> 1..4 octaves
    int rate { 5 };      // 0..7 -> 1/4, 1/8D, 1/4T, 1/8, 1/8T, 1/16, 1/16T, 1/32
    int gate { 75 };     // 10..100 %
    bool hold { false };
};

struct Osc1Params
{
    Wave1 wave { Wave1::SuperSaw };
    int ctrl1 { 0 }; // detune, offset, cutoff, harmonics, width or shape (0 as on JE-8086)
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
    int delaySync { 0 }; // 0 = Free, 1..11 = 1/32..1/2 synced to host BPM
};

// JP-8080 velocity matrix (VELOCITY on plus a depth per destination, -127 to
// 127): each destination takes depth x g(velocity) in its own units, g from -1
// at velocity 0 through about -0.2 at 64 to +1 at 127 (mapping::velocityMatrixScale).
struct VelocityMatrix
{
    bool on { false };
    int lfo1Rate { 0 }, lfo1Fade { 0 }, lfo2Rate { 0 }, xmod { 0 }, balance { 0 };
    int pitchLfo { 0 }, pitchLfo2 { 0 }, pitchEnvDepth { 0 }, pitchAttack { 0 }, pitchDecay { 0 };
    int osc1Ctrl1 { 0 }, osc1Ctrl2 { 0 }, osc2Range { 0 }, osc2Fine { 0 }, osc2Ctrl1 { 0 }, osc2Ctrl2 { 0 };
    int cutoff { 0 }, resonance { 0 }, keyFollow { 0 }, filterLfo { 0 }, filterLfo2 { 0 };
    int envDepth { 0 }, filterAttack { 0 }, filterDecay { 0 }, filterSustain { 0 }, filterRelease { 0 };
    int level { 0 }, ampLfo { 0 }, ampLfo2 { 0 }, ampAttack { 0 }, ampDecay { 0 }, ampSustain { 0 }, ampRelease { 0 };
    int bass { 0 }, treble { 0 }, multiFx { 0 }, delayTime { 0 }, delayFeedback { 0 }, delayLevel { 0 }, portamento { 0 };
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
    int velocityAmp { 0 }, velocityFilter { 0 }; // [measured] no effect at the JE-8086 defaults
    int aftertouchCutoff { 0 };                  // -64..63, bipolar filter cutoff offset at full channel pressure
    int aftertouchLfo2 { 0 };                    // 0..127, scales channel pressure into LFO 2 depth alongside mod wheel
    // JP-8080 only: the JP-8000 has no unison switch.
    bool unison { false };
    int unisonDetune { 12 }; // cents
    int unisonSpread { 0 };  // 0..100 %, stereo pan spread of unison voices
    ArpParams arp;
    VelocityMatrix velocityMatrix;
};

// The patch as one voice plays it: the velocity matrix's offsets added at the
// key's velocity (0-1), each clamped to its control's range.
Patch withVelocity (const Patch& patch, double velocity) noexcept;
} // namespace trancera
