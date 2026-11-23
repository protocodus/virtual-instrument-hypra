#include "HypraEngine.h"

namespace hypra
{
namespace
{
// JP-8080 OM: UNISON cannot be turned on with FEEDBACK OSC.
bool unison (const Patch& p) noexcept
{
    return p.unison && p.osc1.wave != Wave1::FeedbackOsc;
}

bool monophonic (const Patch& p) noexcept
{
    // OM p.56: FEEDBACK OSC sounds one note at a time.
    return p.mono || p.legato || p.unison || p.osc1.wave == Wave1::FeedbackOsc;
}

// [measured, black box] One plain-saw voice at LEVEL 100 peaks at 0.0646 of
// full scale. With the saw at 0.8 and the inferred square law for LEVEL,
// that is this factor.
constexpr double voiceHeadroom = 0.130;
} // namespace

void Engine::prepare (double hostRate, std::uint32_t seed)
{
    seed_ = seed;
    resampler_.prepare (hostRate);
    output_.prepare (OutputNode::Rack);
    dcPole_ = 1.0 - kTwoPi * 4.0 / kEngineRate;
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
        v.ampEnv.reset();
        v.filterEnv.reset();
        v.filter.reset();
        v.gain = v.gainStep = v.dcX = v.dcY = 0.0;
    }
    std::uint32_t partSeed = seed_ * 31u + 17u;
    for (PartState& s : parts_)
    {
        s.lfoRandom = Random (partSeed++);
        s.keyCount = 0;
        s.lfoPhase = s.lfoHeld = s.lfo1 = s.lfo2Phase = s.lfo2 = 0.0;
        s.lastPitch = 60.0;
        s.hasPlayed = false;
        s.panL = s.panR = 1.0;
        s.panStepL = s.panStepR = 0.0;
        s.tone.reset();
        s.delay.reset();
        s.chorus.reset();
    }
    output_.reset();
    resampler_.reset();
    bend_ = mod_ = 0.0;
    sustain_ = false;
    order_ = 0;
    untilControl_ = 0;
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
    for (Voice& v : voices_)
    {
        v.active = v.held = v.sustained = false;
        v.ampEnv.reset();
        v.filterEnv.reset();
    }
    for (PartState& s : parts_)
        s.keyCount = 0;
}

void Engine::setPatch (Part which, const Patch& patch) noexcept
{
    const int part = static_cast<int> (which);
    PartState& s = parts_[static_cast<std::size_t> (part)];
    const bool becameMono = monophonic (patch)
                            && (! monophonic (s.patch) || unison (patch) != unison (s.patch));
    s.patch = patch;
    s.lfoStep = mapping::lfoHz (patch.lfo1.rate) / kControlRate;
    s.lfo2Step = mapping::lfo2Hz (patch.lfo2.rate) / kControlRate;
    s.fade = mapping::fadeSeconds (patch.lfo1.fade);
    s.glideSeconds = patch.portamento ? mapping::portamentoSeconds (patch.portamentoTime) : 0.0;
    const EffectParams& fx = patch.effects;
    s.tone.configure (fx.bass, fx.treble);
    s.delay.configure (fx.delayType, fx.delayTime, fx.feedback, fx.delay);
    if (! s.hasPlayed)
        s.delay.snap();
    s.chorus.configure (fx.chorusType, fx.chorus);

    const Span range = span (part);
    for (int i = range.first; i < range.first + range.count; ++i)
    {
        Voice& v = voices_[static_cast<std::size_t> (i)];
        const auto set = [] (Envelope& env, const EnvParams& e, double sustain)
        {
            env.configure (mapping::attackTau (e.attack), mapping::envelopeTau (e.decay),
                           sustain, mapping::envelopeTau (e.release),
                           mapping::attackHoldSeconds (e.attack), mapping::segmentHoldSeconds (e.decay),
                           mapping::segmentHoldSeconds (e.release));
        };
        set (v.ampEnv, patch.amp, mapping::ampSustain (patch.amp.sustain));
        set (v.filterEnv, patch.filter.env, mapping::filterSustain (patch.filter.env.sustain));
        if (becameMono && i - range.first >= (unison (patch) ? range.count : 1))
            release (v);
    }
}

void Engine::release (Voice& v) noexcept
{
    v.held = v.sustained = false;
    v.ampEnv.release();
    v.filterEnv.release();
}

void Engine::trigger (Voice& v, int part, int note, int velocity, bool retrigger, bool mayGlide) noexcept
{
    PartState& s = parts_[static_cast<std::size_t> (part)];
    const Patch& p = s.patch;
    const bool fresh = ! v.active;
    const bool start = fresh || retrigger;
    v.note = note;
    v.order = ++order_;
    v.held = true;
    v.sustained = false;
    v.target = note;
    if (! (p.portamento && mayGlide))
        v.pitch = note;
    else if (fresh || ! monophonic (p))
        v.pitch = s.lastPitch;
    // [measured] The glide takes the same time whatever the interval.
    const double ticks = s.glideSeconds * kControlRate;
    v.glideStep = ticks > 0.0 ? std::abs (v.target - v.pitch) / ticks : 1.0e9;
    if (! start)
        return;

    v.velocity = velocity / 127.0;
    v.age = 0.0;
    if (fresh)
    {
        v.filter.reset();
        v.gain = v.dcX = v.dcY = 0.0;
        v.ampEnv.reset();
        v.filterEnv.reset();
    }
    const auto set = [] (Envelope& env, const EnvParams& e, double sustain)
    {
        env.configure (mapping::attackTau (e.attack), mapping::envelopeTau (e.decay),
                       sustain, mapping::envelopeTau (e.release),
                       mapping::attackHoldSeconds (e.attack), mapping::segmentHoldSeconds (e.decay),
                       mapping::segmentHoldSeconds (e.release));
        env.trigger();
    };
    set (v.ampEnv, p.amp, mapping::ampSustain (p.amp.sustain));
    set (v.filterEnv, p.filter.env, mapping::filterSustain (p.filter.env.sustain));
    // A mono retrigger keeps its supersaw phases running.
    if (fresh || ! monophonic (p))
        v.oscillators.trigger (random_, p.osc1.wave, fresh);
    v.active = true;
    controlVoice (v, part);
    if (fresh)
    {
        // Start at the level itself; the envelope supplies the attack.
        v.gain += v.gainStep * kControlInterval;
        v.gainStep = 0.0;
    }
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
        // Same key, then a free voice, then the oldest released, then the
        // oldest held: "last-received notes will be given priority" (OM p.48).
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

int Engine::activeVoices() const noexcept
{
    int count = 0;
    for (const Voice& v : voices_)
        count += v.active ? 1 : 0;
    return count;
}

void Engine::controlVoice (Voice& v, int part) noexcept
{
    const PartState& s = parts_[static_cast<std::size_t> (part)];
    const Patch& p = s.patch;
    v.age += 1.0 / kControlRate;
    if (v.pitch != v.target)
        v.pitch = v.target > v.pitch ? std::min (v.target, v.pitch + v.glideStep)
                                     : std::max (v.target, v.pitch - v.glideStep);

    const double fade = mapping::fadeLevel (v.age, s.fade);
    const double lfo = s.lfo1 * fade;

    // [measured] Pitch env decay follows the envelope table back to the
    // played pitch. [inferred] Its attack is taken to be the amp attack's.
    double pitchEnv = 0.0;
    if (p.pitchEnvDepth != 0)
    {
        const double attackTau = mapping::attackTau (p.pitchAttack);
        const double rise = attackTau * 1.6014; // ln (1.2525 / 0.2525)
        if (v.age < rise)
            pitchEnv = Envelope::attackTarget * (1.0 - std::exp (-v.age / std::max (attackTau, 1e-9)));
        else
            pitchEnv = std::exp (-(v.age - rise) / std::max (mapping::envelopeTau (p.pitchDecay), 1e-9));
    }

    const double semis = mapping::pitchLfoSemitones (p.pitchLfo) * lfo
                         + mapping::pitchEnvSemitones (p.pitchEnvDepth) * pitchEnv;
    const bool toXmod = p.destination == ModDestination::XMod;

    const double common = v.pitch + v.detune + 12.0 * std::clamp (p.oscShift, -2, 2)
                          + bend_ * (bend_ >= 0.0 ? p.bendUp : p.bendDown)
                          + mod_ * mapping::pitchLfoSemitones (p.lfo2.pitch) * s.lfo2
                          + (part == 1 ? partDetune_ / 100.0 : 0.0);
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
            c.ctrl1 = std::clamp (c.ctrl1 + c.ctrl2 * 0.5 * (lfo + fade), 0.0, 1.0);
            break;
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
    c.width2 = std::clamp (unit (p.osc2.pulseWidth) + unit (p.osc2.pwmDepth) * 0.5 * (lfo + fade), 0.0, 1.0);
    c.xmod = unit (p.xmod);
    if (toXmod)
        c.xmod = std::clamp (c.xmod + mapping::xmodModDepth (p.pitchLfo) * lfo + mapping::xmodModDepth (p.pitchEnvDepth) * pitchEnv, 0.0, 1.0);
    c.gain1 = mapping::balanceGain (p.balance, true);
    c.gain2 = mapping::balanceGain (p.balance, false);
    c.sync = p.osc2.sync;
    c.ring = p.ring;
    v.oscillators.configure (c);

    const double velocityDepth = bipolar (p.velocityFilter) * (v.velocity - 0.5) * 5.0;
    const double octaves = mapping::cutoffOctaves (p.filter.cutoff)
                           + mapping::keyFollow (p.filter.keyFollow) // [measured] after OSC SHIFT
                               * (v.pitch + 12.0 * std::clamp (p.oscShift, -2, 2) - 60.0) / 12.0
                           + mapping::filterEnvSemitones (p.filter.envDepth) / 12.0 * v.filterEnv.value()
                           + mapping::filterLfoSemitones (p.filter.lfoDepth) / 12.0 * lfo
                           + mod_ * mapping::filterLfoSemitones (p.lfo2.filter) / 12.0 * s.lfo2
                           + velocityDepth;
    v.filter.configure (mapping::cutoffCoefficient (octaves), unit (p.filter.resonance), p.filter.type, p.filter.steep);

    double gain = voiceHeadroom * unit (p.level) * unit (p.level)
                  * (1.0 - unit (p.velocityAmp) * (1.0 - v.velocity));
    if (p.pan == PanMode::Off)
        gain *= 1.0 + mapping::ampLfoDepth (p.ampLfo) * lfo;
    gain *= 1.0 + mod_ * mapping::ampLfoDepth (p.lfo2.amp) * s.lfo2;
    if (unison (p)) // the stack's phases are unrelated, so it adds in power
        gain *= 1.25 / std::sqrt (static_cast<double> (span (part).count));
    v.gainStep = (gain - v.gain) / kControlInterval;
}

void Engine::control() noexcept
{
    for (int part = 0; part < partCount(); ++part)
    {
        PartState& s = parts_[static_cast<std::size_t> (part)];
        s.lfoPhase += s.lfoStep;
        if (s.lfoPhase >= 1.0)
        {
            s.lfoPhase -= 1.0;
            s.lfoHeld = s.lfoRandom.bipolarValue();
        }
        const double p = s.lfoPhase;
        switch (s.patch.lfo1.wave)
        {
            case LfoWave::Triangle: s.lfo1 = osc::triangle (p < 0.75 ? p + 0.25 : p - 0.75); break;
            case LfoWave::Saw: s.lfo1 = 1.0 - 2.0 * p; break; // OM p.63: falls, then jumps up
            case LfoWave::Square: s.lfo1 = p < 0.5 ? 1.0 : -1.0; break;
            case LfoWave::SampleHold: s.lfo1 = s.lfoHeld; break;
        }
        s.lfo2Phase += s.lfo2Step;
        if (s.lfo2Phase >= 1.0)
            s.lfo2Phase -= 1.0;
        s.lfo2 = osc::triangle (s.lfo2Phase < 0.75 ? s.lfo2Phase + 0.25 : s.lfo2Phase - 0.75);

        // [measured] Pan against the centre: the near side gains 3.2 dB at
        // ±32 and stays there, the far side falls to 0.50 at ±32 and to
        // silence at the end (JE-8086, ±32 and ±63/64). Linear between those
        // points. AUTO swings the full width at 63. OM p.62: FADE does not
        // apply.
        double pan = 0.0;
        if (s.patch.pan == PanMode::Manual)
            pan = bipolar (s.patch.ampLfo);
        else if (s.patch.pan == PanMode::Auto)
            pan = mapping::ampLfoDepth (s.patch.ampLfo) / 0.953 * s.lfo1;
        double l = 1.0, r = 1.0;
        if (s.patch.pan != PanMode::Off)
        {
            const double u = std::min (1.0, std::abs (pan)), k = u < 0.5 ? u / 0.5 : (u - 0.5) / 0.5;
            const double nearSide = u < 0.5 ? 1.0 + 0.432 * k : 1.432 + 0.015 * k;
            const double farSide = u < 0.5 ? 1.0 - 0.501 * k : 0.499 * (1.0 - k);
            l = pan < 0.0 ? nearSide : farSide;
            r = pan < 0.0 ? farSide : nearSide;
        }
        s.panStepL = (l - s.panL) / kControlInterval;
        s.panStepR = (r - s.panR) / kControlInterval;

        s.chorus.control();
        const Span range = span (part);
        for (int i = range.first; i < range.first + range.count; ++i)
            if (Voice& v = voices_[static_cast<std::size_t> (i)]; v.active)
                controlVoice (v, part);
    }
}

Stereo Engine::tick() noexcept
{
    if (untilControl_ == 0)
    {
        control();
        untilControl_ = kControlInterval;
    }
    --untilControl_;

    Stereo sum;
    for (int part = 0; part < partCount(); ++part)
    {
        PartState& s = parts_[static_cast<std::size_t> (part)];
        double dry = 0.0;
        const Span range = span (part);
        for (int i = range.first; i < range.first + range.count; ++i)
        {
            Voice& v = voices_[static_cast<std::size_t> (i)];
            if (! v.active)
                continue;
            const double amp = v.ampEnv.process();
            v.filterEnv.process();
            if (v.ampEnv.idle())
            {
                v.active = v.held = v.sustained = false;
                continue;
            }
            const double x = v.oscillators.process (random_);
            v.dcY = x - v.dcX + dcPole_ * v.dcY;
            v.dcX = x;
            v.gain += v.gainStep;
            dry += amp * v.gain * v.filter.process (v.dcY);
        }
        const double toned = s.tone.process (dry);
        const Stereo echo = s.delay.process (toned);
        const Stereo through { toned + echo.l, toned + echo.r };
        const Stereo wet = s.chorus.process (0.5 * (through.l + through.r));
        s.panL += s.panStepL;
        s.panR += s.panStepR;
        sum.l += s.panL * (through.l + wet.l);
        sum.r += s.panR * (through.r + wet.r);
    }
    // Fixed point at the end of the DSP chain: it clips hard at full scale.
    return output_.process ({ std::clamp (sum.l, -1.0, 1.0), std::clamp (sum.r, -1.0, 1.0) });
}

void Engine::render (float* left, float* right, int frames) noexcept
{
    for (int i = 0; i < frames; ++i)
    {
        const Stereo out = resampler_.process ([this] { return tick(); });
        left[i] = static_cast<float> (volume_ * out.l);
        right[i] = static_cast<float> (volume_ * out.r);
    }
}
} // namespace hypra
