// The patch as a flat list of front-panel controls, for every front end (the
// Rack Extension, a plug-in, a test host). Each control is a whole number
// between its own minimum and maximum, as the JP-8000's panel and SysEx hold
// it; `steps` is the number of positions of a choice and 0 for a knob. The
// ids never change once published: front ends store them in songs and
// patches, so a new control is appended.
#pragma once

#include "HypraPatch.h"

#include <array>
#include <cstddef>

namespace hypra
{
struct ControlInfo
{
    const char* id;
    const char* name;
    int min, max, initial;
    int steps; // a choice's positions, 0 for a knob
};

enum Control : int
{
    kOsc1Wave, kOsc1Ctrl1, kOsc1Ctrl2,
    kOsc2Wave, kOsc2Sync, kOsc2Range, kOsc2Fine, kOsc2Width, kOsc2Pwm,
    kRing, kXmod, kBalance, kModDestination,
    kPitchLfo, kPitchEnvDepth, kPitchEnvAttack, kPitchEnvDecay,
    kFilterType, kFilterSlope, kCutoff, kResonance, kKeyFollow, kFilterLfo, kFilterEnvDepth,
    kFilterAttack, kFilterDecay, kFilterSustain, kFilterRelease,
    kLevel, kAmpLfo, kPanMode, kAmpAttack, kAmpDecay, kAmpSustain, kAmpRelease,
    kLfo1Wave, kLfo1Rate, kLfo1Fade, kLfo2Rate, kLfo2Pitch, kLfo2Filter, kLfo2Amp,
    kBass, kTreble, kChorusType, kChorusLevel, kDelayType, kDelayTime, kDelayFeedback, kDelayLevel,
    kMono, kLegato, kPortamento, kPortamentoTime, kOscShift, kBendRange, kVelocity,
    kControlCount
};

// Initial values are the default Patch's, so a new instance plays as the
// engine's own default patch does.
inline constexpr std::array<ControlInfo, kControlCount> kControls { {
    { "osc1_wave", "Osc 1 Wave", 0, 6, 0, 7 },
    { "osc1_ctrl1", "Osc 1 Control 1", 0, 127, 0, 0 },
    { "osc1_ctrl2", "Osc 1 Control 2", 0, 127, 0, 0 },
    { "osc2_wave", "Osc 2 Wave", 0, 2, 1, 3 },
    { "osc2_sync", "Osc 2 Sync", 0, 1, 0, 2 },
    { "osc2_range", "Osc 2 Range", -25, 25, 0, 0 },
    { "osc2_fine", "Osc 2 Fine", -50, 50, 0, 0 },
    { "osc2_width", "Osc 2 Width", 0, 127, 0, 0 },
    { "osc2_pwm", "Osc 2 PWM", 0, 127, 0, 0 },
    { "ring", "Ring Mod", 0, 1, 0, 2 },
    { "xmod", "Cross Mod", 0, 127, 0, 0 },
    { "balance", "Osc Balance", -64, 63, -64, 0 },
    { "mod_destination", "Mod Destination", 0, 2, 0, 3 },
    { "pitch_lfo", "Pitch LFO", -64, 63, 0, 0 },
    { "pitch_env_depth", "Pitch Env Depth", -64, 63, 0, 0 },
    { "pitch_env_attack", "Pitch Env Attack", 0, 127, 0, 0 },
    { "pitch_env_decay", "Pitch Env Decay", 0, 127, 40, 0 },
    { "filter_type", "Filter Type", 0, 2, 2, 3 },
    { "filter_slope", "Filter Slope", 0, 1, 1, 2 },
    { "cutoff", "Cutoff", 0, 127, 127, 0 },
    { "resonance", "Resonance", 0, 127, 0, 0 },
    { "key_follow", "Key Follow", -64, 63, 0, 0 },
    { "filter_lfo", "Filter LFO", -64, 63, 0, 0 },
    { "filter_env_depth", "Filter Env Depth", -64, 63, 0, 0 },
    { "filter_attack", "Filter Attack", 0, 127, 0, 0 },
    { "filter_decay", "Filter Decay", 0, 127, 64, 0 },
    { "filter_sustain", "Filter Sustain", 0, 127, 127, 0 },
    { "filter_release", "Filter Release", 0, 127, 5, 0 },
    { "level", "Amp Level", 0, 127, 100, 0 },
    { "amp_lfo", "Amp LFO", -64, 63, 0, 0 },
    { "pan_mode", "Pan Mode", 0, 2, 0, 3 },
    { "amp_attack", "Amp Attack", 0, 127, 0, 0 },
    { "amp_decay", "Amp Decay", 0, 127, 64, 0 },
    { "amp_sustain", "Amp Sustain", 0, 127, 127, 0 },
    { "amp_release", "Amp Release", 0, 127, 5, 0 },
    { "lfo1_wave", "LFO 1 Wave", 0, 3, 0, 4 },
    { "lfo1_rate", "LFO 1 Rate", 0, 127, 48, 0 },
    { "lfo1_fade", "LFO 1 Fade", 0, 127, 0, 0 },
    { "lfo2_rate", "LFO 2 Rate", 0, 127, 60, 0 },
    { "lfo2_pitch", "LFO 2 Pitch", -64, 63, 8, 0 },
    { "lfo2_filter", "LFO 2 Filter", -64, 63, 0, 0 },
    { "lfo2_amp", "LFO 2 Amp", -64, 63, 0, 0 },
    { "bass", "Tone Bass", -64, 63, 0, 0 },
    { "treble", "Tone Treble", -64, 63, 0, 0 },
    { "chorus_type", "Effect Type", 0, 11, 0, 12 },
    { "chorus_level", "Effect Level", 0, 127, 0, 0 },
    { "delay_type", "Delay Type", 0, 4, 0, 5 },
    { "delay_time", "Delay Time", 0, 127, 64, 0 },
    { "delay_feedback", "Delay Feedback", 0, 127, 40, 0 },
    { "delay_level", "Delay Level", 0, 127, 0, 0 },
    { "mono", "Mono", 0, 1, 0, 2 },
    { "legato", "Legato", 0, 1, 0, 2 },
    { "portamento", "Portamento", 0, 1, 0, 2 },
    { "portamento_time", "Portamento Time", 0, 127, 11, 0 },
    { "osc_shift", "Osc Shift", -2, 2, 0, 0 },
    { "bend_range", "Bend Range", 0, 24, 2, 0 },
    { "velocity", "Velocity", 0, 127, 0, 0 },
} };

// The patch with one control set; out-of-range values are clamped.
void applyControl (Patch& patch, int control, int value) noexcept;
// A control's value in a patch.
[[nodiscard]] int controlValue (const Patch& patch, int control) noexcept;
} // namespace hypra
