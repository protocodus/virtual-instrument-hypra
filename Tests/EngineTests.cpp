// Behavioural checks for the proof of concept. Each expectation cites what
// pins it: the JP-8000 owner's manual (OM), the service notes (SN), or
// Szabo 2010 (a black-box measurement of the hardware).
#include "../Source/DSP/HypraControls.h"
#include "../Source/DSP/HypraEngine.h"

#include <algorithm>
#include <complex>
#include <cstdio>
#include <cstdlib>
#include <functional>
#include <new>
#include <string>
#include <vector>

namespace
{
long allocations = 0;
bool countAllocations = false;
int failures = 0, checks = 0;

void check (bool ok, const std::string& what)
{
    ++checks;
    if (! ok)
    {
        ++failures;
        std::printf ("FAIL  %s\n", what.c_str());
    }
}

void near (double value, double expected, double tolerance, const std::string& what)
{
    check (std::abs (value - expected) <= tolerance,
           what + ": " + std::to_string (value) + " expected " + std::to_string (expected));
}

using namespace hypra;
constexpr double fs = kEngineRate;

// Amplitude of one frequency through a Blackman-Harris window.
double tone (const std::vector<double>& x, double hz, double rate = fs)
{
    std::complex<double> sum {}, z { 1.0, 0.0 };
    const auto step = std::polar (1.0, -kTwoPi * hz / rate);
    double weight = 0.0;
    for (std::size_t i = 0; i < x.size(); ++i)
    {
        const double p = kTwoPi * static_cast<double> (i) / static_cast<double> (x.size());
        const double w = 0.35875 - 0.48829 * std::cos (p) + 0.14128 * std::cos (2 * p)
                         - 0.01168 * std::cos (3 * p);
        sum += x[i] * w * z;
        weight += w;
        z *= step;
        if ((i & 1023) == 1023)
            z /= std::abs (z);
    }
    return 2.0 * std::abs (sum) / weight;
}

double db (double gain) { return 20.0 * std::log10 (std::max (gain, 1e-15)); }

double peakHz (const std::vector<double>& x, double lo, double hi, double step, double rate = fs)
{
    double best = lo, value = -1.0;
    for (double hz = lo; hz <= hi; hz += step)
        if (const double a = tone (x, hz, rate); a > value)
        {
            value = a;
            best = hz;
        }
    return best;
}

void superSaw()
{
    // Szabo 2010: offsets at full detune, as ratios of the centre saw.
    constexpr double expected[] { -0.11002313, -0.06288439, -0.01952356, 0.0,
                                  0.01991221, 0.06216538, 0.10745242 };
    constexpr double f0 = 1000.0;
    SuperSaw osc;
    Random random (7);
    osc.trigger (random);
    osc.configure (f0, 1.0, 1.0);
    std::vector<double> x (88200);
    for (auto& v : x)
        v = osc.process();
    for (int i = 0; i < 7; ++i)
    {
        const double hz = f0 * (1.0 + expected[i]);
        near (peakHz (x, hz - 4, hz + 4, 0.5), hz, 1.0, "supersaw offset " + std::to_string (i));
    }
    // Szabo's measured tables (thesis Tables 2 and 3), not his fitted curves.
    near (mapping::superCentre (0.0), 1.0, 1e-9, "centre gain at mix 0");
    near (mapping::superSide (0.0), 0.03836, 1e-9, "side gain at mix 0");
    near (mapping::superCentre (95.0 / 127.0), 0.585, 1e-9, "centre gain at mix 95");
    near (mapping::superSide (1.0), 0.59, 1e-9, "side gain at mix 127");
    near (mapping::superDetune (63.0 / 127.0), 0.0967273, 2e-4, "detune at 63 (Szabo; JE-8086 within 0.1 %)");
    near (mapping::superDetune (124.0 / 127.0), 0.52376, 1e-5, "detune climbs steeply from 120 (JE-8086)");
    near (mapping::superDetune (1.0), 1.0, 1e-9, "detune at 127");
    bool rising = true;
    for (int v = 1; v < 128; ++v)
        rising = rising && mapping::superDetune (v / 127.0) >= mapping::superDetune ((v - 1) / 127.0);
    check (rising, "detune never turns back");

    // Pitch-tracked high-pass: what aliases below f0 is well under the fundamental.
    SuperSaw high;
    high.trigger (random);
    high.configure (6000.0, 0.0, 0.0);
    for (auto& v : x)
        v = high.process();
    check (db (tone (x, 1800.0) / tone (x, 6000.0)) < -35.0, "alias below f0 is filtered");
}

void hardwareRecording()
{
    // FadedShadows' JP-8080 recording (Freesound 275434, CC0): SUPER SAW at
    // DETUNE 0 and MIX 0, 98.874 Hz. Docs/research/hardware-ab-2026-11-22.md.
    constexpr double f0 = 98.8741659278;
    SuperSaw drift;
    drift.configure (f0, 0.0, 0.0);
    const int measured[] { -6, -4, -1, 0, 1, 3, 5 };
    for (int i = 0; i < 7; ++i)
        check (drift.stepsFromCentre (i) == measured[i], "measured drift step of saw " + std::to_string (i));

    // Two identical 12 dB high-passes at 0.904 f0, Q 0.878: the fundamental
    // sits 0.8 dB under the saw law. Averaged over starting phases.
    double first = 0.0, tenth = 0.0;
    for (std::uint32_t seed = 1; seed <= 16; ++seed)
    {
        Random random (seed * 7919u);
        SuperSaw osc;
        osc.trigger (random);
        osc.configure (441.0, 0.0, 0.0);
        std::vector<double> x (44100);
        for (int i = 0; i < 2000; ++i)
            osc.process();
        for (auto& v : x)
            v = osc.process();
        const double a = tone (x, 441.0), b = 10.0 * tone (x, 4410.0);
        first += a * a;
        tenth += b * b;
    }
    near (10.0 * std::log10 (first / tenth), -0.83, 0.35, "measured high-pass at the fundamental");
}

// Laws measured on JE-8086 2.2.27 running JP-8000 system 1.05, played as a
// black box (Docs/research/black-box-je8086-2026-11-22.md).
void measuredLaws()
{
    near (mapping::pulseDuty (64.0 / 127.0), 0.75, 1e-9, "pulse duty is 0.5 + v/256");
    near (mapping::pulseDuty (8.0 / 127.0), 0.53125, 1e-9, "pulse duty at 8");
    near (mapping::sawShapeK (0.0), 1.267, 1e-9, "saw SHAPE 0");
    near (mapping::sawShapeK (32.0 / 127.0), 0.0, 0.006, "saw SHAPE 32 is a plain saw");
    near (mapping::sawShapeK (1.0), -3.523, 1e-9, "saw SHAPE 127");
    near (mapping::triangleModGain (0.0), 2.015, 1e-9, "triangle mod gain at 0");
    near (mapping::triangleModGain (64.0 / 127.0), 1.6175, 1e-9, "triangle mod gain at 64");
    near (mapping::triangleModGain (1.0), 1.375, 1e-9, "triangle mod gain at 127");
    near (mapping::cutoffCoefficient (mapping::cutoffOctaves (64)), 0.039016, 1e-6, "cutoff 64");
    near (mapping::cutoffCoefficient (mapping::cutoffOctaves (96)) / 0.2498, 1.0, 0.015, "cutoff 96");
    near (mapping::cutoffCoefficient (mapping::cutoffOctaves (127)), 1.0, 1e-9, "cutoff 127 is fully open");
    near (mapping::resonanceQ (0.0), 0.993, 1e-9, "resonance 0");
    near (mapping::resonanceQ (64.0 / 127.0), 0.287, 1e-9, "resonance 64");

    // One measured harmonic set: SHAPE 0 at C3 gave n x amplitude of
    // 2.267, 1.330, 1.151 for the first three harmonics.
    std::vector<double> x (88200);
    for (std::size_t i = 0; i < x.size(); ++i)
        x[i] = osc::shapedSaw (std::fmod (i * 100.0 / kEngineRate, 1.0), 0.0);
    const double h30 = 30.0 * tone (x, 3000.0);
    const double measured[] { 2.267, 1.330, 1.151 };
    for (int n = 1; n <= 3; ++n)
        near (n * tone (x, 100.0 * n) / h30, measured[n - 1], 0.02, "SHAPE 0 harmonic " + std::to_string (n));
}

void filter()
{
    const double f1k = 2.0 * std::sin (kPi * 1000.0 / fs);
    const auto gain = [f1k] (FilterType type, bool steep, double hz, double resonance = 0.0)
    {
        Filter f;
        f.configure (f1k, resonance, type, steep);
        std::vector<double> x (44100);
        for (int i = -88200; i < 44100; ++i)
        {
            const double y = f.process (1e-3 * std::sin (kTwoPi * hz * i / fs));
            if (i >= 0)
                x[static_cast<std::size_t> (i)] = y * 1e3;
        }
        return tone (x, hz);
    };
    // OM p.60: -12 and -24 dB/oct in all three types.
    near (db (gain (FilterType::LowPass, false, 8000) / gain (FilterType::LowPass, false, 4000)), -12.4, 1.2, "LP12");
    near (db (gain (FilterType::LowPass, true, 8000) / gain (FilterType::LowPass, true, 4000)), -24.8, 1.8, "LP24");
    near (db (gain (FilterType::HighPass, false, 125) / gain (FilterType::HighPass, false, 250)), -12.0, 1.0, "HP12");
    near (db (gain (FilterType::HighPass, true, 125) / gain (FilterType::HighPass, true, 250)), -24.0, 1.5, "HP24");
    check (gain (FilterType::BandPass, true, 1000) > 4.0 * gain (FilterType::BandPass, true, 4000), "BP passes cutoff");

    // Measured: the -24 dB passband falls by the damping q as resonance
    // rises (-10.8 dB at 64), the -12 dB passband only above 64 (-3.0 dB at 96).
    near (db (gain (FilterType::LowPass, true, 50, 64.0 / 127.0)), -10.8, 0.4, "LP24 passband at resonance 64");
    near (db (gain (FilterType::LowPass, false, 50, 64.0 / 127.0)), 0.0, 0.2, "LP12 passband at resonance 64");
    near (db (gain (FilterType::LowPass, false, 50, 96.0 / 127.0)), -3.0, 0.3, "LP12 passband at resonance 96");
    near (db (gain (FilterType::LowPass, false, 1000, 96.0 / 127.0) / gain (FilterType::LowPass, false, 50, 96.0 / 127.0)),
          db (1.0 / 0.0748), 0.6, "LP12 peak at resonance 96");

    for (const bool steep : { false, true })
    {
        // OM p.60 calls full resonance oscillation. Measured, it is a ring
        // that lasts hundreds of cycles, not a sustained oscillator.
        Filter f;
        f.configure (f1k, 1.0, FilterType::LowPass, steep);
        f.process (0.5);
        std::vector<double> x (8820);
        for (int i = 0; i < 8820; ++i)
            f.process (0.0);
        for (auto& v : x)
            v = f.process (0.0);
        const double hz = peakHz (x, 950, 1050, 2);
        check (tone (x, hz) > 1.0e-4, "full resonance still rings after 100 cycles");
        near (hz, 1000.0, 10.0, "the ring tracks cutoff");
        check (*std::max_element (x.begin(), x.end()) < 4.0, "the ring is bounded");
    }
}

void circuit()
{
    // SN pp.23, 27, 30 component values: shelf (R41, R38, C24), Sallen-Key
    // (R45, R47, C26, C30) and the shunt on the JP-8000 panel B follower.
    const auto h = [] (OutputNode node, double hz) { return db (std::abs (AnalogOutput::response (node, hz))); };
    near (h (OutputNode::Rack, 20) - h (OutputNode::Rack, 1000), 2.9, 0.4, "bass shelf");
    near (h (OutputNode::Rack, 10000) - h (OutputNode::Rack, 1000), -0.4, 0.7, "flat to 10 kHz");
    near (h (OutputNode::KeyboardAsPrinted, 10000) - h (OutputNode::Rack, 10000), -19.5, 0.3, "printed pole");

    for (const auto node : { OutputNode::Rack, OutputNode::KeyboardAsPrinted })
    {
        const auto measure = [&] (double hz)
        {
            AnalogOutput a;
            a.prepare (node);
            std::vector<double> x (44100);
            for (int i = -176400; i < 44100; ++i)
            {
                const double y = a.process ({ 0.25 * std::sin (kTwoPi * hz * i / fs), 0.0 }).l;
                if (i >= 0)
                    x[static_cast<std::size_t> (i)] = y;
            }
            return db (tone (x, hz) / 0.25);
        };
        const double reference = measure (1000) - h (node, 1000);
        for (const double hz : { 50.0, 10000.0, 18000.0 })
        {
            const double p = kPi * hz / fs;
            near (measure (hz) - reference, h (node, hz) + db (std::sin (p) / p), 0.15,
                  "discretised circuit at " + std::to_string (hz));
        }
    }
}

std::vector<double> renderNote (const Patch& patch, double rate, double seconds, int note = 69, int block = 256)
{
    Engine e;
    e.prepare (rate);
    e.setPatch (Part::Upper, patch);
    e.noteOn (note, 100);
    const int total = static_cast<int> (rate * seconds);
    std::vector<float> l (static_cast<std::size_t> (total)), r (l.size());
    for (int at = 0; at < total; at += block)
        e.render (l.data() + at, r.data() + at, std::min (block, total - at));
    return { l.begin(), l.end() };
}

void engine()
{
    Patch p;
    p.osc1.wave = Wave1::Saw;
    p.osc1.ctrl1 = 32; // the plain saw, OM p.56 figure
    p.balance = -64;
    for (const double rate : { 44100.0, 48000.0, 96000.0, 192000.0 })
    {
        auto x = renderNote (p, rate, 1.2);
        x.erase (x.begin(), x.begin() + static_cast<long> (rate * 0.2));
        near (peakHz (x, 438, 442, 0.1, rate), 440.0, 0.2, "A4 at " + std::to_string (rate));
    }
    check (renderNote (p, 48000, 0.2, 60, 1) == renderNote (p, 48000, 0.2, 60, 997), "block-size independent");
    check (renderNote (p, 48000, 0.2) == renderNote (p, 48000, 0.2), "deterministic");

    Engine e;
    e.prepare (48000);
    float l[480], r[480];
    e.render (l, r, 480);
    check (std::all_of (l, l + 480, [] (float v) { return v == 0.0f; }), "silent before a note");
    e.setPatch (Part::Upper, p);
    for (int i = 0; i < 12; ++i)
        e.noteOn (48 + i, 100);
    e.render (l, r, 480);
    check (e.activeVoices() == 8, "eight voices, last note priority (OM p.48)");
    for (int i = 0; i < 12; ++i)
        e.noteOff (48 + i);
    for (int i = 0; i < 600; ++i)
        e.render (l, r, 480);
    check (e.activeVoices() == 0, "released voices free up");

    Patch fb;
    fb.osc1.wave = Wave1::FeedbackOsc;
    e.setPatch (Part::Upper, fb);
    e.noteOn (60, 100);
    e.noteOn (64, 100);
    e.render (l, r, 480);
    check (e.activeVoices() == 1, "feedback oscillator is monophonic (OM p.56)");

    // JP-8080 OM: UNISON sounds all the part's voices, and cannot be turned
    // on for the feedback oscillator.
    fb.unison = true;
    e.reset();
    e.setPatch (Part::Upper, fb);
    e.noteOn (60, 100);
    e.render (l, r, 480);
    check (e.activeVoices() == 1, "no unison with the feedback oscillator");
    fb.unison = false;
    Patch stacked = p;
    stacked.unison = true;
    e.reset();
    e.setPatch (Part::Upper, stacked);
    e.noteOn (60, 100);
    e.render (l, r, 480);
    check (e.activeVoices() == 8, "unison sounds all eight voices");

    e.reset();
    e.setMode (KeyMode::Dual);
    e.setPatch (Part::Lower, p);
    e.setPatch (Part::Upper, p);
    for (int i = 0; i < 8; ++i)
        e.noteOn (50 + i, 100);
    e.render (l, r, 480);
    check (e.activeVoices() == 8, "dual splits the eight voices 4-4");

    allocations = 0;
    countAllocations = true;
    e.noteOn (70, 100);
    e.setPatch (Part::Upper, fb);
    e.render (l, r, 480);
    e.allNotesOff();
    countAllocations = false;
    check (allocations == 0, "no allocation on the audio thread");
}

void feedback()
{
    // OM p.56: like guitar feedback, with AMOUNT setting the level of the
    // harmonics. A howl has to sit on a harmonic of the played note and
    // HARMONICS has to choose a different one.
    const auto howl = [] (int harmonics, int amount)
    {
        Patch p;
        p.osc1 = { Wave1::FeedbackOsc, harmonics, amount };
        p.balance = -64;
        auto x = renderNote (p, 48000, 3.0, 57);
        x.erase (x.begin(), x.begin() + 96000);
        return x;
    };
    // JE-8086 at AMOUNT 127: HARMONICS 0 howls on the 2nd harmonic, 32 on
    // the 8th, 64 on the 4th, exactly (Docs/research/black-box-je8086-2026-11-22.md).
    for (const auto& [harmonics, expected] : { std::pair { 0, 2 }, std::pair { 32, 8 }, std::pair { 64, 4 } })
    {
        const auto x = howl (harmonics, 127);
        double best = 0.0, level = 0.0;
        for (int n = 1; n <= 12; ++n)
        {
            const double hz = peakHz (x, 220.0 * n - 20, 220.0 * n + 20, 1.0, 48000);
            if (const double a = tone (x, hz, 48000); a > level)
            {
                level = a;
                best = hz;
            }
        }
        near (best / 220.0, expected, 0.01, "howl on harmonic " + std::to_string (expected));
        check (db (level / tone (x, 220.0, 48000)) > 6.0, "howl stands over the fundamental");
    }
}

void effects()
{
    // OM p.65: delay times with TIME fully right.
    const std::pair<DelayType, double> cases[] {
        { DelayType::MonoLong, 1.250 }, { DelayType::PanLR, 0.625 },
        { DelayType::PanShort, 0.15625 }, { DelayType::MonoShort, 0.15625 } };
    for (const auto& [type, seconds] : cases)
    {
        Delay d;
        d.prepare();
        d.configure (type, 127, 0, 127);
        d.snap();
        int first = -1;
        for (int i = 0; i < 130000 && first < 0; ++i)
        {
            const auto y = d.process (i == 0 ? 1.0 : 0.0);
            if (i > 0 && std::abs (type == DelayType::PanShort ? y.r : y.l) > 0.05)
                first = i;
        }
        near (first, seconds * fs, 3.0, "delay maximum");
    }
    // JE-8086: MONO LONG at 64 answers after 0.2 s; feedback 127 loses 2.2 dB a repeat.
    near (mapping::delaySeconds (DelayType::MonoLong, 64), 0.2, 1e-9, "delay time 64");
    near (db (mapping::delayFeedback (127)), -2.2, 0.05, "feedback 127");
}

void stability()
{
    Random random (99);
    const auto byte = [&] { return static_cast<int> (random.next() % 128u); };
    const auto signed7 = [&] { return byte() - 64; };
    Engine e;
    e.prepare (48000);
    bool finite = true;
    float l[256], r[256];
    for (int trial = 0; trial < 1500; ++trial)
    {
        Patch p;
        p.osc1 = { static_cast<Wave1> (trial % 7), byte(), byte() };
        p.osc2.wave = static_cast<Wave2> (byte() % 3);
        p.osc2.sync = byte() & 1;
        p.osc2.range = byte() % 51 - 25;
        p.osc2.fine = byte() % 101 - 50;
        p.ring = byte() & 1;
        p.xmod = byte();
        p.balance = signed7();
        p.filter.type = static_cast<FilterType> (byte() % 3);
        p.filter.steep = byte() & 1;
        p.filter.cutoff = byte();
        p.filter.resonance = byte();
        p.filter.envDepth = signed7();
        p.filter.lfoDepth = signed7();
        p.pitchLfo = signed7();
        p.pitchEnvDepth = signed7();
        p.lfo1.rate = byte();
        p.effects.chorusType = static_cast<ChorusType> (byte() % 12);
        p.effects.chorus = byte();
        p.effects.delayType = static_cast<DelayType> (byte() % 5);
        p.effects.delay = byte();
        p.effects.feedback = byte();
        p.effects.delayTime = byte();
        p.unison = trial % 5 == 0;
        e.setPatch (Part::Upper, p);
        e.noteOn (byte(), byte() | 1);
        for (int b = 0; b < 5; ++b)
        {
            e.render (l, r, 256);
            for (int i = 0; i < 256; ++i)
                finite = finite && std::isfinite (l[i]) && std::isfinite (r[i])
                         && std::abs (l[i]) < 4.0f && std::abs (r[i]) < 4.0f;
        }
        if (trial % 3 == 0)
            e.allNotesOff();
    }
    check (finite, "random patches stay finite and bounded");
}
// Timing and modulation laws from the second black-box round on JE-8086
// (Docs/research/black-box-je8086-2026-11-22.md, "Round 2").
double zeroCrossingHz (const std::vector<double>& x, std::size_t from, std::size_t to, double rate)
{
    double first = -1.0, last = 0.0;
    int count = 0;
    for (std::size_t i = from + 1; i < to; ++i)
        if (x[i - 1] < 0.0 && x[i] >= 0.0)
        {
            const double t = static_cast<double> (i - 1) + x[i - 1] / (x[i - 1] - x[i]);
            if (first < 0.0)
                first = t;
            else
                ++count;
            last = t;
        }
    return count > 0 ? rate * count / (last - first) : 0.0;
}

void timing()
{
    near (mapping::envelopeSeconds60 (64), 2.176, 1e-3, "decay and release 64 fall 60 dB in 2.18 s");
    near (mapping::envelopeSeconds60 (16), 0.1355, 1e-4, "envelope 16");
    near (mapping::envelopeSeconds60 (112), 18.2, 0.01, "envelope 112");
    near (mapping::envelopeSeconds60 (120), 57.58, 0.01, "envelope 120, past the step at 117");
    near (mapping::envelopeSeconds60 (127), 100.9, 0.01, "envelope 127");
    near (mapping::lfoHz (0), 0.0499, 1e-4, "LFO 1 rate 0");
    near (mapping::lfoHz (64), 3.225, 1e-3, "LFO 1 rate 64");
    near (mapping::lfoHz (66), std::sqrt (3.225 * 3.674), 1e-3, "LFO rate between points is geometric");
    near (mapping::lfoHz (127), 50.04, 0.01, "LFO 1 rate 127");
    near (mapping::pitchEnvSemitones (32), 14.57, 1e-3, "pitch env depth +32");
    near (mapping::pitchEnvSemitones (-64), -48.324, 1e-3, "pitch env depth -64 is four octaves down");
    near (mapping::pitchLfoSemitones (32), 7.285, 1e-3, "pitch LFO depth is half the envelope's");
    near (mapping::filterEnvSemitones (32), 36.13, 1e-3, "filter env depth +32");
    near (mapping::filterEnvSemitones (-48), -75.98, 1e-3, "filter env depth is symmetric");
    near (mapping::filterLfoSemitones (40), 27.39, 1e-3, "filter LFO depth is half the envelope's");
    near (mapping::keyFollow (-64), -1.0, 1e-9, "key follow -64 is -100 %");
    near (mapping::keyFollow (32), 1.0, 1e-9, "key follow +32 is 100 %");
    near (mapping::portamentoSeconds (64), 0.316, 1e-3, "portamento 64");
    near (mapping::portamentoSeconds (0), 0.0, 1e-9, "portamento 0 jumps");

    // Attack rises toward 1.2525 with the decay's time constant and stops at
    // 1, so it takes ln 4.96 time constants; 64 reaches half in 0.161 s.
    Envelope env;
    const double tau = mapping::envelopeSeconds60 (64) / 6.907755;
    env.configure (tau, tau, 0.0, tau);
    env.trigger();
    int half = -1, full = -1;
    for (int i = 0; i < static_cast<int> (fs); ++i)
    {
        const double v = env.process();
        if (half < 0 && v >= 0.5)
            half = i;
        if (full < 0 && v >= 0.999)
            full = i;
    }
    near (half / fs, 0.509 * tau, 0.002, "attack 64 reaches half");
    near (full / fs, 1.6014 * tau, 0.003, "attack 64 reaches the top");

    // Portamento is a straight line in pitch that takes the same time for
    // any interval: 48 to 72 at 64 passes 60 halfway through 0.316 s.
    Patch p;
    p.osc1.wave = Wave1::Triangle;
    p.balance = -64;
    p.mono = p.portamento = true;
    p.portamentoTime = 64;
    p.amp.sustain = 127;
    Engine e;
    e.prepare (fs);
    e.setPatch (Part::Upper, p);
    std::vector<float> l (static_cast<std::size_t> (fs)), r (l.size());
    e.noteOn (48, 100);
    e.render (l.data(), r.data(), 8820);
    e.noteOn (72, 100);
    e.render (l.data(), r.data(), static_cast<int> (l.size()));
    const std::vector<double> x (l.begin(), l.end());
    const auto at = [&] (double t) { return static_cast<std::size_t> (t * fs); };
    const double mid = zeroCrossingHz (x, at (0.158 - 0.006), at (0.158 + 0.006), fs);
    near (12.0 * std::log2 (mid / 261.6256), 0.0, 0.35, "glide passes C4 halfway");
    const double end = zeroCrossingHz (x, at (0.4), at (0.6), fs);
    near (12.0 * std::log2 (end / 523.2511), 0.0, 0.02, "glide lands on C5");
}

// Round 3 on JE-8086 (Docs/research/black-box-je8086-2026-11-22.md, "Round 3").
void modulation()
{
    near (mapping::attackHoldSeconds (28), 0.0, 1e-12, "attack below 30 starts at once");
    near (mapping::attackHoldSeconds (30), 0.014, 1e-12, "attack from 30 starts 14 ms late");
    near (mapping::attackTau (16), 0.0042113 * std::exp (0.08069 * 16), 1e-12, "attack 16 is faster than the table");
    near (mapping::attackTau (64), mapping::envelopeTau (64), 1e-12, "attack from 30 follows the table");
    near (mapping::segmentHoldSeconds (5), 0.0, 1e-12, "decay and release below 10 start at once");
    near (mapping::segmentHoldSeconds (16), 0.0075, 1e-12, "decay and release from 10 start 7.5 ms late");
    near (mapping::segmentHoldSeconds (64), 0.025, 1e-12, "decay and release from 30 start 25 ms late");

    Envelope env;
    const double tau = mapping::envelopeTau (64);
    env.configure (tau, tau, 0.0, tau, 0.014, 0.025, 0.025);
    env.trigger();
    int half = -1;
    for (int i = 0; half < 0 && i < static_cast<int> (fs); ++i)
        if (env.process() >= 0.5)
            half = i;
    near (half / fs, 0.014 + 0.509 * tau, 0.002, "held attack reaches half");

    near (mapping::fadeSeconds (64), 2.176 / 4.45, 1e-3, "FADE 64 is full after 0.49 s");
    near (mapping::fadeSeconds (0), 0.0, 1e-12, "FADE 0 is off");
    near (db (mapping::fadeLevel (0.0, 1.0)), -37.0, 1e-6, "fade starts 37 dB down");
    near (db (mapping::fadeLevel (0.5, 1.0)), -18.5, 1e-6, "fade rises in decibels");
    near (mapping::fadeLevel (1.2, 1.0), 1.0, 1e-12, "fade stops at full");

    near (mapping::lfo2Hz (0), 0.781, 1e-3, "LFO 2 rate 0");
    near (mapping::lfo2Hz (64), 4.41, 0.03, "LFO 2 rate 64");
    near (mapping::lfo2Hz (120), 19.99, 0.15, "LFO 2 rate 120");
    near (mapping::lfo2Hz (127), 50.0, 1e-9, "LFO 2 rate 127 jumps to 50 Hz");

    near (mapping::ampLfoDepth (32), 0.2873, 1e-3, "amp LFO depth 32 swings 29 %");
    near (mapping::ampLfoDepth (63), 0.953, 1e-3, "amp LFO depth 63");
    near (mapping::crossModHz (1.0), 8776.0, 1e-9, "X-MOD 127 swings 8.8 kHz");
    near (mapping::balanceGain (0, true), std::sqrt (0.5), 1e-9, "balance centre is -3 dB each");
    near (mapping::balanceGain (-64, true), 1.0, 1e-9, "balance OSC 1");
    near (mapping::balanceGain (-64, false), 0.0, 1e-9, "balance OSC 1 mutes OSC 2");
    near (mapping::balanceGain (32, false), std::sin (kPi / 4.0 * (1.0 + 32.0 / 63.0)), 1e-9, "balance is constant power");
    near (mapping::noiseCoefficient (261.6256, 64.0 / 127.0), kTwoPi * 261.6256 / fs, 1e-12, "noise cutoff 64 sits on the note");
    near (mapping::noiseCoefficient (261.6256, 76.0 / 127.0), 2.0 * kTwoPi * 261.6256 / fs, 1e-12, "noise cutoff, a semitone a step");
}


// Round 5 on JE-8086: TONE CONTROL as first-order shelves.
void toneControl()
{
    const auto gainAt = [] (int bass, int treble, double hz)
    {
        ToneControl t;
        t.configure (bass, treble);
        std::vector<double> x (static_cast<std::size_t> (fs));
        for (std::size_t i = 0; i < x.size(); ++i)
            x[i] = t.process (std::sin (kTwoPi * hz * static_cast<double> (i) / fs));
        x.erase (x.begin(), x.begin() + 44100);
        return db (tone (x, hz));
    };
    near (gainAt (0, 0, 1000.0), 0.0, 0.05, "tone flat at 0");
    near (gainAt (0, 63, 16000.0), 14.3, 0.8, "treble +63 lifts 16 kHz 14 dB");
    near (gainAt (0, 32, 16000.0), 9.7, 0.8, "treble +32 lifts 16 kHz 10 dB");
    near (gainAt (0, -64, 16000.0), -14.4, 0.8, "treble -64 cuts 16 kHz 14 dB");
    near (gainAt (0, 63, 250.0), 0.3, 0.8, "treble leaves the bass");
    near (gainAt (63, 0, 31.5), 22.2, 1.5, "bass +63 lifts 31 Hz 22 dB");
    near (gainAt (32, 0, 63.0), 6.7, 1.0, "bass +32 lifts 63 Hz 7 dB");
    near (gainAt (-64, 0, 31.5), -22.5, 1.5, "bass -64 cuts 31 Hz 22 dB");
    near (gainAt (63, 0, 4000.0), 0.2, 0.5, "bass leaves the treble");
}

// Factory-patch round on JE-8086: amp SUSTAIN is square-law, filter SUSTAIN linear.
void sustainLaw()
{
    near (mapping::ampSustain (64), 0.254, 0.002, "amp sustain 64 is a quarter (-11.9 dB)");
    near (mapping::ampSustain (127), 1.0, 1e-12, "amp sustain 127");
    near (mapping::filterSustain (64), 64.0 / 127.0, 1e-12, "filter sustain is linear");
    Patch p;
    p.osc1.wave = Wave1::Triangle;
    p.balance = -64;
    p.amp = { 0, 0, 64, 5 };
    auto x = renderNote (p, fs, 0.6, 60);
    p.amp.sustain = 127;
    auto y = renderNote (p, fs, 0.6, 60);
    double ex = 0.0, ey = 0.0;
    for (std::size_t i = static_cast<std::size_t> (0.3 * fs); i < x.size(); ++i)
    {
        ex += x[i] * x[i];
        ey += y[i] * y[i];
    }
    near (10.0 * std::log10 (ex / ey), -11.9, 0.3, "sustain 64 plays 11.9 dB down");
}

// Factory-patch differences on JE-8086 (Dreams Are Made): key follow tracks
// the sounding pitch, after OSC SHIFT.
void keyFollowShift()
{
    const auto brightness = [] (int shift)
    {
        Patch p;
        p.osc1.wave = Wave1::Saw;
        p.balance = -64;
        p.filter.cutoff = 40;
        p.filter.keyFollow = -40;
        p.oscShift = shift;
        auto x = renderNote (p, fs, 0.8, 60);
        x.erase (x.begin(), x.begin() + static_cast<long> (0.3 * fs));
        return db (tone (x, 4.0 * 261.6256 * std::exp2 (shift)));
    };
    // Two octaves down with key follow -40 (-62.5 %) opens the filter by
    // 15 steps; at 0 the pivot (C4) leaves it alone.
    Patch flat;
    flat.osc1.wave = Wave1::Saw;
    flat.balance = -64;
    flat.filter.cutoff = 40 + 15;
    flat.oscShift = -2;
    auto x = renderNote (flat, fs, 0.8, 60);
    x.erase (x.begin(), x.begin() + static_cast<long> (0.3 * fs));
    near (brightness (-2), db (tone (x, 4.0 * 261.6256 / 4.0)), 1.0, "key follow sees OSC SHIFT");
}

// Factory-patch differences on JE-8086 (Moody Organ): LFO 1 and the pitch
// envelope reach X-MOD on the same depth curve as pitch, so a small depth
// barely moves it.
void xmodDestination()
{
    near (mapping::xmodModDepth (-3), -mapping::pitchEnvSemitones (3) / mapping::pitchEnvTable[16], 1e-12,
          "X-MOD destination depth follows the pitch-depth curve");
    check (std::abs (mapping::xmodModDepth (-3)) < 0.005, "depth 3 moves X-MOD by under 0.5 %");
    near (mapping::xmodModDepth (63), 1.0, 1e-12, "full depth sweeps all of X-MOD");
}

// Factory-patch differences on JE-8086: effect levels are square-law, and
// each chorus type adds the level the emulator adds.
void effectLevels()
{
    near (mapping::effectLevel (64), 0.254, 0.002, "effect level 64 is a quarter");
    near (mapping::effectLevel (127), 1.0, 1e-12, "effect level 127");
    near (mapping::chorusWetGain (ChorusType::SuperFast), 0.079, 1e-9, "SUPER CHORUS FST adds almost no level");
    near (mapping::chorusWetGain (ChorusType::SuperSlow), 1.402, 1e-9, "SUPER CHORUS SLW adds 4 dB at 127");
}

// The flat control list every front end uses: unique ids, every value of every
// control lands in the patch and reads back, and the initial values are the
// default patch's.
void controls()
{
    const Patch defaults;
    for (int c = 0; c < kControlCount; ++c)
    {
        const ControlInfo& info = kControls[static_cast<std::size_t> (c)];
        near (controlValue (defaults, c), info.initial, 0, std::string (info.id) + " starts at the default patch's value");
        check (info.steps == 0 || info.steps == info.max - info.min + 1, std::string (info.id) + ": a choice has one step a value");
        for (int other = 0; other < c; ++other)
            check (std::string (kControls[static_cast<std::size_t> (other)].id) != info.id, std::string (info.id) + " is unique");
        bool roundTrip = true;
        for (int v = info.min; v <= info.max; ++v)
        {
            Patch p;
            applyControl (p, c, v);
            roundTrip = roundTrip && controlValue (p, c) == v;
        }
        check (roundTrip, std::string (info.id) + " reads back every value");
        Patch p;
        applyControl (p, c, info.max + 1000);
        near (controlValue (p, c), info.max, 0, std::string (info.id) + " clamps");
    }
    // A host's master tuning moves every note.
    const auto pitchOf = [] (double cents)
    {
        Patch p;
        p.osc1.wave = Wave1::Triangle;
        p.balance = -64;
        Engine e;
        e.prepare (fs);
        e.setTune (cents);
        e.setPatch (Part::Upper, p);
        e.noteOn (69, 100);
        std::vector<float> l (static_cast<std::size_t> (fs)), r (l.size());
        e.render (l.data(), r.data(), static_cast<int> (l.size()));
        std::vector<double> x (l.begin() + 22050, l.end());
        return peakHz (x, 400, 480, 0.25);
    };
    near (1200.0 * std::log2 (pitchOf (50.0) / pitchOf (0.0)), 50.0, 1.5, "master tune +50 cents");
}
} // namespace

void* operator new (std::size_t size)
{
    if (countAllocations)
        ++allocations;
    if (void* p = std::malloc (size ? size : 1))
        return p;
    throw std::bad_alloc();
}
void operator delete (void* p) noexcept { std::free (p); }
void operator delete (void* p, std::size_t) noexcept { std::free (p); }

int main (int argc, char** argv)
{
    const std::pair<const char*, std::function<void()>> suites[] {
        { "SuperSaw", superSaw }, { "HardwareRecording", hardwareRecording }, { "MeasuredLaws", measuredLaws }, { "Filter", filter }, { "Circuit", circuit },
        { "Engine", engine }, { "Feedback", feedback }, { "Effects", effects }, { "Stability", stability }, { "Timing", timing }, { "Modulation", modulation }, { "ToneControl", toneControl }, { "SustainLaw", sustainLaw }, { "KeyFollowShift", keyFollowShift }, { "XmodDestination", xmodDestination }, { "EffectLevels", effectLevels }, { "Controls", controls } };
    for (const auto& [name, run] : suites)
        if (argc < 2 || std::string (argv[1]) == name)
            run();
    std::printf ("%d checks, %d failed\n", checks, failures);
    return failures || ! checks ? 1 : 0;
}
