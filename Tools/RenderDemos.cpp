// Renders the proof-of-concept listening material: original phrases played
// by the engine, 48 kHz / 24 bit, with no external processing.
//
//   HypraRenderDemos <output-directory>
//
// 00 sums several engine instances at the fader gains written beside them.
// Every other demo is one instance. The A/B pair is matched on whole-file
// RMS by turning the louder file down; nothing else is trimmed.
#include "HypraEngine.h"

#include <cstdio>
#include <filesystem>
#include <functional>
#include <initializer_list>
#include <memory>
#include <string>
#include <vector>

using namespace hypra;

namespace
{
constexpr int rate = 48000;

struct Audio
{
    std::vector<float> l, r;

    void add (const Audio& other, double gain = 1.0)
    {
        l.resize (std::max (l.size(), other.l.size()));
        r.resize (l.size());
        for (std::size_t i = 0; i < other.l.size(); ++i)
        {
            l[i] += static_cast<float> (gain * other.l[i]);
            r[i] += static_cast<float> (gain * other.r[i]);
        }
    }

    void append (const Audio& other)
    {
        l.insert (l.end(), other.l.begin(), other.l.end());
        r.insert (r.end(), other.r.begin(), other.r.end());
    }

    double rms() const
    {
        double sum = 0.0;
        for (std::size_t i = 0; i < l.size(); ++i)
            sum += 0.5 * (l[i] * l[i] + r[i] * r[i]);
        return std::sqrt (sum / static_cast<double> (std::max<std::size_t> (1, l.size())));
    }

    double peak() const
    {
        double top = 0.0;
        for (std::size_t i = 0; i < l.size(); ++i)
            top = std::max ({ top, static_cast<double> (std::abs (l[i])), static_cast<double> (std::abs (r[i])) });
        return top;
    }
};

class Performance
{
public:
    explicit Performance (Patch patch, double bpm = 60.0) : upper (patch), beat_ (60.0 / bpm) {}

    Patch upper, lower;
    KeyMode mode { KeyMode::Single };
    OutputNode node { OutputNode::Rack };
    int detune { 0 };

    void at (double beats, std::function<void (Engine&, Patch&)> action)
    {
        events_.push_back ({ beats * beat_, std::move (action) });
    }

    void note (double start, double length, int key, int velocity = 110)
    {
        at (start, [=] (Engine& e, Patch&) { e.noteOn (key, velocity); });
        at (start + length, [=] (Engine& e, Patch&) { e.noteOff (key); });
    }

    void chord (double start, double length, std::initializer_list<int> keys, int velocity = 105)
    {
        for (const int key : keys)
            note (start, length, key, velocity);
    }

    void sweep (double start, double length, double from, double to, std::function<void (Patch&, int)> set)
    {
        const int steps = std::max (2, static_cast<int> (length * beat_ * 120.0));
        for (int i = 0; i <= steps; ++i)
        {
            const int value = static_cast<int> (std::lround (from + (to - from) * i / steps));
            at (start + length * i / steps, [=] (Engine& e, Patch& p)
            {
                set (p, value);
                e.setPatch (Part::Upper, p);
            });
        }
    }

    void change (double start, std::function<void (Patch&)> set)
    {
        at (start, [=] (Engine& e, Patch& p)
        {
            set (p);
            e.setPatch (Part::Upper, p);
        });
    }

    Audio render (double beats)
    {
        std::stable_sort (events_.begin(), events_.end(),
                          [] (const Event& a, const Event& b) { return a.seconds < b.seconds; });
        auto engine = std::make_unique<Engine>();
        engine->prepare (rate);
        engine->setOutputNode (node);
        engine->setMode (mode);
        engine->setPartDetune (detune);
        engine->setPatch (Part::Upper, upper);
        engine->setPatch (Part::Lower, lower);
        engine->reset();

        Patch live = upper;
        const int frames = static_cast<int> (beats * beat_ * rate);
        Audio out;
        out.l.resize (static_cast<std::size_t> (frames));
        out.r.resize (out.l.size());
        int position = 0;
        const auto renderTo = [&] (int end)
        {
            while (position < end)
            {
                const int count = std::min (256, end - position);
                engine->render (out.l.data() + position, out.r.data() + position, count);
                position += count;
            }
        };
        for (const Event& event : events_)
        {
            const int when = static_cast<int> (std::lround (event.seconds * rate));
            if (when >= frames)
                break;
            renderTo (when);
            event.action (*engine, live);
        }
        renderTo (frames);
        // A 250 ms fade so that echoes still sounding at the end do not click.
        const int fade = std::min (frames, rate / 4);
        for (int i = 0; i < fade; ++i)
        {
            const auto index = static_cast<std::size_t> (frames - 1 - i);
            const float gain = static_cast<float> (i) / static_cast<float> (fade);
            out.l[index] *= gain;
            out.r[index] *= gain;
        }
        return out;
    }

private:
    struct Event
    {
        double seconds;
        std::function<void (Engine&, Patch&)> action;
    };

    double beat_;
    std::vector<Event> events_;
};

Patch superSaw (int detune, int mix)
{
    Patch p;
    p.osc1 = { Wave1::SuperSaw, detune, mix };
    p.balance = -64;
    return p;
}

Patch single (Wave1 wave, int ctrl1 = 0, int ctrl2 = 0)
{
    Patch p;
    p.osc1 = { wave, ctrl1, ctrl2 };
    p.balance = -64;
    return p;
}

Patch leadPatch()
{
    Patch p = superSaw (72, 96);
    p.filter.cutoff = 118;
    p.filter.resonance = 14;
    p.filter.keyFollow = 10;
    p.amp = { 2, 29, 108, 13 };
    p.level = 105;
    p.effects.chorusType = ChorusType::SuperSlow;
    p.effects.chorus = 38;
    p.effects.delayType = DelayType::PanLR;
    p.effects.delayTime = 92;
    p.effects.delay = 50;
    p.effects.feedback = 52;
    return p;
}

Patch padPatch()
{
    Patch p = superSaw (58, 110);
    p.filter.cutoff = 86;
    p.filter.resonance = 18;
    p.filter.keyFollow = 12;
    p.filter.lfoDepth = 15;
    p.lfo1.rate = 19;
    p.amp = { 42, 40, 115, 38 };
    p.level = 96;
    p.effects.chorusType = ChorusType::SuperMid;
    p.effects.chorus = 72;
    return p;
}

Patch bassPatch()
{
    Patch p = single (Wave1::Saw, 0);
    p.balance = 0;
    p.osc2.wave = Wave2::Square;
    p.osc2.range = -12;
    p.filter.cutoff = 42;
    p.filter.resonance = 72;
    p.filter.envDepth = 37;
    p.filter.env = { 0, 10, 12, 9 };
    p.amp = { 1, 20, 100, 7 };
    p.mono = true;
    return p;
}

Patch pluckPatch()
{
    Patch p = superSaw (44, 80);
    p.filter.cutoff = 52;
    p.filter.resonance = 30;
    p.filter.envDepth = 42;
    p.filter.env = { 0, 13, 0, 10 };
    p.amp = { 1, 25, 20, 10 };
    p.level = 127;
    p.effects.delayType = DelayType::PanLR;
    p.effects.delayTime = 88;
    p.effects.delay = 62;
    p.effects.feedback = 60;
    return p;
}

struct Step { double at, length; int key; };

// An original eight-bar theme over A minor, F, C, G.
const std::vector<Step> theme {
    { 0, .75, 76 }, { .75, .75, 72 }, { 1.5, .5, 74 }, { 2, 1.25, 76 }, { 3.5, .5, 81 },
    { 4, .75, 79 }, { 4.75, .75, 77 }, { 5.5, .5, 76 }, { 6, 1.75, 72 },
    { 8, .75, 79 }, { 8.75, .75, 76 }, { 9.5, .5, 72 }, { 10, 1.25, 74 }, { 11.5, .5, 76 },
    { 12, 1, 74 }, { 13, .5, 71 }, { 13.5, .5, 74 }, { 14, 1.9, 79 },
    { 16, .75, 84 }, { 16.75, .75, 83 }, { 17.5, .5, 81 }, { 18, 1.25, 76 }, { 19.5, .5, 79 },
    { 20, 1.5, 81 }, { 21.5, .5, 77 }, { 22, 1.75, 72 },
    { 24, .75, 72 }, { 24.75, .75, 74 }, { 25.5, .5, 76 }, { 26, 1.5, 79 }, { 27.5, .5, 76 },
    { 28, .75, 74 }, { 28.75, .75, 76 }, { 29.5, .5, 74 }, { 30, 1.9, 71 },
};

const int roots[] { 45, 41, 48, 43 };
const int thirds[] { 3, 4, 4, 4 };

void addPad (Performance& p, double start, int bars)
{
    for (int bar = 0; bar < bars; ++bar)
    {
        const int root = roots[bar % 4] + 12;
        p.chord (start + 4 * bar, 3.9, { root, root + thirds[bar % 4], root + 7, root + 12 }, 95);
    }
}

void addBass (Performance& p, double start, int bars)
{
    for (int bar = 0; bar < bars; ++bar)
        for (int i = 0; i < 4; ++i)
            p.note (start + 4 * bar + i + 0.5, 0.38, roots[bar % 4] - 12 + (i == 3 ? 12 : 0));
}

void addArp (Performance& p, double start, int bars)
{
    for (int bar = 0; bar < bars; ++bar)
    {
        const int root = roots[bar % 4] + 24;
        const int notes[] { root, root + 7, root + 12, root + thirds[bar % 4] };
        for (int i = 0; i < 16; ++i)
            p.note (start + 4 * bar + 0.25 * i, 0.18, notes[i % 4], i % 4 ? 90 : 118);
    }
}

Audio song()
{
    constexpr double bpm = 138.0, beats = 68.0;

    Performance pad (padPatch(), bpm);
    addPad (pad, 0, 16);
    pad.sweep (0, 30, 55, 100, [] (Patch& p, int v) { p.filter.cutoff = v; });

    Performance bass (bassPatch(), bpm);
    addBass (bass, 16, 12);

    Performance arp (pluckPatch(), bpm);
    addArp (arp, 0, 16);

    Performance lead (leadPatch(), bpm);
    for (const Step& s : theme)
        lead.note (32 + s.at, s.length * 0.92, s.key);

    // The drums are patches too: a swept triangle and filtered noise.
    Patch kickPatch = single (Wave1::Triangle);
    kickPatch.pitchEnvDepth = 42;
    kickPatch.pitchDecay = 9;
    kickPatch.amp = { 0, 13, 0, 10 };
    kickPatch.level = 127;
    Performance kick (kickPatch, bpm);
    for (int i = 16; i < 64; ++i)
        kick.note (i, 0.45, 28, 127);

    Patch hatPatch = single (Wave1::Noise, 127, 10);
    hatPatch.filter.type = FilterType::HighPass;
    hatPatch.filter.cutoff = 108;
    hatPatch.amp = { 0, 8, 0, 7 };
    hatPatch.level = 127;
    Performance hat (hatPatch, bpm);
    for (int i = 32; i < 64; ++i)
        hat.note (i + 0.5, 0.1, 84, 127);

    Audio out;
    out.add (pad.render (beats), 0.58);
    out.add (bass.render (beats), 1.05);
    out.add (arp.render (beats), 0.70);
    out.add (lead.render (beats), 1.02);
    out.add (kick.render (beats), 1.55);
    out.add (hat.render (beats), 0.70);
    return out;
}

Audio supersawLead()
{
    Performance p (leadPatch(), 138);
    for (const Step& s : theme)
        p.note (s.at, s.length * 0.92, s.key);
    return p.render (36);
}

Audio pad()
{
    Performance p (padPatch(), 100);
    addPad (p, 0, 5);
    p.sweep (0, 18, 45, 112, [] (Patch& x, int v) { x.filter.cutoff = v; });
    return p.render (23);
}

Audio detuneAndMix()
{
    Patch patch = superSaw (0, 100);
    patch.amp = { 2, 20, 127, 9 };
    Performance p (patch);
    p.note (0, 7, 57);
    p.sweep (0.5, 6, 0, 127, [] (Patch& x, int v) { x.osc1.ctrl1 = v; });
    p.change (7.3, [] (Patch& x) { x.osc1 = { Wave1::SuperSaw, 70, 0 }; });
    p.chord (7.5, 6.5, { 57, 64, 69 });
    p.sweep (8, 5.5, 0, 127, [] (Patch& x, int v) { x.osc1.ctrl2 = v; });
    return p.render (14.6);
}

Audio waveforms()
{
    Audio out;
    for (int i = 0; i < 7; ++i)
    {
        const auto wave = static_cast<Wave1> (i);
        Patch patch = single (wave);
        patch.amp = { 2, 20, 127, 9 };
        if (wave == Wave1::SuperSaw)
            patch.osc1.ctrl2 = 105;
        if (wave == Wave1::Noise)
            patch.osc1.ctrl2 = 92;
        if (wave == Wave1::FeedbackOsc)
            patch.osc1.ctrl2 = 110;
        Performance p (patch);
        p.note (0, 2.4, wave == Wave1::FeedbackOsc ? 57 : 48);
        p.sweep (0, 2.4, wave == Wave1::Noise ? 30 : 0, 127, [] (Patch& x, int v) { x.osc1.ctrl1 = v; });
        out.append (p.render (2.8));
    }
    return out;
}

Audio syncLead()
{
    Patch patch = single (Wave1::Saw, 0);
    patch.balance = 50;
    patch.osc2.sync = true;
    patch.osc2.range = 3;
    patch.destination = ModDestination::Osc2;
    patch.pitchEnvDepth = 42;
    patch.pitchAttack = 9;
    patch.pitchDecay = 70;
    patch.level = 120;
    patch.mono = true;
    patch.portamento = true;
    patch.portamentoTime = 7;
    patch.filter.cutoff = 122;
    patch.amp = { 2, 20, 115, 9 };
    patch.effects.delay = 44;
    patch.effects.delayTime = 86;
    Performance p (patch, 124);
    const Step riff[] { { 0, 1.4, 57 }, { 1.5, .5, 60 }, { 2, 1, 64 }, { 3, .9, 62 },
                        { 4, 1.4, 57 }, { 5.5, .5, 67 }, { 6, 1.8, 64 }, { 8, 1, 69 },
                        { 9, 1, 67 }, { 10, 1, 64 }, { 11, .9, 60 }, { 12, 3.5, 57 } };
    for (const Step& s : riff)
        p.note (s.at, s.length, s.key);
    p.at (12.5, [] (Engine& e, Patch&) { e.modWheel (0.7); });
    return p.render (18);
}

Audio crossModAndRing()
{
    Patch patch = single (Wave1::Triangle);
    patch.osc2.wave = Wave2::Triangle;
    patch.osc2.range = 19;
    patch.amp = { 0, 46, 0, 38 };
    patch.level = 127;
    patch.velocityAmp = 0;
    patch.effects.delay = 36;
    Performance p (patch);
    for (int i = 0; i < 6; ++i)
        p.note (i * 1.1, 0.9, 60 + (i * 7) % 12);
    p.sweep (0, 6.5, 0, 90, [] (Patch& x, int v) { x.xmod = v; });
    p.change (6.9, [] (Patch& x)
    {
        x.xmod = 0;
        x.ring = true;
        x.balance = 63;
        x.osc2.range = 18;
        x.osc2.fine = 23;
    });
    const int bells[] { 72, 79, 76, 84, 67, 74 };
    for (int i = 0; i < 6; ++i)
        p.note (7 + i * 0.9, 0.8, bells[i]);
    return p.render (14.5);
}

Audio feedbackSolo()
{
    Patch patch = single (Wave1::FeedbackOsc, 40, 112);
    patch.legato = true;
    patch.portamento = true;
    patch.portamentoTime = 10;
    patch.filter.cutoff = 119;
    patch.amp = { 2, 20, 120, 11 };
    patch.effects.delay = 46;
    patch.effects.delayTime = 100;
    patch.effects.chorusType = ChorusType::SuperClear;
    patch.effects.chorus = 25;
    Performance p (patch);
    p.note (0, 2.2, 52);
    p.note (2.1, 1.3, 55);
    p.note (3.3, 2.9, 59);
    p.note (6.7, 1.2, 62);
    p.note (7.8, 5.0, 64);
    p.sweep (3.3, 2.8, 40, 100, [] (Patch& x, int v) { x.osc1.ctrl1 = v; });
    p.sweep (7.8, 4, 100, 20, [] (Patch& x, int v) { x.osc1.ctrl1 = v; });
    p.at (9.2, [] (Engine& e, Patch&) { e.modWheel (0.8); });
    return p.render (14);
}

Audio filters()
{
    Audio out;
    for (const auto type : { FilterType::LowPass, FilterType::BandPass, FilterType::HighPass })
        for (const bool steep : { false, true })
        {
            Patch patch = superSaw (48, 90);
            patch.filter.type = type;
            patch.filter.steep = steep;
            patch.filter.resonance = 84;
            Performance p (patch);
            p.chord (0, 2.1, { 45, 52, 57 });
            p.sweep (0, 1.05, 22, 116, [] (Patch& x, int v) { x.filter.cutoff = v; });
            p.sweep (1.06, 1.0, 116, 22, [] (Patch& x, int v) { x.filter.cutoff = v; });
            out.append (p.render (2.5));
        }
    Patch whistle = single (Wave1::Saw, 0);
    whistle.filter.resonance = 127;
    Performance p (whistle);
    p.note (0, 3, 36);
    p.sweep (0, 2.9, 40, 92, [] (Patch& x, int v) { x.filter.cutoff = v; });
    out.append (p.render (3.4));
    return out;
}

Audio bass()
{
    Patch patch = bassPatch();
    patch.portamento = true;
    patch.portamentoTime = 4;
    patch.level = 120;
    Performance p (patch, 136);
    const int line[] { 33, 33, 45, 33, 36, 33, 43, 40, 33, 45, 33, 31, 33, 33, 48, 45 };
    for (int i = 0; i < 64; ++i)
        p.note (i * 0.5, i % 4 == 2 ? 0.46 : 0.3, line[i % 16], i % 3 ? 90 : 120);
    p.sweep (0, 28, 28, 70, [] (Patch& x, int v) { x.filter.cutoff = v; });
    return p.render (34);
}

Audio arp()
{
    Performance p (pluckPatch(), 138);
    addArp (p, 0, 8);
    p.sweep (0, 30, 40, 78, [] (Patch& x, int v) { x.filter.cutoff = v; });
    return p.render (36);
}

Audio dualAndUnison()
{
    Patch upper = leadPatch();
    upper.level = 96;
    Patch under = single (Wave1::Square, 20);
    under.oscShift = -1;
    under.level = 88;
    under.amp = upper.amp;
    under.effects.chorusType = ChorusType::SuperFast;
    under.effects.chorus = 50;

    Performance dual (upper, 138);
    dual.lower = under;
    dual.mode = KeyMode::Dual;
    dual.detune = 8;
    for (std::size_t i = 0; i < 9; ++i)
        dual.note (theme[i].at, theme[i].length, theme[i].key - 12);
    Audio out = dual.render (10);

    upper.unison = true;
    Performance unison (upper, 138);
    for (std::size_t i = 18; i < 26; ++i)
        unison.note (theme[i].at - 16, theme[i].length, theme[i].key);
    out.append (unison.render (10));
    return out;
}

Audio chorusTypes()
{
    Audio out;
    for (int i = 0; i < 12; ++i)
    {
        Patch patch = single (Wave1::Saw, 0);
        patch.filter.cutoff = 100;
        patch.amp = { 2, 20, 120, 9 };
        patch.effects.chorusType = static_cast<ChorusType> (i);
        patch.effects.chorus = 105;
        Performance p (patch);
        p.chord (0, 1.45, { 57, 60, 64 });
        out.append (p.render (1.8));
    }
    return out;
}

Audio outputNodePhrase (OutputNode node)
{
    Patch patch = leadPatch();
    patch.effects.delay = 0;
    Performance p (patch, 138);
    p.node = node;
    p.chord (0, 3.7, { 57, 60, 64, 69 });
    for (std::size_t i = 0; i < 9; ++i)
        p.note (4 + theme[i].at, theme[i].length * 0.92, theme[i].key);
    return p.render (13);
}

bool writeWav (const std::string& path, const Audio& audio, double gain = 1.0)
{
    std::FILE* file = std::fopen (path.c_str(), "wb");
    if (file == nullptr)
        return false;
    const auto put = [file] (std::uint32_t value, int bytes)
    {
        for (int i = 0; i < bytes; ++i)
            std::fputc (static_cast<int> ((value >> (8 * i)) & 255u), file);
    };
    const auto bytes = static_cast<std::uint32_t> (audio.l.size() * 6);
    std::fputs ("RIFF", file);
    put (36 + bytes, 4);
    std::fputs ("WAVEfmt ", file);
    put (16, 4);
    put (1, 2);
    put (2, 2);
    put (rate, 4);
    put (rate * 6, 4);
    put (6, 2);
    put (24, 2);
    std::fputs ("data", file);
    put (bytes, 4);
    const auto sample = [&] (float x)
    {
        const double v = std::clamp (std::round (gain * x * 8388608.0), -8388608.0, 8388607.0);
        put (static_cast<std::uint32_t> (static_cast<std::int32_t> (v)), 3);
    };
    for (std::size_t i = 0; i < audio.l.size(); ++i)
    {
        sample (audio.l[i]);
        sample (audio.r[i]);
    }
    const double peak = gain * audio.peak();
    std::printf ("%-58s %5.1f s  peak %6.2f dBFS  rms %6.2f dBFS%s\n",
                 std::filesystem::path (path).filename().string().c_str(),
                 static_cast<double> (audio.l.size()) / rate, 20.0 * std::log10 (peak + 1e-12),
                 20.0 * std::log10 (gain * audio.rms() + 1e-12), peak >= 1.0 ? "  CLIPS" : "");
    return std::fclose (file) == 0 && peak < 1.0;
}
} // namespace

int main (int argc, char** argv)
{
    if (argc != 2)
    {
        std::fprintf (stderr, "usage: HypraRenderDemos <output-directory>\n");
        return 2;
    }
    const std::string dir = argv[1];
    std::filesystem::create_directories (dir + "/ab-output-stage");

    const std::pair<const char*, Audio (*)()> demos[] {
        { "00-full-track-all-hypra", song },
        { "01-supersaw-lead", supersawLead },
        { "02-supersaw-pad", pad },
        { "03-supersaw-detune-then-mix", detuneAndMix },
        { "04-seven-waveforms", waveforms },
        { "05-sync-lead", syncLead },
        { "06-crossmod-then-ring", crossModAndRing },
        { "07-feedback-oscillator", feedbackSolo },
        { "08-filter-types-and-oscillation", filters },
        { "09-mono-bass", bass },
        { "10-pluck-arp-delay", arp },
        { "11-dual-then-unison", dualAndUnison },
        { "12-twelve-chorus-types", chorusTypes },
    };
    bool ok = true;
    for (const auto& [name, make] : demos)
        ok = writeWav (dir + "/" + name + ".wav", make()) && ok;

    const Audio a = outputNodePhrase (OutputNode::KeyboardAsPrinted);
    const Audio b = outputNodePhrase (OutputNode::Rack);
    const double target = std::min (a.rms(), b.rms());
    ok = writeWav (dir + "/ab-output-stage/A.wav", a, target / a.rms()) && ok;
    ok = writeWav (dir + "/ab-output-stage/B.wav", b, target / b.rms()) && ok;
    std::printf ("A/B trims: A %+.2f dB, B %+.2f dB\n",
                 20.0 * std::log10 (target / a.rms()), 20.0 * std::log10 (target / b.rms()));
    return ok ? 0 : 1;
}
