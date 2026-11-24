#include "TranceraEngine.h"

namespace trancera
{
namespace
{
bool unison (const Patch& p) noexcept
{
    return p.unison && p.osc1.wave != Wave1::FeedbackOsc;
}

// [measured] LEGATO alone does not make the part monophonic: JE-8086 plays a
// three-note chord on Velo Syncoid (MONO off, LEGATO on) as three voices
// (Tools/blackbox/variants.py, 2026-11-23). It shapes MONO's retriggers only.
bool monophonic (const Patch& p) noexcept
{
    return p.mono || p.unison || p.osc1.wave == Wave1::FeedbackOsc;
}

struct EnvLfoFlags
{
    bool ampActive, ampEdge;
    bool filtActive, filtEdge;
    bool filtUseEnvSpeed;
};

EnvLfoFlags envLfoFlags (const Patch& p, bool lfoEdge, double modWheel) noexcept
{
    const bool ampLfo1 = p.pan == PanMode::Off && p.ampLfo != 0;
    const bool ampLfo2 = p.lfo2.amp != 0 && modWheel > 0.0;
    const bool filtLfo1 = p.filter.lfoDepth != 0;
    const bool filtLfo2 = p.lfo2.filter != 0;
    return {
        ampLfo1 || ampLfo2,
        ampLfo1 && lfoEdge,
        filtLfo1 || filtLfo2,
        filtLfo1 && lfoEdge,
        p.filter.envDepth != 0
    };
}
} // namespace

void Engine::prepare (double hostRate, std::uint32_t seed)
{
    seed_ = seed;
    hostRate_ = hostRate;
    resampler_.prepare (hostRate);
    output_.prepare (OutputNode::Rack);
    for (Voice& v : voices_)
        v.oscillators.prepare();
    for (PartState& part : parts_)
        part.delay.prepare();
    setPatch (Part::Upper, parts_[0].patch);
    setPatch (Part::Lower, parts_[1].patch);
    reset();
}

void Engine::reset() noexcept
{
    random_ = Random (seed_);
    for (Voice& v : voices_)
    {
        v.active = v.held = v.sustained = false;
        v.portaDelta = 0.0;
        v.portaRemain = 0;
        v.fadePhase = 16384;
        v.qSnapTicks = 0;
        v.ampEnv.reset();
        v.filterEnv.reset();
        v.pitchEnv.reset();
        v.filter.reset();
        v.ampBaseQ23 = v.ampTargetQ23 = v.ampSmoothedQ23 = 0;
        v.ampAlpha = splitCoeff15 (mapping::envSmootherAlphaQ23 (11));
        v.ampSplit = {};
        v.ampMoving = false;
    }
    std::uint32_t partSeed = seed_ * 31u + 17u;
    for (PartState& s : parts_)
    {
        s.lfoRandom = Random (partSeed++);
        s.keyCount = 0;
        s.lfo1Phase = s.lfo2Phase = 0;
        s.lfoHeld = s.lfo1 = s.lfo2 = 0.0;
        s.effAmpLfo = s.patch.ampLfo;
        s.lastPitch = 60.0;
        s.hasPlayed = false;
        s.lfoEdge = false;
        s.panLQ23 = s.panTargetLQ23 = PartState::kPanCenterQ23;
        s.panRQ23 = s.panTargetRQ23 = PartState::kPanCenterQ23;
        s.panLSplit = splitCoeff15 (s.panLQ23);
        s.panRSplit = splitCoeff15 (s.panRQ23);
        s.tone.reset();
        s.delay.reset();
        s.chorus.reset();
    }
    output_.reset();
    resampler_.reset();
    bend_ = mod_ = 0.0;
    sustain_ = false;
    everTriggered_ = false;
    order_ = 0;
    untilControl_ = kEnvelopeInterval;
    silentTicks_ = 0;
}

Engine::Span Engine::span (int part) const noexcept
{
    if (mode_ == KeyMode::Dual)
        return { part * maxVoices / 2, maxVoices / 2 };
    return { 0, part == 0 ? maxVoices : 0 };
}

void Engine::setMode (KeyMode mode) noexcept
{
    if (mode == mode_)
        return;
    mode_ = mode;
    if (everTriggered_)
        silentTicks_ = 0;
    for (Voice& v : voices_)
    {
        v.active = v.held = v.sustained = false;
        v.ampEnv.reset();
        v.filterEnv.reset();
        v.pitchEnv.reset();
    }
    for (PartState& s : parts_)
        s.keyCount = 0;
}

Patch withVelocity (const Patch& patch, double velocity) noexcept
{
    const VelocityMatrix& m = patch.velocityMatrix;
    if (! m.on)
        return patch;
    Patch p = patch;
    const int vIdx = std::clamp (static_cast<int> (std::lround (velocity * 127.0)), 0, 127);
    const int scale = mapping::velocityMatrixFactor (vIdx);
    const auto add = [scale] (int& value, int depth, int lo, int hi)
    {
        if (depth != 0)
            value = std::clamp (value + ((depth * scale) >> 7), lo, hi);
    };
    const auto addDepth = [scale] (int& value, int depth)
    {
        if (depth == 0)
            return;
        const int raw = value + ((depth * scale) >> 7);
        value = value < 0 ? std::clamp (raw, -64, 0) : std::clamp (raw, 0, 63);
    };
    add (p.lfo1.rate, m.lfo1Rate, 0, 127);
    add (p.lfo1.fade, m.lfo1Fade, 0, 127);
    add (p.lfo2.rate, m.lfo2Rate, 0, 127);
    add (p.xmod, m.xmod, 0, 127);
    add (p.balance, m.balance, -64, 63);
    addDepth (p.pitchLfo, m.pitchLfo);
    addDepth (p.lfo2.pitch, m.pitchLfo2);
    addDepth (p.pitchEnvDepth, m.pitchEnvDepth);
    add (p.pitchAttack, m.pitchAttack, 0, 127);
    add (p.pitchDecay, m.pitchDecay, 0, 127);
    add (p.osc1.ctrl1, m.osc1Ctrl1, 0, 127);
    add (p.osc1.ctrl2, m.osc1Ctrl2, 0, 127);
    add (p.osc2.range, m.osc2Range, -25, 25);
    add (p.osc2.fine, m.osc2Fine, -50, 50);
    add (p.osc2.pulseWidth, m.osc2Ctrl1, 0, 127);
    add (p.osc2.pwmDepth, m.osc2Ctrl2, 0, 127);
    add (p.filter.cutoff, m.cutoff, 0, 127);
    add (p.filter.resonance, m.resonance, 0, 127);
    add (p.filter.keyFollow, m.keyFollow, -64, 63);
    addDepth (p.filter.lfoDepth, m.filterLfo);
    addDepth (p.lfo2.filter, m.filterLfo2);
    addDepth (p.filter.envDepth, m.envDepth);
    add (p.filter.env.attack, m.filterAttack, 0, 127);
    add (p.filter.env.decay, m.filterDecay, 0, 127);
    add (p.filter.env.sustain, m.filterSustain, 0, 127);
    add (p.filter.env.release, m.filterRelease, 0, 127);
    add (p.level, m.level, 0, 127);
    if (p.pan == PanMode::Off)
        addDepth (p.ampLfo, m.ampLfo);
    else
        add (p.ampLfo, m.ampLfo, -64, 63);
    addDepth (p.lfo2.amp, m.ampLfo2);
    add (p.amp.attack, m.ampAttack, 0, 127);
    add (p.amp.decay, m.ampDecay, 0, 127);
    add (p.amp.sustain, m.ampSustain, 0, 127);
    add (p.amp.release, m.ampRelease, 0, 127);
    add (p.effects.bass, m.bass, -64, 63);
    add (p.effects.treble, m.treble, -64, 63);
    add (p.effects.chorus, m.multiFx, 0, 127);
    add (p.effects.delayTime, m.delayTime, 0, 127);
    add (p.effects.feedback, m.delayFeedback, 0, 127);
    add (p.effects.delay, m.delayLevel, 0, 127);
    add (p.portamentoTime, m.portamento, 0, 127);
    return p;
}

void Engine::setPatch (Part which, const Patch& patch) noexcept
{
    if (everTriggered_)
        silentTicks_ = 0;
    const int part = static_cast<int> (which);
    PartState& s = parts_[static_cast<std::size_t> (part)];
    const bool becameMono = monophonic (patch)
                            && (! monophonic (s.patch) || unison (patch) != unison (s.patch));
    s.patch = patch;
    s.lfo1Step = mapping::lfo1RateTable[static_cast<std::size_t> (std::clamp (patch.lfo1.rate, 0, 127))];
    s.lfo2Step = mapping::lfo2RateTable[static_cast<std::size_t> (std::clamp (patch.lfo2.rate, 0, 127))];
    s.effAmpLfo = patch.ampLfo;
    const EffectParams& fx = patch.effects;
    s.tone.configure (fx.bass, fx.treble);
    s.delay.configure (fx.delayType, fx.delayTime, fx.feedback, fx.delay);
    s.chorus.configure (fx.chorusType, fx.chorus);
    if (patch.pan == PanMode::Off)
    {
        s.panTargetLQ23 = s.panTargetRQ23 = PartState::kPanCenterQ23;
    }
    else if (patch.pan == PanMode::Manual)
    {
        const int idx = std::clamp (patch.ampLfo + 64, 0, 127);
        s.panTargetLQ23 = static_cast<Q23> (mapping::panLeftTable[static_cast<std::size_t> (idx)]) << 8;
        s.panTargetRQ23 = static_cast<Q23> (mapping::panRightTable[static_cast<std::size_t> (idx)]) << 8;
    }
    else
    {
        const Stereo gains = mapping::panGains (mapping::ampLfoDepth (s.effAmpLfo) * s.lfo1);
        s.panTargetLQ23 = toQ23 (gains.l);
        s.panTargetRQ23 = toQ23 (gains.r);
    }
    if (! s.hasPlayed)
    {
        s.tone.snap();
        s.delay.snap();
        s.chorus.snap();
        if (patch.pan == PanMode::Off || patch.pan == PanMode::Manual)
        {
            s.panLQ23 = s.panTargetLQ23;
            s.panRQ23 = s.panTargetRQ23;
            s.panLSplit = splitCoeff15 (s.panLQ23);
            s.panRSplit = splitCoeff15 (s.panRQ23);
        }
    }

    const Span range = span (part);
    for (int i = range.first; i < range.first + range.count; ++i)
    {
        Voice& v = voices_[static_cast<std::size_t> (i)];
        v.patch = withVelocity (patch, v.velocity);
        const Patch& vp = v.patch;
        v.ampEnv.configure (vp.amp.attack, vp.amp.decay, mapping::ampSustain (vp.amp.sustain), vp.amp.release);
        v.filterEnv.configure (vp.filter.env.attack, vp.filter.env.decay,
                               mapping::filterSustain (vp.filter.env.sustain), vp.filter.env.release);
        v.pitchEnv.configure (vp.pitchAttack, vp.pitchDecay, 0.0, vp.pitchDecay);
        if (becameMono && i - range.first >= (unison (patch) ? range.count : 1))
            release (v);
        if (v.active)
            controlVoice (v, part, false);
    }
}

void Engine::release (Voice& v) noexcept
{
    v.held = v.sustained = false;
    v.ampEnv.release();
    v.filterEnv.release();
    v.pitchEnv.release();
}

void Engine::trigger (Voice& v, int part, int note, int velocity, bool retrigger, bool mayGlide) noexcept
{
    PartState& s = parts_[static_cast<std::size_t> (part)];
    const bool fresh = ! v.active;
    const bool start = fresh || retrigger;
    if (start)
    {
        v.patch = withVelocity (s.patch, velocity / 127.0);
        if (s.patch.velocityMatrix.on)
        {
            const EffectParams& fx = v.patch.effects;
            s.tone.configure (fx.bass, fx.treble);
            s.delay.configure (fx.delayType, fx.delayTime, fx.feedback, fx.delay);
            s.chorus.configure (fx.chorusType, fx.chorus);
            s.lfo1Step = mapping::lfo1RateTable[static_cast<std::size_t> (std::clamp (v.patch.lfo1.rate, 0, 127))];
            s.lfo2Step = mapping::lfo2RateTable[static_cast<std::size_t> (std::clamp (v.patch.lfo2.rate, 0, 127))];
            s.effAmpLfo = v.patch.ampLfo;
            if (s.patch.pan == PanMode::Manual)
            {
                const int idx = std::clamp (s.effAmpLfo + 64, 0, 127);
                s.panTargetLQ23 = static_cast<Q23> (mapping::panLeftTable[static_cast<std::size_t> (idx)]) << 8;
                s.panTargetRQ23 = static_cast<Q23> (mapping::panRightTable[static_cast<std::size_t> (idx)]) << 8;
            }
        }
    }
    const Patch& p = v.patch;
    v.note = note;
    v.order = ++order_;
    v.held = true;
    v.sustained = false;
    v.target = note;
    if (! (p.portamento && mayGlide))
    {
        v.pitch = note;
        v.portaDelta = 0.0;
        v.portaRemain = 0;
    }
    else
    {
        if (fresh || ! monophonic (p))
            v.pitch = s.lastPitch;
        v.portaDelta = v.pitch - v.target;
        if (p.portamentoTime <= 0)
        {
            v.pitch = note;
            v.portaDelta = 0.0;
            v.portaRemain = 0;
        }
        else
        {
            v.portaRemain = 8192;
        }
    }
    if (! start)
    {
        controlVoice (v, part, false);
        return;
    }

    v.velocity = velocity / 127.0;
    const auto fadeIdx = static_cast<std::size_t> (std::clamp (p.lfo1.fade, 0, 127));
    v.fadePhase = std::min (16384, static_cast<int> (mapping::fadeRateTable[fadeIdx]));
    v.qSnapTicks = 2;
    if (fresh)
    {
        v.filter.reset();
        v.ampBaseQ23 = v.ampTargetQ23 = v.ampSmoothedQ23 = 0;
        v.ampAlpha = splitCoeff15 (mapping::envSmootherAlphaQ23 (11));
        v.ampSplit = {};
        v.ampMoving = false;
        v.ampEnv.reset();
        v.filterEnv.reset();
        v.pitchEnv.reset();
    }
    const EnvLfoFlags flags = envLfoFlags (p, s.lfoEdge, mod_);
    v.ampEnv.configure (p.amp.attack, p.amp.decay, mapping::ampSustain (p.amp.sustain), p.amp.release);
    v.ampEnv.trigger (flags.ampActive, flags.ampEdge, true);
    v.filterEnv.configure (p.filter.env.attack, p.filter.env.decay,
                           mapping::filterSustain (p.filter.env.sustain), p.filter.env.release);
    v.filterEnv.trigger (flags.filtActive, flags.filtEdge, flags.filtUseEnvSpeed);
    v.pitchEnv.configure (p.pitchAttack, p.pitchDecay, 0.0, p.pitchDecay);
    v.pitchEnv.trigger();

    if (fresh || ! monophonic (p))
        v.oscillators.trigger (random_, p.osc1.wave, fresh);
    v.active = true;
    controlVoice (v, part, false);
}

void Engine::startMono (int part, int note, int velocity, bool retrigger) noexcept
{
    const PartState& s = parts_[static_cast<std::size_t> (part)];
    const Span range = span (part);
    const int count = unison (s.patch) ? range.count : 1;
    for (int i = 0; i < range.count; ++i)
    {
        Voice& v = voices_[static_cast<std::size_t> (range.first + i)];
        if (i < count)
        {
            v.detune = count > 1 ? s.patch.unisonDetune * (2.0 * i / (count - 1) - 1.0) / 100.0 : 0.0;
            const bool mayGlide = s.hasPlayed && (! s.patch.legato || ! retrigger);
            trigger (v, part, note, velocity, retrigger, mayGlide);
        }
        else if (v.held || v.sustained)
            release (v);
    }
}

void Engine::startPart (int part, int note, int velocity) noexcept
{
    PartState& s = parts_[static_cast<std::size_t> (part)];
    const bool overlapping = s.keyCount > 0;
    int kept = 0;
    for (int i = 0; i < s.keyCount; ++i)
        if (s.keys[static_cast<std::size_t> (i)].note != note)
            s.keys[static_cast<std::size_t> (kept++)] = s.keys[static_cast<std::size_t> (i)];
    s.keys[static_cast<std::size_t> (kept++)] = { note, velocity };
    s.keyCount = kept;

    if (monophonic (s.patch))
        startMono (part, note, velocity, ! (s.patch.legato && overlapping));
    else
    {
        const Span range = span (part);
        Voice* chosen = nullptr;
        int bestClass = 4;
        for (int i = range.first; i < range.first + range.count; ++i)
        {
            Voice& v = voices_[static_cast<std::size_t> (i)];
            const int kind = v.active && v.note == note ? 0 : ! v.active ? 1 : ! v.held ? 2 : 3;
            if (kind < bestClass || (kind == bestClass && chosen != nullptr && v.order < chosen->order))
            {
                bestClass = kind;
                chosen = &v;
            }
        }
        if (chosen != nullptr)
        {
            chosen->detune = 0.0;
            trigger (*chosen, part, note, velocity, true, s.hasPlayed);
        }
    }
    s.lastPitch = note;
    s.hasPlayed = true;
}

void Engine::noteOn (int note, int velocity) noexcept
{
    if (note < 0 || note > 127)
        return;
    if (velocity <= 0)
    {
        noteOff (note);
        return;
    }
    everTriggered_ = true;
    silentTicks_ = 0;
    for (int part = 0; part < partCount(); ++part)
        startPart (part, note, std::min (velocity, 127));
}

void Engine::noteOff (int note) noexcept
{
    for (int part = 0; part < partCount(); ++part)
    {
        PartState& s = parts_[static_cast<std::size_t> (part)];
        const bool wasTop = s.keyCount > 0 && s.keys[static_cast<std::size_t> (s.keyCount - 1)].note == note;
        int kept = 0;
        for (int i = 0; i < s.keyCount; ++i)
            if (s.keys[static_cast<std::size_t> (i)].note != note)
                s.keys[static_cast<std::size_t> (kept++)] = s.keys[static_cast<std::size_t> (i)];
        s.keyCount = kept;

        if (monophonic (s.patch) && wasTop && kept > 0)
        {
            const HeldNote back = s.keys[static_cast<std::size_t> (kept - 1)];
            startMono (part, back.note, back.velocity, ! s.patch.legato);
            s.lastPitch = back.note;
            continue;
        }
        const Span range = span (part);
        for (int i = range.first; i < range.first + range.count; ++i)
        {
            Voice& v = voices_[static_cast<std::size_t> (i)];
            if (! v.held || v.note != note)
                continue;
            if (sustain_)
            {
                v.held = false;
                v.sustained = true;
            }
            else
                release (v);
        }
    }
}

void Engine::allNotesOff() noexcept
{
    for (Voice& v : voices_)
        release (v);
    for (PartState& s : parts_)
        s.keyCount = 0;
}

void Engine::sustain (bool down) noexcept
{
    sustain_ = down;
    if (! down)
        for (Voice& v : voices_)
            if (v.sustained)
                release (v);
}

void Engine::setTune (double cents) noexcept
{
    const double t = std::clamp (cents, -100.0, 100.0) / 100.0;
    if (t == tune_)
        return;
    tune_ = t;
    refreshVoices();
}

void Engine::pitchBend (double value) noexcept
{
    const double b = std::clamp (value, -1.0, 1.0);
    if (b == bend_)
        return;
    bend_ = b;
    refreshVoices();
}

void Engine::modWheel (double value) noexcept
{
    const double m = std::clamp (value, 0.0, 1.0);
    if (m == mod_)
        return;
    mod_ = m;
    refreshVoices();
}

void Engine::refreshVoices() noexcept
{
    for (int part = 0; part < partCount(); ++part)
    {
        const Span range = span (part);
        for (int i = range.first; i < range.first + range.count; ++i)
            if (Voice& v = voices_[static_cast<std::size_t> (i)]; v.active)
                controlVoice (v, part, false);
    }
}

int Engine::activeVoices() const noexcept
{
    int count = 0;
    for (const Voice& v : voices_)
        count += v.active ? 1 : 0;
    return count;
}

void Engine::controlVoice (Voice& v, int part, bool advanceTick) noexcept
{
    const PartState& s = parts_[static_cast<std::size_t> (part)];
    const Patch& p = v.patch;
    if (advanceTick)
    {
        if (v.portaRemain > 0)
        {
            const auto portaIdx = static_cast<std::size_t> (std::clamp (p.portamentoTime, 0, 127));
            v.portaRemain = std::max (0, v.portaRemain - static_cast<int> (mapping::portamentoRate[portaIdx]));
            v.pitch = v.target + v.portaDelta * (static_cast<double> (v.portaRemain) / 8192.0);
        }
        if (v.fadePhase < 16384)
        {
            const auto fadeIdx = static_cast<std::size_t> (std::clamp (p.lfo1.fade, 0, 127));
            v.fadePhase = std::min (16384, v.fadePhase + static_cast<int> (mapping::fadeRateTable[fadeIdx]));
        }
        const EnvLfoFlags flags = envLfoFlags (p, s.lfoEdge, mod_);
        v.ampEnv.tick100Hz (flags.ampActive, flags.ampEdge, true);
        v.filterEnv.tick100Hz (flags.filtActive, flags.filtEdge, flags.filtUseEnvSpeed);
        v.pitchEnv.tick100Hz();
        if (v.qSnapTicks > 0)
            --v.qSnapTicks;
    }

    const double fade = mapping::fadeLevelFromPhase (v.fadePhase);
    const double lfo = s.lfo1 * fade;
    const double pitchEnv = p.pitchEnvDepth != 0 ? v.pitchEnv.value() : 0.0;

    const double semis = mapping::pitchLfoSemitones (p.pitchLfo) * lfo
                         + mapping::pitchEnvSemitones (p.pitchEnvDepth) * pitchEnv;
    const bool toXmod = p.destination == ModDestination::XMod;

    const double common = v.pitch + v.detune + 12.0 * std::clamp (p.oscShift, -2, 2)
                          + bend_ * (bend_ >= 0.0 ? p.bendUp : p.bendDown)
                          + mod_ * mapping::pitchLfoSemitones (p.lfo2.pitch) * s.lfo2
                          + (part == 1 ? partDetune_ / 100.0 : 0.0) + tune_;
    const double pitch1 = common + (p.destination == ModDestination::Osc1And2 ? semis : 0.0);
    const double pitch2 = common + mapping::osc2Semitones (p.osc2) + (toXmod ? 0.0 : semis);

    OscillatorPair::Controls c;
    c.wave1 = p.osc1.wave;
    c.wave2 = p.osc2.wave;
    c.hz1 = 440.0 * std::exp2 ((pitch1 - 69.0) / 12.0);
    c.hz2 = 440.0 * std::exp2 ((pitch2 - 69.0) / 12.0);
    c.ctrl1 = unit (p.osc1.ctrl1);
    c.ctrl2 = unit (p.osc1.ctrl2);
    switch (p.osc1.wave)
    {
        case Wave1::Square:
        case Wave1::TriangleMod:
        case Wave1::Saw:
        case Wave1::Triangle:
            c.ctrl1 = std::clamp (c.ctrl1 + c.ctrl2 * lfo, 0.0, 1.0);
            break;
        case Wave1::SuperSaw:
        case Wave1::Noise:
        case Wave1::FeedbackOsc:
            break;
    }
    c.width2 = std::clamp (unit (p.osc2.pulseWidth) + unit (p.osc2.pwmDepth) * lfo, 0.0, 1.0);
    c.xmod = unit (p.xmod);
    if (toXmod)
        c.xmod = std::clamp (c.xmod + mapping::xmodModDepth (p.pitchLfo) * lfo + mapping::xmodModDepth (p.pitchEnvDepth) * pitchEnv, 0.0, 1.0);
    c.gain2 = mapping::balanceRatio (p.balance);
    c.gain1 = 1.0 - c.gain2;
    c.sync = p.osc2.sync;
    c.ring = p.ring;
    v.oscillators.configure (c);

    const double velUnit = (p.velocityFilter != 0 || p.velocityAmp != 0)
                               ? mapping::velocityCurveUnit (static_cast<int> (std::lround (v.velocity * 127.0)))
                               : v.velocity;
    const double velocityDepth = bipolar (p.velocityFilter) * (velUnit - 0.5) * 5.0;
    const double octaves = mapping::cutoffOctaves (p.filter.cutoff)
                           + mapping::keyFollow (p.filter.keyFollow)
                               * (v.pitch + 12.0 * std::clamp (p.oscShift, -2, 2) - 60.0) / 12.0
                           + mapping::filterEnvSemitones (p.filter.envDepth) / 12.0 * v.filterEnv.value()
                           + mapping::filterLfoSemitones (p.filter.lfoDepth) / 12.0 * lfo
                           + mod_ * mapping::filterLfoSemitones (p.lfo2.filter) / 12.0 * s.lfo2
                           + velocityDepth;
    const double fCoeff = mapping::cutoffCoefficient (octaves);
    const double res = unit (p.filter.resonance);
    const Q23 qAlphaQ23 = v.qSnapTicks > 0 ? (7 * (kQ23One >> 3)) : (1 << 14);
    v.filter.configureQ23 (toQ23 (fCoeff), toQ23 (mapping::resonanceQHalf (res, fCoeff)),
                           p.filter.steep ? kQ23Max : toQ23 (mapping::resonance12AmpComp (res)),
                           p.filter.type, p.filter.steep,
                           v.filterEnv.smootherAlphaQ23(), qAlphaQ23);

    double ampBase = 0.5 * mapping::balanceAmpComp (p.balance) * mapping::levelCurve (p.level)
                     * (1.0 - unit (p.velocityAmp) * (1.0 - velUnit));
    if (p.pan == PanMode::Off)
        ampBase *= std::max (0.0, 1.0 + mapping::ampLfoDepth (p.ampLfo) * lfo);
    ampBase *= std::max (0.0, 1.0 + mod_ * mapping::ampLfoDepth (p.lfo2.amp) * s.lfo2);
    if (unison (p))
        ampBase *= 1.25 / std::sqrt (static_cast<double> (span (part).count));
    v.ampBaseQ23 = toQ23 (ampBase);
    v.ampTargetQ23 = static_cast<Q23> ((static_cast<std::int64_t> (v.ampBaseQ23) * v.ampEnv.valueQ14()) >> 14);
    v.ampAlpha = splitCoeff15 (v.ampEnv.smootherAlphaQ23());
    v.ampMoving = v.ampSmoothedQ23 != v.ampTargetQ23;
}

void Engine::control() noexcept
{
    for (int part = 0; part < partCount(); ++part)
    {
        PartState& s = parts_[static_cast<std::size_t> (part)];
        const std::uint16_t prevPhase = s.lfo1Phase;
        s.lfo1Phase = static_cast<std::uint16_t> (s.lfo1Phase + s.lfo1Step);
        const bool wrapped = s.lfo1Phase < prevPhase;
        const bool halfToggle = wrapped || (((prevPhase ^ s.lfo1Phase) & 0x8000u) != 0u);
        if (halfToggle)
            s.lfoHeld = s.lfoRandom.bipolarValue();

        switch (s.patch.lfo1.wave)
        {
            case LfoWave::Triangle:
            {
                const std::uint16_t shifted = static_cast<std::uint16_t> (s.lfo1Phase + 0x4000u);
                const std::int32_t tri = shifted < 32768u ? (static_cast<std::int32_t> (shifted) * 2 - 32768)
                                                          : (98304 - static_cast<std::int32_t> (shifted) * 2);
                s.lfo1 = static_cast<double> (tri) * (1.0 / 32768.0);
                s.lfoEdge = false;
                break;
            }
            case LfoWave::Saw:
                s.lfo1 = 1.0 - static_cast<double> (s.lfo1Phase) * (1.0 / 32768.0);
                s.lfoEdge = wrapped;
                break;
            case LfoWave::Square:
                s.lfo1 = (s.lfo1Phase & 0x8000u) == 0u ? 1.0 : -1.0;
                s.lfoEdge = halfToggle;
                break;
            case LfoWave::SampleHold:
                s.lfo1 = s.lfoHeld;
                s.lfoEdge = halfToggle;
                break;
        }
        s.lfo2Phase = static_cast<std::uint16_t> (s.lfo2Phase + s.lfo2Step);
        const std::uint16_t lfo2Shifted = static_cast<std::uint16_t> (s.lfo2Phase + 0x4000u);
        const std::int32_t tri2 = lfo2Shifted < 32768u ? (static_cast<std::int32_t> (lfo2Shifted) * 2 - 32768)
                                                         : (98304 - static_cast<std::int32_t> (lfo2Shifted) * 2);
        s.lfo2 = static_cast<double> (tri2) * (1.0 / 32768.0);

        if (s.patch.pan == PanMode::Off)
        {
            s.panTargetLQ23 = s.panTargetRQ23 = PartState::kPanCenterQ23;
        }
        else if (s.patch.pan == PanMode::Manual)
        {
            const int idx = std::clamp (s.effAmpLfo + 64, 0, 127);
            s.panTargetLQ23 = static_cast<Q23> (mapping::panLeftTable[static_cast<std::size_t> (idx)]) << 8;
            s.panTargetRQ23 = static_cast<Q23> (mapping::panRightTable[static_cast<std::size_t> (idx)]) << 8;
        }
        else
        {
            const Stereo gains = mapping::panGains (mapping::ampLfoDepth (s.effAmpLfo) * s.lfo1);
            s.panTargetLQ23 = toQ23 (gains.l);
            s.panTargetRQ23 = toQ23 (gains.r);
        }

        const Span range = span (part);
        for (int i = range.first; i < range.first + range.count; ++i)
            if (Voice& v = voices_[static_cast<std::size_t> (i)]; v.active)
                controlVoice (v, part, true);
    }
}

void Engine::renderChunk (Stereo* out, int n) noexcept
{
    Q23 voiceBuf[maxVoices][kChunkSize];
    std::int32_t pannedL[kChunkSize];
    std::int32_t pannedR[kChunkSize];

    bool anyActiveInChunk = false;
    const int nParts = partCount();
    for (int part = 0; part < nParts; ++part)
    {
        PartState& s = parts_[static_cast<std::size_t> (part)];
        const Span range = span (part);

        int fullIdx[maxVoices];
        int fullCount = 0;
        int tailIdx[maxVoices];
        int tailLen[maxVoices];
        int tailCount = 0;

        for (int i = range.first; i < range.first + range.count; ++i)
        {
            Voice& v = voices_[static_cast<std::size_t> (i)];
            if (! v.active)
                continue;
            const bool envIdle = v.ampEnv.idle();
            if (! envIdle)
            {
                anyActiveInChunk = true;
                fullIdx[fullCount++] = i;
                continue;
            }
            if (! v.ampMoving)
            {
                if (v.ampSmoothedQ23 <= 8)
                {
                    v.active = v.held = v.sustained = false;
                }
                else
                {
                    anyActiveInChunk = true;
                    tailIdx[tailCount] = i;
                    tailLen[tailCount] = n;
                    ++tailCount;
                }
                continue;
            }
            if (v.ampSmoothedQ23 <= 8)
            {
                const Q23 nextAmp = sat24 (v.ampSmoothedQ23
                                           + mulSplit15 (v.ampTargetQ23 - v.ampSmoothedQ23, v.ampAlpha.hi, v.ampAlpha.lo));
                if (nextAmp == v.ampSmoothedQ23)
                {
                    v.ampMoving = false;
                }
                else
                {
                    v.ampSmoothedQ23 = nextAmp;
                    v.ampSplit = splitCoeff15 (nextAmp);
                }
                v.active = v.held = v.sustained = false;
                continue;
            }
            // Env is idle and ampSmoothedQ23 > 8 and ampMoving: count exact active samples before <= 8.
            anyActiveInChunk = true;
            Q23 cur = v.ampSmoothedQ23;
            const Q23 tgt = v.ampTargetQ23;
            const std::int32_t aHi = v.ampAlpha.hi, aLo = v.ampAlpha.lo;
            int len = 0;
            bool stoppedEarly = false;
            while (len < n)
            {
                const Q23 nextAmp = sat24 (cur + mulSplit15 (tgt - cur, aHi, aLo));
                if (nextAmp == cur)
                {
                    len = n;
                    break;
                }
                cur = nextAmp;
                if (cur <= 8)
                {
                    stoppedEarly = true;
                    break;
                }
                ++len;
            }
            tailIdx[tailCount] = i;
            tailLen[tailCount] = stoppedEarly ? len : n;
            ++tailCount;
        }

        int oscPos = 0;
        while (oscPos + 4 <= fullCount)
        {
            OscillatorPair* const ops[4] {
                &voices_[static_cast<std::size_t> (fullIdx[oscPos])].oscillators,
                &voices_[static_cast<std::size_t> (fullIdx[oscPos + 1])].oscillators,
                &voices_[static_cast<std::size_t> (fullIdx[oscPos + 2])].oscillators,
                &voices_[static_cast<std::size_t> (fullIdx[oscPos + 3])].oscillators
            };
            if (OscillatorPair::canProcess4SuperSaw (ops))
            {
                Q23* const bufs[4] { voiceBuf[oscPos], voiceBuf[oscPos + 1], voiceBuf[oscPos + 2], voiceBuf[oscPos + 3] };
                OscillatorPair::process4SuperSawBlockQ23 (ops, bufs, n);
                oscPos += 4;
                continue;
            }
            ops[0]->processBlockQ23 (voiceBuf[oscPos], n, random_);
            ++oscPos;
        }
        while (oscPos < fullCount)
        {
            voices_[static_cast<std::size_t> (fullIdx[oscPos])].oscillators.processBlockQ23 (voiceBuf[oscPos], n, random_);
            ++oscPos;
        }

        int fltPos = 0;
        while (fltPos + 4 <= fullCount)
        {
            Voice* const v0 = &voices_[static_cast<std::size_t> (fullIdx[fltPos])];
            Voice* const v1 = &voices_[static_cast<std::size_t> (fullIdx[fltPos + 1])];
            Voice* const v2 = &voices_[static_cast<std::size_t> (fullIdx[fltPos + 2])];
            Voice* const v3 = &voices_[static_cast<std::size_t> (fullIdx[fltPos + 3])];
            const FilterType t = v0->patch.filter.type;
            const bool steep = v0->patch.filter.steep;
            if (v0->filter.isFastBatchSafe()
                && v1->filter.isFastBatchSafe() && v1->patch.filter.type == t && v1->patch.filter.steep == steep
                && v2->filter.isFastBatchSafe() && v2->patch.filter.type == t && v2->patch.filter.steep == steep
                && v3->filter.isFastBatchSafe() && v3->patch.filter.type == t && v3->patch.filter.steep == steep)
            {
                Filter* const fs[4] { &v0->filter, &v1->filter, &v2->filter, &v3->filter };
                Q23* const bufs[4] { voiceBuf[fltPos], voiceBuf[fltPos + 1], voiceBuf[fltPos + 2], voiceBuf[fltPos + 3] };
                Filter::process4BlockQ23 (fs, bufs, n, t, steep);
                fltPos += 4;
                continue;
            }
            v0->filter.processBlockQ23 (voiceBuf[fltPos], n);
            ++fltPos;
        }
        while (fltPos < fullCount)
        {
            voices_[static_cast<std::size_t> (fullIdx[fltPos])].filter.processBlockQ23 (voiceBuf[fltPos], n);
            ++fltPos;
        }

        std::int32_t voiceSum[kChunkSize] {};
        for (int k = 0; k < fullCount; ++k)
        {
            Voice& v = voices_[static_cast<std::size_t> (fullIdx[k])];
            const Q23* const buf = voiceBuf[k];
            if (! v.ampMoving)
            {
                const std::int32_t aHi = v.ampSplit.hi, aLo = v.ampSplit.lo;
                for (int j = 0; j < n; ++j)
                    voiceSum[j] += mulSplit15 (buf[j], aHi, aLo);
            }
            else
            {
                Q23 cur = v.ampSmoothedQ23;
                const Q23 tgt = v.ampTargetQ23;
                const std::int32_t alHi = v.ampAlpha.hi, alLo = v.ampAlpha.lo;
                Coeff15 sp = v.ampSplit;
                bool mob = true;
                for (int j = 0; j < n; ++j)
                {
                    if (mob)
                    {
                        const Q23 nextAmp = sat24 (cur + mulSplit15 (tgt - cur, alHi, alLo));
                        if (nextAmp == cur)
                            mob = false;
                        else
                        {
                            cur = nextAmp;
                            sp = splitCoeff15 (cur);
                        }
                    }
                    voiceSum[j] += mulSplit15 (buf[j], sp.hi, sp.lo);
                }
                v.ampSmoothedQ23 = cur;
                v.ampSplit = sp;
                v.ampMoving = mob;
            }
        }

        for (int t = 0; t < tailCount; ++t)
        {
            Voice& v = voices_[static_cast<std::size_t> (tailIdx[t])];
            const int activeLen = tailLen[t];
            Q23 tBuf[kChunkSize];
            if (activeLen > 0)
            {
                v.oscillators.processBlockQ23 (tBuf, activeLen, random_);
                v.filter.processBlockQ23 (tBuf, activeLen);
            }
            if (! v.ampMoving)
            {
                const std::int32_t aHi = v.ampSplit.hi, aLo = v.ampSplit.lo;
                for (int j = 0; j < activeLen; ++j)
                    voiceSum[j] += mulSplit15 (tBuf[j], aHi, aLo);
            }
            else
            {
                Q23 cur = v.ampSmoothedQ23;
                const Q23 tgt = v.ampTargetQ23;
                const std::int32_t alHi = v.ampAlpha.hi, alLo = v.ampAlpha.lo;
                Coeff15 sp = v.ampSplit;
                bool mob = true;
                for (int j = 0; j < activeLen; ++j)
                {
                    if (mob)
                    {
                        const Q23 nextAmp = sat24 (cur + mulSplit15 (tgt - cur, alHi, alLo));
                        if (nextAmp == cur)
                            mob = false;
                        else
                        {
                            cur = nextAmp;
                            sp = splitCoeff15 (cur);
                        }
                    }
                    voiceSum[j] += mulSplit15 (tBuf[j], sp.hi, sp.lo);
                }
                if (activeLen < n)
                {
                    const Q23 endAmp = sat24 (cur + mulSplit15 (tgt - cur, alHi, alLo));
                    cur = endAmp;
                    sp = splitCoeff15 (cur);
                    v.active = v.held = v.sustained = false;
                }
                v.ampSmoothedQ23 = cur;
                v.ampSplit = sp;
                v.ampMoving = mob;
            }
        }

        Q23 toned[kChunkSize], fxL[kChunkSize], fxR[kChunkSize];
        for (int j = 0; j < n; ++j)
            toned[j] = sat24 (voiceSum[j] * 2);
        s.tone.processBlockSaturatingQ23 (toned, n);
        s.delay.processBlockMixQ23 (toned, fxL, fxR, n);
        s.chorus.processBlockMixQ23 (fxL, fxR, n);

        if (s.panLQ23 == s.panTargetLQ23 && s.panRQ23 == s.panTargetRQ23)
        {
            const std::int32_t lHi = s.panLSplit.hi, lLo = s.panLSplit.lo;
            const std::int32_t rHi = s.panRSplit.hi, rLo = s.panRSplit.lo;
            if (part == 0)
            {
                for (int j = 0; j < n; ++j)
                {
                    pannedL[j] = mulSplit15 (fxL[j], lHi, lLo);
                    pannedR[j] = mulSplit15 (fxR[j], rHi, rLo);
                }
            }
            else
            {
                for (int j = 0; j < n; ++j)
                {
                    pannedL[j] += mulSplit15 (fxL[j], lHi, lLo);
                    pannedR[j] += mulSplit15 (fxR[j], rHi, rLo);
                }
            }
        }
        else
        {
            Q23 pL = s.panLQ23, pR = s.panRQ23;
            const Q23 tL = s.panTargetLQ23, tR = s.panTargetRQ23;
            Coeff15 lSp = s.panLSplit, rSp = s.panRSplit;
            for (int j = 0; j < n; ++j)
            {
                if (pL != tL)
                {
                    pL = smoothStepQ23 (pL, tL, 9);
                    lSp = splitCoeff15 (pL);
                }
                if (pR != tR)
                {
                    pR = smoothStepQ23 (pR, tR, 9);
                    rSp = splitCoeff15 (pR);
                }
                const std::int32_t wl = mulSplit15 (fxL[j], lSp.hi, lSp.lo);
                const std::int32_t wr = mulSplit15 (fxR[j], rSp.hi, rSp.lo);
                if (part == 0)
                {
                    pannedL[j] = wl;
                    pannedR[j] = wr;
                }
                else
                {
                    pannedL[j] += wl;
                    pannedR[j] += wr;
                }
            }
            s.panLQ23 = pL; s.panRQ23 = pR;
            s.panLSplit = lSp; s.panRSplit = rSp;
        }
    }

    StereoQ23 dacWords[kChunkSize];
    for (int j = 0; j < n; ++j)
    {
        const Q23 sumL = sat24 (pannedL[j] * 2) >> 2;
        const Q23 sumR = sat24 (pannedR[j] * 2) >> 2;
        dacWords[j] = { sat24 (static_cast<std::int64_t> (sumL) * 12),
                        sat24 (static_cast<std::int64_t> (sumR) * 12) };
    }
    output_.processBlockQ23 (dacWords, out, n);

    if (anyActiveInChunk)
    {
        silentTicks_ = 0;
    }
    else
    {
        int sCount = silentTicks_;
        for (int j = 0; j < n; ++j)
            sCount = (fastAbs (out[j].l) <= 1.0e-8 && fastAbs (out[j].r) <= 1.0e-8) ? (sCount + 1) : 0;
        silentTicks_ = sCount;
    }
}

void Engine::renderTicks (Stereo* out, int count) noexcept
{
    int pos = 0;
    while (pos < count)
    {
        if (untilControl_ <= 1)
        {
            untilControl_ = kEnvelopeInterval + 1;
            control();
        }
        const int n = std::min ({ count - pos, untilControl_ - 1, kChunkSize });
        untilControl_ -= n;
        renderChunk (out + pos, n);
        pos += n;
    }
}

void Engine::advanceIdleTicks (int ticks) noexcept
{
    if (ticks <= 0)
        return;
    for (int part = 0; part < partCount(); ++part)
        parts_[static_cast<std::size_t> (part)].chorus.advanceIdlePhase (ticks);

    int rem = ticks;
    while (rem > 0)
    {
        if (untilControl_ <= 1)
        {
            untilControl_ = kEnvelopeInterval + 1;
            control();
        }
        const int step = std::min (rem, untilControl_ - 1);
        untilControl_ -= step;
        rem -= step;
    }
}

Stereo Engine::tick() noexcept
{
    Stereo out {};
    renderTicks (&out, 1);
    return out;
}

void Engine::render (float* left, float* right, int frames) noexcept
{
    bool anyActive = false;
    for (const Voice& v : voices_)
        if (v.active) { anyActive = true; break; }

    if (! everTriggered_ || (! anyActive && silentTicks_ >= kIdleTicksThreshold))
    {
        std::fill_n (left, frames, 0.0f);
        std::fill_n (right, frames, 0.0f);
        const int ticks = resampler_.advanceIdle (frames);
        advanceIdleTicks (ticks);
        return;
    }

    const double vol = volume_;
    if (resampler_.isPassthrough())
    {
        Stereo out[kChunkSize];
        int pos = 0;
        while (pos < frames)
        {
            const int n = std::min (frames - pos, kChunkSize);
            renderTicks (out, n);
            for (int i = 0; i < n; ++i)
            {
                left[pos + i] = static_cast<float> (vol * out[i].l);
                right[pos + i] = static_cast<float> (vol * out[i].r);
            }
            pos += n;
        }
        return;
    }

    constexpr int maxTickBuf = 128;
    const int hostBatch = std::max (1, std::min (64, static_cast<int> (96.0 * hostRate_ / kEngineRate)));
    Stereo tickBuf[maxTickBuf];
    int pos = 0;
    while (pos < frames)
    {
        const int sub = std::min (frames - pos, hostBatch);
        const int needed = resampler_.ticksNeededForFrames (sub);
        if (needed <= maxTickBuf)
        {
            renderTicks (tickBuf, needed);
            resampler_.processBlockFromTicks (tickBuf, left + pos, right + pos, sub, vol);
        }
        else
        {
            for (int i = 0; i < sub; ++i)
            {
                const Stereo out = resampler_.process ([this] { return tick(); });
                left[pos + i] = static_cast<float> (vol * out.l);
                right[pos + i] = static_cast<float> (vol * out.r);
            }
        }
        pos += sub;
    }
}
} // namespace trancera
