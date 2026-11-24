#include "TranceraControls.h"

#include <algorithm>

namespace trancera
{
namespace
{
// Each control's field in the patch, as a pointer to the whole number it is.
int* field (Patch& p, int control) noexcept
{
    switch (control)
    {
        case kOsc1Ctrl1: return &p.osc1.ctrl1;
        case kOsc1Ctrl2: return &p.osc1.ctrl2;
        case kOsc2Range: return &p.osc2.range;
        case kOsc2Fine: return &p.osc2.fine;
        case kOsc2Width: return &p.osc2.pulseWidth;
        case kOsc2Pwm: return &p.osc2.pwmDepth;
        case kXmod: return &p.xmod;
        case kBalance: return &p.balance;
        case kPitchLfo: return &p.pitchLfo;
        case kPitchEnvDepth: return &p.pitchEnvDepth;
        case kPitchEnvAttack: return &p.pitchAttack;
        case kPitchEnvDecay: return &p.pitchDecay;
        case kCutoff: return &p.filter.cutoff;
        case kResonance: return &p.filter.resonance;
        case kKeyFollow: return &p.filter.keyFollow;
        case kFilterLfo: return &p.filter.lfoDepth;
        case kFilterEnvDepth: return &p.filter.envDepth;
        case kFilterAttack: return &p.filter.env.attack;
        case kFilterDecay: return &p.filter.env.decay;
        case kFilterSustain: return &p.filter.env.sustain;
        case kFilterRelease: return &p.filter.env.release;
        case kLevel: return &p.level;
        case kAmpLfo: return &p.ampLfo;
        case kAmpAttack: return &p.amp.attack;
        case kAmpDecay: return &p.amp.decay;
        case kAmpSustain: return &p.amp.sustain;
        case kAmpRelease: return &p.amp.release;
        case kLfo1Rate: return &p.lfo1.rate;
        case kLfo1Fade: return &p.lfo1.fade;
        case kLfo2Rate: return &p.lfo2.rate;
        case kLfo2Pitch: return &p.lfo2.pitch;
        case kLfo2Filter: return &p.lfo2.filter;
        case kLfo2Amp: return &p.lfo2.amp;
        case kBass: return &p.effects.bass;
        case kTreble: return &p.effects.treble;
        case kChorusLevel: return &p.effects.chorus;
        case kDelayTime: return &p.effects.delayTime;
        case kDelayFeedback: return &p.effects.feedback;
        case kDelayLevel: return &p.effects.delay;
        case kPortamentoTime: return &p.portamentoTime;
        case kOscShift: return &p.oscShift;
        case kVelocity: return &p.velocityAmp;
        case kUnisonDetune: return &p.unisonDetune;
        case kArpRange: return &p.arp.range;
        case kArpRate: return &p.arp.rate;
        case kArpGate: return &p.arp.gate;
        case kFilterVelocity: return &p.velocityFilter;
        case kAftertouchCutoff: return &p.aftertouchCutoff;
        case kAftertouchLfo2: return &p.aftertouchLfo2;
        case kUnisonSpread: return &p.unisonSpread;
        case kDelaySync: return &p.effects.delaySync;
        default: return nullptr;
    }
}
} // namespace

void applyControl (Patch& p, int control, int value) noexcept
{
    if (control < 0 || control >= kControlCount)
        return;
    const ControlInfo& info = kControls[static_cast<std::size_t> (control)];
    const int v = std::clamp (value, info.min, info.max);
    if (int* f = field (p, control))
    {
        *f = v;
        return;
    }
    switch (control)
    {
        case kOsc1Wave: p.osc1.wave = static_cast<Wave1> (v); break;
        case kOsc2Wave: p.osc2.wave = static_cast<Wave2> (v); break;
        case kOsc2Sync: p.osc2.sync = v != 0; break;
        case kRing: p.ring = v != 0; break;
        case kModDestination: p.destination = static_cast<ModDestination> (v); break;
        case kFilterType: p.filter.type = static_cast<FilterType> (v); break;
        case kFilterSlope: p.filter.steep = v != 0; break;
        case kPanMode: p.pan = static_cast<PanMode> (v); break;
        case kLfo1Wave: p.lfo1.wave = static_cast<LfoWave> (v); break;
        case kChorusType: p.effects.chorusType = static_cast<ChorusType> (v); break;
        case kDelayType: p.effects.delayType = static_cast<DelayType> (v); break;
        case kMono: p.mono = v != 0; break;
        case kLegato: p.legato = v != 0; break;
        case kPortamento: p.portamento = v != 0; break;
        case kBendRange: p.bendUp = p.bendDown = v; break;
        case kUnison: p.unison = v != 0; break;
        case kArp: p.arp.on = v != 0; break;
        case kArpMode: p.arp.mode = static_cast<ArpMode> (v); break;
        case kArpHold: p.arp.hold = v != 0; break;
        case kLfo1Sync: p.lfo1.sync = v != 0; break;
        case kLfo1Retrig: p.lfo1.retrig = v != 0; break;
        default: break;
    }
}

int controlValue (const Patch& patch, int control) noexcept
{
    if (control < 0 || control >= kControlCount)
        return 0;
    Patch& p = const_cast<Patch&> (patch);
    if (const int* f = field (p, control))
        return *f;
    switch (control)
    {
        case kOsc1Wave: return static_cast<int> (p.osc1.wave);
        case kOsc2Wave: return static_cast<int> (p.osc2.wave);
        case kOsc2Sync: return p.osc2.sync ? 1 : 0;
        case kRing: return p.ring ? 1 : 0;
        case kModDestination: return static_cast<int> (p.destination);
        case kFilterType: return static_cast<int> (p.filter.type);
        case kFilterSlope: return p.filter.steep ? 1 : 0;
        case kPanMode: return static_cast<int> (p.pan);
        case kLfo1Wave: return static_cast<int> (p.lfo1.wave);
        case kChorusType: return static_cast<int> (p.effects.chorusType);
        case kDelayType: return static_cast<int> (p.effects.delayType);
        case kMono: return p.mono ? 1 : 0;
        case kLegato: return p.legato ? 1 : 0;
        case kPortamento: return p.portamento ? 1 : 0;
        case kBendRange: return p.bendUp;
        case kUnison: return p.unison ? 1 : 0;
        case kArp: return p.arp.on ? 1 : 0;
        case kArpMode: return static_cast<int> (p.arp.mode);
        case kArpHold: return p.arp.hold ? 1 : 0;
        case kLfo1Sync: return p.lfo1.sync ? 1 : 0;
        case kLfo1Retrig: return p.lfo1.retrig ? 1 : 0;
        default: return 0;
    }
}
} // namespace trancera
