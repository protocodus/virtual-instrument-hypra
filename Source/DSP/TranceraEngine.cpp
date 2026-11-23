#include "TranceraEngine.h"

namespace trancera
{
namespace
{
bool unison (const Patch& p) noexcept
{
    return p.unison && p.osc1.wave != Wave1::FeedbackOsc;
}

bool monophonic (const Patch& p) noexcept
{
    return p.mono || p.legato || p.unison || p.osc1.wave == Wave1::FeedbackOsc;
}

constexpr double kMasterOutputGain = 12.0;
} // namespace

void Engine::prepare (double hostRate, std::uint32_t seed)
{
    seed_ = seed;
    resampler_.prepare (hostRate);
    output_.prepare (OutputNode::Rack);
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
        v.pitchEnv.reset();
        v.filter.reset();
        v.ampBase = v.ampSmoothed = 0.0;
    }
    std::uint32_t partSeed = seed_ * 31u + 17u;
    for (PartState& s : parts_)
    {
        s.lfoRandom = Random (partSeed++);
        s.keyCount = 0;
        s.lfoPhase = s.lfoHeld = s.lfo1 = s.lfo2Phase = s.lfo2 = 0.0;
        s.lastPitch = 60.0;
        s.hasPlayed = false;
        s.panL = s.panTargetL = mapping::panCenterGain;
        s.panR = s.panTargetR = mapping::panCenterGain;
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
        v.pitchEnv.reset();
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
    s.chorus.configure (fx.chorusType, fx.chorus);
    if (! s.hasPlayed)
    {
        s.delay.snap();
        s.chorus.snap();
    }

    const Span range = span (part);
    for (int i = range.first; i < range.first + range.count; ++i)
    {
        Voice& v = voices_[static_cast<std::size_t> (i)];
        v.ampEnv.configure (patch.amp.attack, patch.amp.decay,
                            mapping::ampSustain (patch.amp.sustain), patch.amp.release);
        v.filterEnv.configure (patch.filter.env.attack, patch.filter.env.decay,
                               mapping::filterSustain (patch.filter.env.sustain), patch.filter.env.release);
        v.pitchEnv.configure (patch.pitchAttack, patch.pitchDecay, 0.0, patch.pitchDecay);
        if (becameMono && i - range.first >= (unison (patch) ? range.count : 1))
            release (v);
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
    const double ticks = s.glideSeconds * kControlRate;
    v.glideStep = ticks > 0.0 ? std::abs (v.target - v.pitch) / ticks : 1.0e9;
    if (! start)
        return;

    v.velocity = velocity / 127.0;
    v.age = 0.0;
    if (fresh)
    {
        v.filter.reset();
        v.ampBase = v.ampSmoothed = 0.0;
        v.ampEnv.reset();
        v.filterEnv.reset();
        v.pitchEnv.reset();
    }
    v.ampEnv.configure (p.amp.attack, p.amp.decay, mapping::ampSustain (p.amp.sustain), p.amp.release);
    v.ampEnv.trigger();
    v.filterEnv.configure (p.filter.env.attack, p.filter.env.decay,
                           mapping::filterSustain (p.filter.env.sustain), p.filter.env.release);
    v.filterEnv.trigger();
    v.pitchEnv.configure (p.pitchAttack, p.pitchDecay, 0.0, p.pitchDecay);
    v.pitchEnv.trigger();

    if (fresh || ! monophonic (p))
        v.oscillators.trigger (random_, p.osc1.wave, fresh);
    v.active = true;
    controlVoice (v, part);
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
    c.gain2 = mapping::balanceRatio (p.balance);
    c.gain1 = 1.0 - c.gain2;
    c.sync = p.osc2.sync;
    c.ring = p.ring;
    v.oscillators.configure (c);

    const double velocityDepth = bipolar (p.velocityFilter) * (v.velocity - 0.5) * 5.0;
    const double octaves = mapping::cutoffOctaves (p.filter.cutoff)
                           + mapping::keyFollow (p.filter.keyFollow)
                               * (v.pitch + 12.0 * std::clamp (p.oscShift, -2, 2) - 60.0) / 12.0
                           + mapping::filterEnvSemitones (p.filter.envDepth) / 12.0 * v.filterEnv.value()
                           + mapping::filterLfoSemitones (p.filter.lfoDepth) / 12.0 * lfo
                           + mod_ * mapping::filterLfoSemitones (p.lfo2.filter) / 12.0 * s.lfo2
                           + velocityDepth;
    v.filter.configure (mapping::cutoffCoefficient (octaves), unit (p.filter.resonance), p.filter.type, p.filter.steep,
                        p.filter.envDepth != 0 ? v.filterEnv.filterSmootherAlpha() : (1.0 / 512.0));

    double ampBase = 0.5 * mapping::balanceAmpComp (p.balance) * mapping::effectLevel (p.level)
                     * (1.0 - unit (p.velocityAmp) * (1.0 - v.velocity));
    if (p.pan == PanMode::Off)
        ampBase *= std::max (0.0, 1.0 + mapping::ampLfoDepth (p.ampLfo) * lfo);
    ampBase *= std::max (0.0, 1.0 + mod_ * mapping::ampLfoDepth (p.lfo2.amp) * s.lfo2);
    if (unison (p))
        ampBase *= 1.25 / std::sqrt (static_cast<double> (span (part).count));
    v.ampBase = ampBase;
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
            case LfoWave::Saw: s.lfo1 = 1.0 - 2.0 * p; break;
            case LfoWave::Square: s.lfo1 = p < 0.5 ? 1.0 : -1.0; break;
            case LfoWave::SampleHold: s.lfo1 = s.lfoHeld; break;
        }
        s.lfo2Phase += s.lfo2Step;
        if (s.lfo2Phase >= 1.0)
            s.lfo2Phase -= 1.0;
        s.lfo2 = osc::triangle (s.lfo2Phase < 0.75 ? s.lfo2Phase + 0.25 : s.lfo2Phase - 0.75);

        if (s.patch.pan == PanMode::Off)
        {
            s.panTargetL = s.panTargetR = mapping::panCenterGain;
        }
        else if (s.patch.pan == PanMode::Manual)
        {
            const int idx = std::clamp (s.patch.ampLfo + 64, 0, 127);
            s.panTargetL = mapping::panLeft (idx);
            s.panTargetR = mapping::panRight (idx);
        }
        else
        {
            const Stereo gains = mapping::panGains (mapping::ampLfoDepth (s.patch.ampLfo) * s.lfo1);
            s.panTargetL = gains.l;
            s.panTargetR = gains.r;
        }

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
        double voiceSum = 0.0;
        const Span range = span (part);
        for (int i = range.first; i < range.first + range.count; ++i)
        {
            Voice& v = voices_[static_cast<std::size_t> (i)];
            if (! v.active)
                continue;
            v.ampEnv.process();
            v.filterEnv.process();
            v.pitchEnv.process();
            const double ampTarget = v.ampBase * v.ampEnv.value();
            v.ampSmoothed += v.ampEnv.smootherAlpha() * (ampTarget - v.ampSmoothed);
            if (v.ampEnv.idle() && v.ampSmoothed < 1.0e-6)
            {
                v.active = v.held = v.sustained = false;
                continue;
            }
            const double x = v.oscillators.process (random_);
            voiceSum += v.ampSmoothed * v.filter.process (x);
        }
        const double dry = sat (2.0 * voiceSum);
        const double toned = s.tone.processSaturating (dry);
        const Stereo afterDelay = s.delay.processMix (toned);
        const Stereo afterFx = s.chorus.processMix (afterDelay);
        constexpr double panAlpha = 1.0 / 512.0;
        s.panL += panAlpha * (s.panTargetL - s.panL);
        s.panR += panAlpha * (s.panTargetR - s.panR);
        sum.l += 0.25 * sat (2.0 * s.panL * afterFx.l);
        sum.r += 0.25 * sat (2.0 * s.panR * afterFx.r);
    }
    return output_.process ({ sat (kMasterOutputGain * sum.l), sat (kMasterOutputGain * sum.r) });
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
} // namespace trancera
