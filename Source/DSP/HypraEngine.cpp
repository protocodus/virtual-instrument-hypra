#include "HypraEngine.h"

namespace hypra
{
namespace
{
bool monophonic (const Patch& p) noexcept
{
    // OM p.56: FEEDBACK OSC sounds one note at a time.
    return p.mono || p.legato || p.unison || p.osc1.wave == Wave1::FeedbackOsc;
}

// Per-voice and part level, leaving room for eight voices below DAC clipping.
constexpr double voiceHeadroom = 0.22;
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
                            && (! monophonic (s.patch) || patch.unison != s.patch.unison);
    s.patch = patch;
    s.lfoStep = mapping::lfoHz (patch.lfo1.rate) / kControlRate;
    s.lfo2Step = mapping::lfoHz (patch.lfo2.rate) / kControlRate;
    s.fade = mapping::fadeSeconds (patch.lfo1.fade);
    s.glide = patch.portamento
                  ? 1.0 - std::exp (-4.0 / (mapping::portamentoSeconds (patch.portamentoTime) * kControlRate))
                  : 1.0;
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
        const auto set = [] (Envelope& env, const EnvParams& e)
        {
            env.configure (mapping::attackSeconds (e.attack), mapping::decaySeconds (e.decay),
                           unit (e.sustain), mapping::decaySeconds (e.release));
        };
        set (v.ampEnv, patch.amp);
        set (v.filterEnv, patch.filter.env);
        if (becameMono && i - range.first >= (patch.unison ? std::min (4, range.count) : 1))
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
    const auto set = [] (Envelope& env, const EnvParams& e)
    {
        env.configure (mapping::attackSeconds (e.attack), mapping::decaySeconds (e.decay),
                       unit (e.sustain), mapping::decaySeconds (e.release));
        env.trigger();
    };
    set (v.ampEnv, p.amp);
    set (v.filterEnv, p.filter.env);
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
    const int count = s.patch.unison ? std::min (4, range.count) : 1;
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
    v.pitch += s.glide * (v.target - v.pitch);

    const double fade = s.fade > 0.0 ? std::min (1.0, v.age / s.fade) : 1.0;
    const double lfo = s.lfo1 * fade;

    double pitchEnv = 0.0;
    if (p.pitchEnvDepth != 0)
    {
        const double attack = mapping::attackSeconds (p.pitchAttack);
        const double decay = mapping::decaySeconds (p.pitchDecay);
        const double late = (v.age - attack) / decay;
        if (late < 0.0)
            pitchEnv = v.age / attack;
        else if (late < 1.0) // OM p.59: back to exactly the played pitch
            pitchEnv = (std::exp (-5.0 * late) - 0.006737947) / (1.0 - 0.006737947);
    }

    const double envDepth = bipolar (p.pitchEnvDepth);
    const double semis = mapping::pitchLfoSemitones (p.pitchLfo) * lfo
                         + mapping::pitchEnvSemitones * envDepth * std::abs (envDepth) * pitchEnv;
    const bool toXmod = p.destination == ModDestination::XMod;

    const double common = v.pitch + v.detune + 12.0 * std::clamp (p.oscShift, -2, 2)
                          + bend_ * (bend_ >= 0.0 ? p.bendUp : p.bendDown)
                          + mod_ * 2.0 * unit (p.lfo2.pitch) * s.lfo2
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
        c.xmod = std::clamp (c.xmod + bipolar (p.pitchLfo) * lfo + envDepth * pitchEnv, 0.0, 1.0);
    c.gain1 = mapping::balanceGain (p.balance, true);
    c.gain2 = mapping::balanceGain (p.balance, false);
    c.sync = p.osc2.sync;
    c.ring = p.ring;
    v.oscillators.configure (c);

    const double velocityDepth = bipolar (p.velocityFilter) * (v.velocity - 0.5) * 5.0;
    const double octaves = mapping::cutoffOctaves (p.filter.cutoff)
                           + mapping::keyFollow (p.filter.keyFollow) * (v.pitch - 60.0) / 12.0
                           + mapping::filterEnvOctaves * bipolar (p.filter.envDepth) * v.filterEnv.value()
                           + mapping::filterLfoOctaves * bipolar (p.filter.lfoDepth) * lfo
                           + mod_ * 4.0 * unit (p.lfo2.filter) * s.lfo2
                           + velocityDepth;
    v.filter.configure (mapping::cutoffHz (octaves), unit (p.filter.resonance), p.filter.type, p.filter.steep);

    double gain = voiceHeadroom * unit (p.level) * unit (p.level)
                  * (1.0 - unit (p.velocityAmp) * (1.0 - v.velocity));
    if (p.pan == PanMode::Off)
        gain *= 1.0 + 0.9 * bipolar (p.ampLfo) * lfo;
    gain *= 1.0 + 0.9 * mod_ * unit (p.lfo2.amp) * s.lfo2;
    if (p.unison)
        gain *= 0.6; // four stacked voices
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

        double pan = 0.0;
        if (s.patch.pan == PanMode::Manual)
            pan = bipolar (s.patch.ampLfo);
        else if (s.patch.pan == PanMode::Auto)
            pan = bipolar (s.patch.ampLfo) * s.lfo1; // OM p.62: FADE does not apply
        const double l = std::min (1.0, 1.0 - pan), r = std::min (1.0, 1.0 + pan);
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
