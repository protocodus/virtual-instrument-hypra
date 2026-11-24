// Behavioural checks for the proof of concept. Each expectation cites what
// pins it: the JP-8000 owner's manual (OM), the service notes (SN), or
// Szabo 2010 (a black-box measurement of the hardware).
#include "../Source/DSP/TranceraControls.h"
#include "../Source/DSP/TranceraEngine.h"
#include "../Source/DSP/TranceraSysEx.h"

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

using namespace trancera;
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
    // Side-to-centre gain ratio and attenuation match the linear mix law across the knob.
    near (mapping::superSide (0.0) / mapping::superCentre (0.0), 400.0 / 6400.0, 1e-6, "side/centre ratio at mix 0");
    near (mapping::superCentre (95.0 / 127.0) / mapping::superCentre (0.0), 0.583, 0.005, "centre gain at mix 95");
    near (mapping::superSide (1.0) / mapping::superCentre (0.0), 0.590, 0.005, "side gain at mix 127");
    near (mapping::superDetune (63.0 / 127.0), (126.0 * 0.5 + 2.0) / 642.0, 1e-6, "detune at 63");
    near (mapping::superDetune (124.0 / 127.0), (672.0 * 0.5 + 2.0) / 642.0, 1e-6, "detune climbs steeply from 120");
    near (mapping::superDetune (1.0), 1.0, 1e-9, "detune at 127");
    bool rising = true;
    for (int v = 1; v < 128; ++v)
        if (v != 103 && v != 104)
            rising = rising && mapping::superDetune (v / 127.0) >= mapping::superDetune ((v - 1) / 127.0);
    check (rising, "detune rises monotonically aside from table step 103");

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
    near (mapping::waveShape (0.0), 0.0, 1e-9, "saw SHAPE 0");
    near (mapping::waveShape (32.0 / 127.0), 0.25, 1e-9, "saw SHAPE 32 is plain saw");
    near (mapping::waveShape (1.0), 124.0 / 128.0, 1e-9, "saw SHAPE 127 clamps to 124/128");
    near (mapping::triangleModGain (0.0), 2.015625, 1e-9, "triangle mod gain at 0");
    near (mapping::triangleModGain (64.0 / 127.0), 1.6171875, 1e-9, "triangle mod gain at 64");
    near (mapping::triangleModGain (1.0), 1.375, 1e-9, "triangle mod gain at 127");
    near (mapping::cutoffCoefficient (mapping::cutoffOctaves (64)), 5.0 / 128.0, 1e-9, "cutoff 64");
    near (mapping::cutoffCoefficient (mapping::cutoffOctaves (96)), (5.0 / 128.0) * std::exp2 (32.0 / 12.0), 1e-9, "cutoff 96");
    near (mapping::cutoffCoefficient (mapping::cutoffOctaves (127)), 1.0, 1e-9, "cutoff 127 is fully open");
    near (mapping::resonanceQ (0.0), 1.0, 1e-9, "resonance 0");
    near (mapping::resonanceQ (64.0 / 127.0), 0.28894, 0.002, "resonance 64");

    // Plain saw at SHAPE 32 has pure 1/n harmonics; SHAPE 0 softens high harmonics.
    std::vector<double> x0 (88200), x32 (88200);
    for (std::size_t i = 0; i < x0.size(); ++i)
    {
        const double u = std::fmod (i * 100.0 / kEngineRate, 1.0);
        x0[i] = osc::shapedSaw (u, 0.0);
        x32[i] = osc::shapedSaw (u, 32.0 / 127.0);
    }
    const double h30 = 30.0 * tone (x0, 3000.0);
    near (tone (x0, 100.0) / h30, 2.21, 0.1, "SHAPE 0 harmonic 1 boost");
    near (tone (x32, 100.0) / (2.0 * tone (x32, 200.0)), 1.0, 0.02, "SHAPE 32 is a 1/n saw");
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
    stacked.mono = true;
    stacked.unison = true;
    e.reset();
    e.setPatch (Part::Upper, stacked);
    e.noteOn (60, 100);
    e.render (l, r, 480);
    check (e.activeVoices() == 8, "mono unison sounds all eight voices");

    Patch polyUni = p;
    polyUni.mono = false;
    polyUni.unison = true;
    e.reset();
    e.setPatch (Part::Upper, polyUni);
    for (int i = 0; i < 4; ++i)
        e.noteOn (60 + i * 3, 100);
    e.render (l, r, 480);
    check (e.activeVoices() == 8, "poly unison pairs 2 voices per note up to 4 notes (8 voices)");

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
    // Delay feedback follows square law and damps high frequencies through the loop LPF.
    near (mapping::delaySeconds (DelayType::MonoLong, 64), 0.2, 1e-9, "delay time 64");
    near (mapping::delayFeedback (127), 1.0, 1e-9, "feedback 127");
    near (mapping::delayFeedback (64), (64.0 / 127.0) * (64.0 / 127.0), 1e-9, "feedback 64 square-law");
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
        p.effects.chorusType = static_cast<ChorusType> (byte() % 13);
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
    near (mapping::envelopeSeconds60 (64), 2.1758, 1e-3, "decay and release 64 fall 60 dB in 2.18 s");
    near (mapping::envelopeSeconds60 (16), 0.1332, 1e-3, "envelope 16");
    near (mapping::envelopeSeconds60 (112), 17.65, 0.02, "envelope 112");
    near (mapping::envelopeSeconds60 (120), 56.55, 0.05, "envelope 120, past the step at 117");
    near (mapping::envelopeSeconds60 (127), 94.28, 0.05, "envelope 127");
    near (mapping::lfoHz (0), 32.0 * 50.0 / 32768.0, 1e-6, "LFO 1 rate 0");
    near (mapping::lfoHz (64), 2115.0 * 50.0 / 32768.0, 1e-6, "LFO 1 rate 64");
    near (mapping::lfoHz (127), 50.0, 1e-6, "LFO 1 rate 127");
    near (mapping::pitchEnvSemitones (32), 14.48, 1e-6, "pitch env depth +32");
    near (mapping::pitchEnvSemitones (-64), -48.0, 1e-6, "pitch env depth -64 is four octaves down");
    near (mapping::pitchLfoSemitones (32), 7.24, 1e-6, "pitch LFO depth is half the envelope's");
    near (mapping::filterEnvSemitones (32), 36.21, 1e-6, "filter env depth +32");
    near (mapping::filterEnvSemitones (-48), -76.15, 1e-6, "filter env depth is symmetric");
    near (mapping::filterLfoSemitones (40), 27.44, 1e-6, "filter LFO depth is half the envelope's");
    near (mapping::keyFollow (-64), -1.0, 1e-9, "key follow -64 is -100 %");
    near (mapping::keyFollow (32), 1.0, 1e-9, "key follow +32 is 100 %");
    near (mapping::portamentoSeconds (64), 0.320, 1e-6, "portamento 64");
    near (mapping::portamentoSeconds (0), 0.0, 1e-9, "portamento 0 jumps");

    // Attack steps at 100 Hz toward 1.25 and clamps at 1.0.
    Envelope env;
    const double tau = mapping::envelopeTau (64);
    env.configure (64, 64, 0.0, 64);
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
    near (half / fs, 0.509 * tau, 0.02, "attack 64 reaches half");
    near (full / fs, 1.6014 * tau, 0.02, "attack 64 reaches the top");

    // Portamento glides linearly in pitch across the set duration.
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
    const double mid = zeroCrossingHz (x, at (0.165 - 0.005), at (0.165 + 0.005), fs);
    near (12.0 * std::log2 (mid / 261.6256), 0.0, 0.35, "glide passes C4 halfway");
    const double end = zeroCrossingHz (x, at (0.4), at (0.6), fs);
    near (12.0 * std::log2 (end / 523.2511), 0.0, 0.02, "glide lands on C5");
}

void modulation()
{
    Envelope env;
    env.configure (32, 32, 0.0, 5);
    env.trigger();
    const double step0 = env.value();
    near (step0, 1.25 * 1341.0 / 8192.0, 1e-6, "attack 32 initial 100 Hz step");
    near (env.smootherAlpha(), 1.0 / 2048.0, 1e-12, "attack >= 30 uses slow smoother");
    for (int i = 0; i < kEnvelopeInterval - 1; ++i)
        near (env.process(), step0, 1e-9, "stepped envelope holds across 10 ms tick");
    check (env.process() > step0, "envelope advances on the 100 Hz tick");

    near (mapping::fadeSeconds (64), 16384.0 / (321.0 * 100.0), 1e-4, "FADE 64 matches table rate");
    near (mapping::fadeSeconds (0), 0.0, 1e-12, "FADE 0 is off");
    near (mapping::fadeLevel (0.0, 1.0), 0.0, 1e-12, "fade starts at zero");
    near (mapping::fadeLevel (0.5, 1.0), 2429.0 / 16384.0, 1e-9, "fade midpoint follows curve table");
    near (mapping::fadeLevel (1.2, 1.0), 1.0, 1e-12, "fade stops at full");

    near (mapping::lfo2Hz (0), 0.78125, 1e-5, "LFO 2 rate 0");
    near (mapping::lfo2Hz (64), 2896.0 * 50.0 / 32768.0, 1e-5, "LFO 2 rate 64");
    near (mapping::lfo2Hz (120), 20.0, 1e-3, "LFO 2 rate 120");
    near (mapping::lfo2Hz (127), 50.0, 1e-6, "LFO 2 rate 127 jumps to 50 Hz");

    near (mapping::ampLfoDepth (32), 1236.0 / 4096.0, 1e-6, "amp LFO depth 32");
    near (mapping::ampLfoDepth (63), 1.0, 1e-6, "amp LFO depth 63");
    near (mapping::crossModHz (1.0), 0.5 * (51.0 / 128.0) * (0.5 * kEngineRate), 1e-6, "X-MOD 127 peak deviation");
    near (mapping::balanceGain (0, true), 0.703, 0.01, "balance centre is near -3 dB each");
    near (mapping::balanceGain (-64, true), 1.0, 1e-9, "balance OSC 1");
    near (mapping::balanceGain (-64, false), 0.0, 1e-9, "balance OSC 1 mutes OSC 2");
    near (mapping::balanceGain (32, false), (23348.0 / 32768.0) * (7539.0 / 5792.0), 1e-6, "balance +32 follows table");
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
    near (mapping::xmodModDepth (-3), mapping::pitchEnvSemitones (-3) / 48.0, 1e-12,
          "X-MOD destination depth follows the pitch-depth curve");
    check (std::abs (mapping::xmodModDepth (-3)) < 0.005, "depth 3 moves X-MOD by under 0.5 %");
    near (mapping::xmodModDepth (63), 1.0, 1e-12, "full depth sweeps all of X-MOD");
}

void effectLevels()
{
    near (mapping::effectLevel (64), 0.254, 0.002, "effect level 64 is a quarter");
    near (mapping::effectLevel (127), 1.0, 1e-12, "effect level 127");
}

void triangleShape()
{
    // OSC 1 Triangle morphs from skewed fold to Chebyshev T3 wavefolding.
    std::vector<double> x0 (88200), x32 (88200), x64 (88200), x127 (88200);
    for (std::size_t i = 0; i < x0.size(); ++i)
    {
        const double p = 2.0 * std::fmod (i * 220.0 / fs, 1.0) - 1.0;
        x0[i] = osc::shapedTriangleHardware (p, mapping::waveShape (0.0));
        x32[i] = osc::shapedTriangleHardware (p, mapping::waveShape (32.0 / 127.0));
        x64[i] = osc::shapedTriangleHardware (p, mapping::waveShape (64.0 / 127.0));
        x127[i] = osc::shapedTriangleHardware (p, mapping::waveShape (1.0));
    }
    check (tone (x32, 660.0) / tone (x32, 220.0) > 0.35, "SHAPE 32 increases 3rd harmonic relative to fundamental");
    check (tone (x64, 440.0) > tone (x64, 220.0) * 1.5, "SHAPE 64 cancels fundamental into upper folds");
    check (tone (x32, 440.0) > tone (x0, 440.0) * 5.0, "SHAPE 32 cubic phase warp adds even harmonics");
    check (tone (x127, 1100.0) > tone (x0, 1100.0) * 3.0, "SHAPE 127 increases upper odd harmonic content");
    check (*std::max_element (x0.begin(), x0.end()) > *std::max_element (x127.begin(), x127.end()),
           "Triangle (1 - 0.5*s) attenuates peak at full SHAPE");
}

void multiFxTypes()
{
    std::array<double, 12> widths {};
    for (int t = 0; t < 12; ++t)
    {
        Chorus c;
        c.reset();
        c.configure (static_cast<ChorusType> (t), 127);
        c.snap();
        double diffSq = 0.0, maxAbs = 0.0;
        for (int i = 0; i < 44100; ++i)
        {
            const double dry = 0.25 * std::sin (kTwoPi * 220.0 * i / fs);
            const Stereo y = c.processMix ({ dry, dry });
            diffSq += (y.l - y.r) * (y.l - y.r);
            maxAbs = std::max ({ maxAbs, std::abs (y.l), std::abs (y.r) });
        }
        widths[static_cast<std::size_t> (t)] = std::sqrt (diffSq / 44100.0);
        check (maxAbs > 0.1 && maxAbs < 1.0, "Multi-FX type stays finite and bounded: " + std::to_string (t));
        check (widths[static_cast<std::size_t> (t)] > 1e-4, "Multi-FX type produces stereo width: " + std::to_string (t));
    }
    check (std::abs (widths[0] - widths[2]) > 1e-3, "SuperSlow and SuperFast have distinct stereo signatures");
    check (std::abs (widths[4] - widths[7]) > 1e-3, "Flanger and Phasing presets differ");

    // JP-8080 Multi-FX mode 12 (Distortion): dry passthrough at level 0, monotonic THD across levels,
    // even + odd asymmetry harmonics, independent L/R channels, zero input -> zero output, and block parity.
    {
        Chorus cDry;
        cDry.reset();
        cDry.configure (ChorusType::Distortion, 0);
        cDry.snap();
        for (int i = 0; i < 64; ++i)
        {
            const StereoQ23 in { toQ23 ((i - 32) * 0.002), toQ23 ((32 - i) * 0.0015) };
            const StereoQ23 got = cDry.processMixQ23 (in);
            check (got.l == sat24 (mul8 (in.l, 116, 7)) && got.r == sat24 (mul8 (in.r, 116, 7)),
                   "Distortion level 0 is bit-identical dry");
        }
    }

    auto measureDistortion = [] (int level, double& thd, double& h2Ratio, double& h3Ratio, double& maxAbs)
    {
        Chorus c;
        c.reset();
        c.configure (ChorusType::Distortion, level);
        c.snap();
        std::vector<double> tail (22050);
        maxAbs = 0.0;
        for (int i = 0; i < 44100; ++i)
        {
            const double dry = (0.25 / 12.0) * std::sin (kTwoPi * 220.0 * i / fs);
            const Stereo y = c.processMix ({ dry, dry });
            check (y.l == y.r, "Distortion keeps L == R identical for identical channels");
            maxAbs = std::max (maxAbs, std::abs (y.l));
            if (i >= 22050)
                tail[static_cast<std::size_t> (i - 22050)] = y.l;
        }
        const double h1 = std::max (1e-12, tone (tail, 220.0));
        const double h2 = tone (tail, 440.0);
        const double h3 = tone (tail, 660.0);
        const double h4 = tone (tail, 880.0);
        const double h5 = tone (tail, 1100.0);
        thd = std::sqrt (h2 * h2 + h3 * h3 + h4 * h4 + h5 * h5) / h1;
        h2Ratio = h2 / h1;
        h3Ratio = h3 / h1;
    };

    double thd24 = 0.0, h2_24 = 0.0, h3_24 = 0.0, max24 = 0.0;
    double thd64 = 0.0, h2_64 = 0.0, h3_64 = 0.0, max64 = 0.0;
    double thd127 = 0.0, h2_127 = 0.0, h3_127 = 0.0, max127 = 0.0;
    measureDistortion (24, thd24, h2_24, h3_24, max24);
    measureDistortion (64, thd64, h2_64, h3_64, max64);
    measureDistortion (127, thd127, h2_127, h3_127, max127);
    check (max24 > 0.005 && max24 < 0.25 && max64 < 0.25 && max127 < 0.25,
           "Distortion output stays cleanly bounded at internal bus scale");
    check (h2_64 > 1e-3 && h3_64 > 1e-3 && h2_127 > 1e-3 && h3_127 > 1e-3,
           "Distortion generates both 2nd (asymmetric) and 3rd harmonics");
    check (thd24 > 1e-4 && thd64 > thd24 && thd127 > thd64,
           "Distortion THD increases monotonically with level 24 < 64 < 127");

    // Zero input -> exact zero output, L != R independence, and sample-vs-block parity.
    {
        Chorus cZero;
        cZero.reset();
        cZero.configure (ChorusType::Distortion, 127);
        cZero.snap();
        for (int i = 0; i < 64; ++i)
        {
            const Stereo y = cZero.processMix ({ 0.0, 0.0 });
            check (y.l == 0.0 && y.r == 0.0, "Distortion produces exact zero output for silence");
        }

        Chorus cA, cB;
        cA.reset();
        cB.reset();
        cA.configure (ChorusType::Distortion, 96);
        cB.configure (ChorusType::Distortion, 96);
        cA.snap();
        cB.snap();
        double diffRSq = 0.0;
        for (int i = 0; i < 128; ++i)
        {
            const double inL = (0.20 / 12.0) * std::sin (kTwoPi * 330.0 * i / fs);
            const Stereo yA = cA.processMix ({ inL, 0.0 });
            const Stereo yB = cB.processMix ({ inL, (0.30 / 12.0) * std::cos (kTwoPi * 110.0 * i / fs) });
            check (yA.l == yB.l, "Distortion left output is unaffected by right channel");
            diffRSq += (yA.r - yB.r) * (yA.r - yB.r);
        }
        check (std::sqrt (diffRSq / 128.0) > 1e-3, "Distortion right channel responds independently");

        Chorus cSingle, cBlock;
        cSingle.reset();
        cBlock.reset();
        const int blocks[] { 1, 7, 64, 513, 19, 128 };
        int sampleIdx = 0;
        for (int bIdx = 0; bIdx < 6; ++bIdx)
        {
            const int lvl = (bIdx == 0) ? 0 : (bIdx < 3 ? 64 : (bIdx < 5 ? 127 : 0));
            cSingle.configure (ChorusType::Distortion, lvl);
            cBlock.configure (ChorusType::Distortion, lvl);
            const int n = blocks[bIdx];
            std::vector<Q23> bL (static_cast<std::size_t> (n)), bR (static_cast<std::size_t> (n));
            for (int k = 0; k < n; ++k, ++sampleIdx)
            {
                bL[static_cast<std::size_t> (k)] = toQ23 ((0.22 / 12.0) * std::sin (kTwoPi * 180.0 * sampleIdx / fs));
                bR[static_cast<std::size_t> (k)] = toQ23 ((0.18 / 12.0) * std::cos (kTwoPi * 260.0 * sampleIdx / fs));
            }
            std::vector<Q23> sL = bL, sR = bR;
            cBlock.processBlockMixQ23 (bL.data(), bR.data(), n);
            for (int k = 0; k < n; ++k)
            {
                const StereoQ23 out = cSingle.processMixQ23 ({ sL[static_cast<std::size_t> (k)], sR[static_cast<std::size_t> (k)] });
                check (out.l == bL[static_cast<std::size_t> (k)] && out.r == bR[static_cast<std::size_t> (k)],
                       "Distortion single-sample and block processing are bit-exact");
            }
        }
    }
}

void envelopeSmoother()
{
    Envelope env;
    env.configure (0, 64, 0.5, 5);
    env.trigger();
    near (env.value(), 1.0, 1e-9, "attack 0 hits full on trigger");
    near (env.smootherAlpha(), 1.0 / 8.0, 1e-9, "attack 0 uses fast 1/8 initial smoother");
    for (int i = 0; i < kEnvelopeInterval - 1; ++i)
        near (env.process(), 1.0, 1e-9, "attack 0 holds 10 ms before decay ticks");
    check (env.process() < 0.99, "decay starts on the 100 Hz tick");
    near (env.smootherAlpha(), 1.0 / 2048.0, 1e-9, "decay >= 30 switches smoother to 1/2048");

    // Verify all fast attack speeds 0..9, intermediate speed 10, and slow speed 11.
    constexpr double fastAlphas[10] {
        2048.0 / 16384.0, 512.0 / 16384.0, 256.0 / 16384.0, 208.0 / 16384.0, 144.0 / 16384.0,
        128.0 / 16384.0,  96.0 / 16384.0,  80.0 / 16384.0,  74.0 / 16384.0,  55.0 / 16384.0
    };
    for (int atk = 0; atk < 10; ++atk)
    {
        env.reset();
        env.configure (atk, 64, 0.5, 64);
        env.trigger();
        near (env.value(), 1.0, 1e-9, "fast attack jumps to peak: " + std::to_string (atk));
        near (env.smootherAlpha(), fastAlphas[atk], 1e-12, "fast attack smoother level " + std::to_string (atk));
    }
    env.reset();
    env.configure (15, 64, 0.5, 64);
    env.trigger();
    near (env.smootherAlpha(), 1.0 / 512.0, 1e-12, "attack 10..29 uses speed 10 (1/512)");
    env.reset();
    env.configure (32, 64, 0.5, 64);
    env.trigger();
    near (env.smootherAlpha(), 1.0 / 2048.0, 1e-12, "attack >= 30 uses speed 11 (1/2048)");
    env.reset();
    env.trigger (true, false, true);
    near (env.smootherAlpha(), 1.0 / 512.0, 1e-12, "active LFO upgrades speed 11 to 1/512");
    env.reset();
    env.trigger (true, true, true);
    near (env.smootherAlpha(), 1.0 / 64.0, 1e-12, "LFO edge upgrades attack smoother to 1/64");
    env.tick100Hz (true, false, true);
    near (env.smootherAlpha(), 1.0 / 64.0, 1e-12, "attack holds LFO edge speed on tick +1");
    env.tick100Hz (true, false, true);
    near (env.smootherAlpha(), 1.0 / 64.0, 1e-12, "attack holds LFO edge speed on tick +2");
    env.tick100Hz (true, false, true);
    near (env.smootherAlpha(), 1.0 / 512.0, 1e-12, "attack returns to LFO speed after two hold ticks");

    env.reset();
    env.configure (0, 64, 0.5, 64);
    env.trigger (false, false, false);
    near (env.smootherAlpha(), 1.0 / 2048.0, 1e-12, "ignoring env speed keeps 1/2048 baseline");
    env.reset();
    env.trigger (true, false, false);
    near (env.smootherAlpha(), 1.0 / 512.0, 1e-12, "ignoring env speed still upgrades on active LFO");

    // Fast release steps straight to idle through speed 5 (1/128), then settles to 1/2048.
    env.reset();
    env.configure (0, 0, 1.0, 5);
    env.trigger();
    for (int i = 0; i < 2000; ++i)
        env.process();
    env.release();
    for (int i = 0; i < 8820 && ! env.idle(); ++i)
        env.process();
    check (env.idle(), "short release reaches idle within 100 ms");
    near (env.smootherAlpha(), 1.0 / 128.0, 1e-12, "release 5 enters idle at speed 5 (1/128)");
    for (int i = 0; i < kEnvelopeInterval; ++i)
        env.process();
    near (env.smootherAlpha(), 1.0 / 2048.0, 1e-12, "idle tail settles to 1/2048 on next tick");
}

void toneSaturation()
{
    ToneControl lin, satTone;
    lin.configure (63, 0);
    satTone.configure (63, 0);
    std::vector<double> x (88200);
    for (int i = 0; i < 88200; ++i)
    {
        const double in = 0.25 * std::sin (kTwoPi * 80.0 * i / fs);
        x[static_cast<std::size_t> (i)] = satTone.processSaturating (in);
    }
    x.erase (x.begin(), x.begin() + 44100);
    check (tone (x, 240.0) > 0.01, "hot bass boost drives saturating ToneControl into upper odd harmonics");
}

void osc2AndSync()
{
    const double inc1 = 2.0 * 110.0 / fs;
    const double inc2 = 2.0 * (110.0 * 2.6697) / fs;
    std::vector<double> o1Saw (44100), o2Saw (44100), o1Tri (44100), o2Tri (44100);
    std::vector<double> sSaw (44100), sTri (44100), sPulse (44100);
    double p1 = -1.0, p2 = -1.0;
    for (int i = 0; i < 44100; ++i)
    {
        p1 = wrap2 (p1 + inc1);
        p2 = wrap2 (p2 + inc2);
        o1Saw[static_cast<std::size_t> (i)] = osc::shapedSawHardware (p1, inc1, 0.25);
        o2Saw[static_cast<std::size_t> (i)] = osc::osc2SawHardware (p1, inc1);
        o1Tri[static_cast<std::size_t> (i)] = osc::shapedTriangleHardware (p1, 0.0);
        o2Tri[static_cast<std::size_t> (i)] = osc::osc2TriangleHardware (p1);
        sSaw[static_cast<std::size_t> (i)] = osc::syncSawHardware (p1, inc1, inc2, 2.6697);
        sTri[static_cast<std::size_t> (i)] = osc::syncTriangleHardware (p1, inc1, 2.6697);
        sPulse[static_cast<std::size_t> (i)] = osc::syncPulseHardware (p1, inc1, inc2, 2.6697, 0.0);
    }
    near (db (tone (o2Saw, 110.0) / tone (o1Saw, 110.0)), 2.9, 0.3, "OSC 2 Saw is +2.9 dB over OSC 1 plain Saw");
    near (db (tone (o2Tri, 110.0) / tone (o1Tri, 110.0)), 0.22, 0.3, "OSC 2 Triangle is +0.22 dB over OSC 1 Triangle");
    check (tone (sSaw, 110.0) > 1e-3 && tone (sSaw, 330.0) > 1e-3, "Sync Saw tracks master fundamental and overtones");
    check (tone (sTri, 110.0) > 1e-3 && tone (sPulse, 110.0) > 1e-3, "Sync Tri and Pulse track master pitch");
}

void controlAccuracy()
{
    // Exact portamento timing law and interval invariance.
    near (mapping::portamentoSeconds (0), 0.0, 1e-12, "portamento 0 is instant");
    near (mapping::portamentoSeconds (16), 8192.0 / (3314.0 * 100.0), 1e-9, "portamento 16");
    near (mapping::portamentoSeconds (64), 0.320, 1e-9, "portamento 64");
    near (mapping::portamentoSeconds (96), 8192.0 / (72.0 * 100.0), 1e-9, "portamento 96");

    const auto glideEndCents = [] (int targetNote)
    {
        Patch p;
        p.osc1.wave = Wave1::Triangle;
        p.balance = -64;
        p.mono = p.portamento = true;
        p.portamentoTime = 64;
        p.amp.sustain = 127;
        Engine e;
        e.prepare (fs);
        e.setPatch (Part::Upper, p);
        std::vector<float> l (static_cast<std::size_t> (0.6 * fs)), r (l.size());
        e.noteOn (48, 100);
        e.render (l.data(), r.data(), 8820);
        e.noteOn (targetNote, 100);
        e.render (l.data(), r.data(), static_cast<int> (l.size()));
        const std::vector<double> x (l.begin(), l.end());
        const double hz = zeroCrossingHz (x, static_cast<std::size_t> (0.34 * fs), static_cast<std::size_t> (0.55 * fs), fs);
        const double expectedHz = 440.0 * std::exp2 ((targetNote - 69) / 12.0);
        return 1200.0 * std::log2 (hz / expectedHz);
    };
    near (glideEndCents (60), 0.0, 3.0, "12-semitone glide finishes within constant time");
    near (glideEndCents (72), 0.0, 3.0, "24-semitone glide finishes within same constant time");

    // Monotonic LFO fade curve.
    near (mapping::fadeLevelFromPhase (0), 0.0, 1e-12, "fade curve starts at 0");
    near (mapping::fadeLevelFromPhase (8192), 2429.0 / 16384.0, 1e-12, "fade curve midpoint");
    near (mapping::fadeLevelFromPhase (16128), 1.0, 1e-12, "fade curve reaches 1 at step 63");
    near (mapping::fadeLevelFromPhase (16384), 1.0, 1e-12, "fade curve clamps at 1");
    bool fadeMono = true;
    for (int ph = 1; ph <= 16384; ph += 128)
        fadeMono = fadeMono && (mapping::fadeLevelFromPhase (ph) >= mapping::fadeLevelFromPhase (ph - 1));
    check (fadeMono, "fade curve is monotonically non-decreasing");

    // Sample & Hold updates twice per LFO 1 cycle (~10 steps/s near 5 Hz).
    {
        Patch p;
        p.osc1.wave = Wave1::Triangle;
        p.balance = -64;
        p.lfo1.wave = LfoWave::SampleHold;
        p.lfo1.rate = 77;
        p.lfo1.fade = 0;
        p.pitchLfo = 32;
        p.amp.sustain = 127;
        Engine e;
        e.prepare (fs);
        e.setPatch (Part::Upper, p);
        std::vector<float> l (static_cast<std::size_t> (fs)), r (l.size());
        e.noteOn (60, 100);
        e.render (l.data(), r.data(), static_cast<int> (l.size()));
        const std::vector<double> x (l.begin(), l.end());
        int changes = 0;
        double prevHz = 0.0;
        for (int k = 1; k < 19; ++k)
        {
            const std::size_t a = static_cast<std::size_t> ((0.05 * k + 0.005) * fs);
            const std::size_t b = static_cast<std::size_t> ((0.05 * k + 0.045) * fs);
            const double hz = zeroCrossingHz (x, a, b, fs);
            if (prevHz > 0.0 && std::abs (hz - prevHz) > 4.0)
                ++changes;
            prevHz = hz;
        }
        check (changes >= 6, "SampleHold updates at twice LFO rate");
    }

    // Bipolar OSC 1 Square PWM swings duty down to symmetric 50% on negative LFO half-cycle.
    {
        Patch p;
        p.osc1 = { Wave1::Square, 64, 64 };
        p.balance = -64;
        p.filter.cutoff = 127;
        p.lfo1.wave = LfoWave::Square;
        p.lfo1.rate = 48;
        p.lfo1.fade = 0;
        p.amp.sustain = 127;
        Engine e;
        e.prepare (fs);
        e.setPatch (Part::Upper, p);
        std::vector<float> l (static_cast<std::size_t> (fs)), r (l.size());
        e.noteOn (60, 100);
        e.render (l.data(), r.data(), static_cast<int> (l.size()));
        const double f0 = 261.6256;
        std::vector<double> firstHalf (l.begin() + static_cast<long> (0.25 * fs),
                                       l.begin() + static_cast<long> (0.40 * fs));
        std::vector<double> secondHalf (l.begin() + static_cast<long> (0.70 * fs),
                                        l.begin() + static_cast<long> (0.85 * fs));
        const double r1 = tone (firstHalf, 2.0 * f0) / tone (firstHalf, f0);
        const double r2 = tone (secondHalf, 2.0 * f0) / tone (secondHalf, f0);
        check (std::max (r1, r2) > 0.45 && std::min (r1, r2) < 0.05,
               "bipolar Square PWM visits both extreme duty and symmetric 50%");
    }

    // Filter smoother upgrades from 1/2048 to 1/512 when LFO 2 depth is non-zero (even at modWheel = 0).
    {
        const auto decayEnergy = [] (int lfo2FilterDepth)
        {
            Patch p;
            p.osc1 = { Wave1::Saw, 32, 0 };
            p.balance = -64;
            p.filter.cutoff = 59;
            p.filter.env = { 0, 30, 0, 10 };
            p.filter.envDepth = 40;
            p.lfo2.filter = lfo2FilterDepth;
            p.amp = { 0, 64, 127, 10 };
            Engine e;
            e.prepare (fs);
            e.setPatch (Part::Upper, p);
            std::vector<float> l (static_cast<std::size_t> (0.06 * fs)), r (l.size());
            e.noteOn (60, 64);
            e.render (l.data(), r.data(), static_cast<int> (l.size()));
            double sumSq = 0.0;
            for (std::size_t i = static_cast<std::size_t> (0.015 * fs); i < static_cast<std::size_t> (0.045 * fs); ++i)
            {
                const double hp = l[i] - l[i - 1];
                sumSq += hp * hp;
            }
            return sumSq;
        };
        check (decayEnergy (19) < decayEnergy (0) * 0.9,
               "filter LFO assignment speeds up filter smoother even with modWheel at 0");
    }

    // Dual mode sums Upper and Lower parts cleanly before bus saturation.
    {
        Patch p;
        p.osc1 = { Wave1::Saw, 32, 0 };
        p.balance = -64;
        Engine e;
        e.prepare (fs);
        e.setMode (KeyMode::Dual);
        e.setPatch (Part::Upper, p);
        e.setPatch (Part::Lower, p);
        e.noteOn (60, 100);
        std::vector<float> l (4096), r (4096);
        e.render (l.data(), r.data(), 4096);
        float peak = 0.0f;
        for (std::size_t i = 0; i < l.size(); ++i)
            peak = std::max ({ peak, std::abs (l[i]), std::abs (r[i]) });
        check (peak > 0.01f && peak < 1.5f, "dual mode sums cleanly and stays bounded");
    }
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

// AnalogColor: transparent at normal levels, exact silence, the digital node
// untouched, peaks near the rail rounded off, and deterministic.
void analogColor()
{
    const auto run = [] (bool on, OutputNode node, double amp, double hz, std::vector<double>* right = nullptr)
    {
        AnalogOutput a;
        AnalogColor c;
        c.on = on;
        a.setColor (c);
        a.prepare (node);
        std::vector<double> x (44100);
        for (int i = -176400; i < 44100; ++i)
        {
            const Stereo y = a.process ({ amp * std::sin (kTwoPi * hz * i / fs), amp * std::sin (kTwoPi * hz * i / fs) });
            if (i >= 0)
            {
                x[static_cast<std::size_t> (i)] = y.l;
                if (right)
                    (*right).push_back (y.r);
            }
        }
        return x;
    };
    const auto thd = [] (const std::vector<double>& x, double hz)
    {
        double h = 0.0;
        for (int k = 2; k <= 7; ++k)
            h += std::pow (tone (x, hz * k), 2.0);
        return std::sqrt (h) / tone (x, hz);
    };
    for (const double hz : { 50.0, 1000.0, 10000.0 })
        near (db (tone (run (true, OutputNode::Rack, 0.25, hz), hz) / tone (run (false, OutputNode::Rack, 0.25, hz), hz)), 0.0, 0.01,
              "colour is transparent at a quarter of full scale, " + std::to_string (hz));
    check (thd (run (true, OutputNode::Rack, 0.25, 1000.0), 1000.0) < 1.0e-4, "no distortion at a quarter of full scale");

    const auto hot = run (true, OutputNode::Rack, 0.99, 50.0), clean = run (false, OutputNode::Rack, 0.99, 50.0);
    check (thd (hot, 50.0) > 1.0e-3 && thd (clean, 50.0) < 1.0e-4, "a full-scale bass note rounds off near the rail");
    double peakHot = 0.0, peakClean = 0.0;
    for (std::size_t i = 0; i < hot.size(); ++i)
    {
        peakHot = std::max (peakHot, std::abs (hot[i]));
        peakClean = std::max (peakClean, std::abs (clean[i]));
    }
    check (peakHot < peakClean && peakHot < 1.35, "the rounded peak stays under the rail");

    const auto silent = run (true, OutputNode::Rack, 0.0, 1000.0);
    check (std::all_of (silent.begin(), silent.end(), [] (double v) { return v == 0.0; }), "silence stays exactly silent");
    // A tail under one DAC step (here -1/3 step, which a floor would hold at
    // -1) is the DSP's settled zero: no step held through the coupling caps.
    {
        AnalogOutput a;
        a.prepare (OutputNode::Rack);
        bool zero = true;
        for (int i = 0; i < 88200; ++i)
        {
            const double w = (i % 2 ? -1.0 : 1.0) / 393216.0;
            const Stereo y = a.process ({ w, -w });
            zero = zero && y.l == 0.0 && y.r == 0.0;
        }
        check (zero, "a tail under one DAC step is silent");
    }
    const auto digitalOn = run (true, OutputNode::Digital, 0.3, 1000.0), digitalOff = run (false, OutputNode::Digital, 0.3, 1000.0);
    check (digitalOn == digitalOff, "the digital node ignores the colour");
    std::vector<double> r1, r2;
    const auto a1 = run (true, OutputNode::Rack, 0.7, 220.0, &r1), a2 = run (true, OutputNode::Rack, 0.7, 220.0, &r2);
    check (a1 == a2 && r1 == r2, "deterministic");

    Engine e;
    e.prepare (48000.0);
    std::vector<float> l (4096), r (4096);
    e.render (l.data(), r.data(), 4096);
    check (std::all_of (l.begin(), l.end(), [] (float v) { return v == 0.0f; }), "the engine is silent before a note with the colour on");
}

void fixedPointAndMatrix()
{
    check (packDelay16 (0) == 0u && unpackDelay16 (0u) == 0, "companded delay word preserves zero");
    const std::array<Q23, 11> boundaries {
        kQ23Min, -(1 << 21), -(1 << 19), -(1 << 17), -(1 << 14), 0,
        1 << 14, 1 << 17, 1 << 19, 1 << 21, kQ23Max
    };
    bool mono = true;
    for (std::size_t i = 1; i < boundaries.size(); ++i)
        mono = mono && unpackDelay16 (packDelay16 (boundaries[i - 1])) <= unpackDelay16 (packDelay16 (boundaries[i]));
    check (mono, "companded delay word is monotonic across exponent boundaries");

    double worstRelErr = 0.0;
    for (std::int32_t s = 1 << 16; s <= kQ23Max; s += 1 << 15)
    {
        const Q23 u = unpackDelay16 (packDelay16 (s));
        worstRelErr = std::max (worstRelErr, std::abs (static_cast<double> (u - s)) / static_cast<double> (s));
    }
    check (worstRelErr < 2.0e-4, "companded delay word maintains < 2e-4 relative accuracy across full scale");

    check (mapping::velocityMatrixFactor (0) == -128
           && mapping::velocityMatrixFactor (80) == 0
           && mapping::velocityMatrixFactor (100) == 54
           && mapping::velocityMatrixFactor (127) == 128,
           "velocity matrix table hits exact signed pivot points");
    {
        Patch base;
        base.velocityMatrix.on = true;
        base.velocityMatrix.filterDecay = 22;
        base.filter.env.decay = 0;
        const Patch v100 = withVelocity (base, 100.0 / 127.0);
        check (v100.filter.env.decay == 9, "velocity matrix uses floor arithmetic shift (22 * 54 >> 7 == 9)");

        base.filter.envDepth = 10;
        base.velocityMatrix.envDepth = 63;
        check (withVelocity (base, 0.0).filter.envDepth == 0, "positive bipolar depth clamps at zero");
        base.filter.envDepth = -10;
        check (withVelocity (base, 1.0).filter.envDepth == 0, "negative bipolar depth clamps at zero");

        base.pan = PanMode::Off;
        base.ampLfo = 10;
        base.velocityMatrix.ampLfo = 63;
        check (withVelocity (base, 0.0).ampLfo == 0, "amp LFO clamps at zero when pan is off");
        base.pan = PanMode::Manual;
        check (withVelocity (base, 0.0).ampLfo < 0, "manual pan LFO crosses zero across full range");
    }

    {
        NoiseOsc n1, n2;
        Random r1 (12345u), r2 (12345u);
        n1.reset (r1);
        n2.reset (r2);
        n1.configure (261.6256, 1.0, 0.0);
        n2.configure (261.6256, 1.0, 0.0);
        double sumSq = 0.0;
        bool deterministic = true;
        for (int i = 0; i < 44100; ++i)
        {
            const Q23 a = n1.processQ23 (r1), b = n2.processQ23 (r2);
            deterministic = deterministic && a == b;
            const double fa = fromQ23 (a);
            sumSq += fa * fa;
        }
        const double rmsOpen = std::sqrt (sumSq / 44100.0);
        check (deterministic, "chaotic noise oscillator is deterministic for a fixed seed");
        check (rmsOpen > 0.05 && rmsOpen < 0.25, "noise oscillator white-band RMS stays within expected level");

        const auto measureNoiseRes = [] (double res)
        {
            NoiseOsc n;
            Random r (777u);
            n.reset (r);
            n.configure (261.6256, 64.0 / 127.0, res);
            for (int i = 0; i < 22050; ++i)
                n.processQ23 (r);
            double e = 0.0;
            for (int i = 0; i < 88200; ++i)
            {
                const double y = fromQ23 (n.processQ23 (r));
                e += y * y;
            }
            return std::sqrt (e / 88200.0);
        };
        check (measureNoiseRes (1.0) > measureNoiseRes (101.0 / 127.0) * 1.2,
               "noise oscillator preserves full-resonance headroom at CTRL 2 = 127");
    }

    {
        const double f1k = 2.0 * std::sin (kPi * 1000.0 / fs);
        const auto gainQ23 = [f1k] (FilterType type, bool steep, double hz)
        {
            Filter f;
            f.configure (f1k, 0.0, type, steep);
            std::vector<double> x (44100);
            for (int i = -88200; i < 44100; ++i)
            {
                const Q23 y = f.processQ23 (toQ23 (0.25 * std::sin (kTwoPi * hz * i / fs)));
                if (i >= 0)
                    x[static_cast<std::size_t> (i)] = fromQ23 (y) * 4.0;
            }
            return tone (x, hz);
        };
        near (db (gainQ23 (FilterType::LowPass, true, 8000) / gainQ23 (FilterType::LowPass, true, 4000)),
              -24.5, 2.0, "Q23 LP24 slope at 0.25 amplitude");
        near (db (gainQ23 (FilterType::HighPass, true, 125) / gainQ23 (FilterType::HighPass, true, 250)),
              -24.0, 2.0, "Q23 HP24 slope at 0.25 amplitude");

        Filter fRing;
        fRing.configure (f1k, 1.0, FilterType::LowPass, true);
        fRing.processQ23 (toQ23 (0.5));
        for (int i = 0; i < 8820; ++i)
            fRing.processQ23 (0);
        std::vector<double> ring (8820);
        for (auto& s : ring)
            s = fromQ23 (fRing.processQ23 (0));
        const double ringHz = peakHz (ring, 950, 1050, 2);
        check (tone (ring, ringHz) > 1.0e-4, "Q23 filter rings past 100 cycles at full resonance");
        near (ringHz, 1000.0, 10.0, "Q23 filter ring tracks cutoff");
    }

    {
        AnalogOutput aZero, aDouble, aQ23;
        aZero.prepare (OutputNode::Rack);
        bool silent = true;
        for (int i = 0; i < 88200; ++i)
        {
            const Stereo y0 = aZero.processQ23 ({ 0, 0 });
            const Stereo ySub = aZero.processQ23 (i % 2 ? StereoQ23 { 63, -63 } : StereoQ23 { -63, 63 });
            silent = silent && y0.l == 0.0 && y0.r == 0.0 && ySub.l == 0.0 && ySub.r == 0.0;
        }
        check (silent, "Q23 DAC path is strictly silent on zero and sub-step words");

        aDouble.prepare (OutputNode::Rack);
        aQ23.prepare (OutputNode::Rack);
        std::vector<double> xD (44100), xQ (44100);
        for (int i = -176400; i < 44100; ++i)
        {
            const double sample = 0.25 * std::sin (kTwoPi * 1000.0 * i / fs);
            const Stereo yD = aDouble.process ({ sample, sample });
            const Stereo yQ = aQ23.processQ23 ({ toQ23 (sample), toQ23 (sample) });
            if (i >= 0)
            {
                xD[static_cast<std::size_t> (i)] = yD.l;
                xQ[static_cast<std::size_t> (i)] = yQ.l;
            }
        }
        near (db (tone (xQ, 1000.0) / tone (xD, 1000.0)), 0.0, 0.02,
              "Q23 DAC path matches double path at quarter scale");
    }

    {
        const double pw = mapping::pulseWidth (120.0 / 127.0);
        const double inc = 2.0 * 220.0 / fs;
        const Q23 pwQ = toQ23 (pw), incQ = toQ23 (inc);
        double p = -1.0;
        Q23 pQ = toQ23 (-1.0);
        double eH = 0.0, eQ = 0.0;
        for (int i = 0; i < 44100; ++i)
        {
            p = wrap2 (p + inc);
            pQ = wrap24 (pQ + incQ);
            const double h = osc::pulseHardware (p, inc, pw);
            const double q = fromQ23 (osc::pulseQ23 (pQ, incQ, pwQ));
            eH += h * h;
            eQ += q * q;
        }
        near (db (std::sqrt (eQ / 44100.0) / std::sqrt (eH / 44100.0)), 0.0, 0.05,
              "Q23 narrow pulse preserves unclipped swing before output scale");
    }

    {
        const Coeff15 negB = splitTable15 (-31743);
        const Coeff15 negT = splitTable15 (-30589);
        check (negB.hi == -124 && negB.lo == 0 && negT.hi == -120 && negT.lo == 65,
               "ToneControl negated 15-bit splits match signed coefficient words");

        Patch velBass;
        velBass.osc1 = { Wave1::Saw, 32, 0 };
        velBass.balance = -64;
        velBass.amp = { 0, 0, 127, 5 };
        velBass.velocityMatrix.on = true;
        velBass.velocityMatrix.bass = 63;
        const auto renderFundamental = [] (const Patch& pat, int vel)
        {
            Engine eng;
            eng.prepare (48000.0);
            eng.setPatch (Part::Upper, pat);
            eng.noteOn (45, vel);
            std::vector<float> l (14400), r (14400);
            eng.render (l.data(), r.data(), 14400);
            std::vector<double> x (l.begin() + 4800, l.end());
            return tone (x, 110.0, 48000.0);
        };
        check (db (renderFundamental (velBass, 127) / renderFundamental (velBass, 80)) > 6.0,
               "velocity matrix updates ToneControl bass gain on note-on");
    }

    {
        OscillatorPair osc;
        Random rng (42u);
        osc.prepare();
        osc.trigger (rng, Wave1::Saw, true);
        OscillatorPair::Controls c;
        c.wave1 = Wave1::Saw;
        c.hz1 = 110.0;
        c.ctrl1 = 32.0 / 127.0;
        c.gain1 = 1.0;
        c.gain2 = 0.0;
        osc.configure (c);
        double mean = 0.0;
        for (int i = 0; i < 88200; ++i)
        {
            const double y = osc.process (rng);
            if (i >= 44100)
                mean += y;
        }
        mean /= 44100.0;
        check (mean < 0.0 && mean > -0.002,
               "oscillator negative-MAC DC blocker settles to small negative equilibrium");

        Delay d;
        d.prepare();
        d.configure (DelayType::PanLR, 48, 110, 120);
        d.snap();
        Chorus ch;
        ch.reset();
        ch.configure (ChorusType::SuperSlow, 127);
        ch.snap();
        FeedbackOsc fb;
        fb.prepare();
        fb.configure (220.0, 0.5, 0.9);
        bool bounded = true;
        for (int i = 0; i < 22050; ++i)
        {
            const Q23 in = toQ23 (i == 0 ? 0.5 : 0.0);
            const StereoQ23 yd = d.processQ23 (in);
            const StereoQ23 yc = ch.processMixQ23 ({ in, in });
            const Q23 yf = fb.processQ23 (toQ23 (-0.5));
            bounded = bounded && std::abs (yd.l) <= kQ23One && std::abs (yd.r) <= kQ23One
                      && std::abs (yc.l) <= kQ23One && std::abs (yc.r) <= kQ23One
                      && std::abs (yf) <= kQ23One;
        }
        check (bounded, "Delay, Chorus, and FeedbackOsc negative-MAC DC blockers remain bounded");
    }

    {
        SuperSaw ssZero;
        ssZero.configure (98.8741659278, 0.0, 0.0);
        const int expectedZero[] { -6, -4, -1, 0, 1, 3, 5 };
        for (int i = 0; i < 7; ++i)
            check (ssZero.stepsFromCentre (i) == expectedZero[i],
                   "SuperSaw 15-bit detune step matches at detune 0 for saw " + std::to_string (i));

        SuperSaw ssFull;
        ssFull.configure (1000.0, 1.0, 1.0);
        check (-ssFull.stepsFromCentre (0) > ssFull.stepsFromCentre (6),
               "SuperSaw 15-bit detune preserves asymmetric outer spread");
    }

    {
        check (mapping::lfo1RateTable[127] == 32768u
               && mapping::levelCurveTable[64] == 8321u
               && mapping::levelCurveTable[127] == 32768u,
               "integer rate and square-law level tables match exact anchor points");
        Patch lfoPat;
        lfoPat.osc1 = { Wave1::Triangle, 0, 0 };
        lfoPat.balance = -64;
        lfoPat.lfo1 = { LfoWave::Saw, 64, 0 };
        lfoPat.pitchLfo = 32;
        lfoPat.amp.sustain = 127;
        Engine e1, e2;
        e1.prepare (48000.0);
        e2.prepare (48000.0);
        e1.setPatch (Part::Upper, lfoPat);
        e2.setPatch (Part::Upper, lfoPat);
        e1.noteOn (60, 100);
        e2.noteOn (60, 100);
        std::vector<float> l1 (48000), r1 (48000), l2 (48000), r2 (48000);
        e1.render (l1.data(), r1.data(), 48000);
        e2.render (l2.data(), r2.data(), 48000);
        check (l1 == l2 && r1 == r2, "16-bit integer LFO phase accumulator is deterministic");
    }

    {
        AnalogOutput stereoDac;
        stereoDac.prepare (OutputNode::Rack);
        double maxDiff = 0.0;
        bool silentZero = true;
        for (int i = 0; i < 8192; ++i)
        {
            const Stereo z = stereoDac.processQ23 (i % 2 ? StereoQ23 { 63, -63 } : StereoQ23 { -63, 63 });
            silentZero = silentZero && z.l == 0.0 && z.r == 0.0;
        }
        stereoDac.reset();
        for (int i = 0; i < 8192; ++i)
        {
            const Q23 s = toQ23 (0.35 * std::sin (kTwoPi * 331.0 * i / fs));
            const Stereo y = stereoDac.processQ23 ({ s, s });
            maxDiff = std::max (maxDiff, std::abs (y.l - y.r));
        }
        check (silentZero && maxDiff > 0.0 && maxDiff < 2.0 / 131072.0,
               "stereo DAC R-2R ladders stay zero on sub-step words and differ slightly across channels");
    }

    {
        // Bit-exact checks for branchless companding, fast windowing, and SIMD voice/filter blocks.
        const auto refPack = [] (Q23 v) noexcept -> std::uint16_t
        {
            v = sat24 (v);
            for (unsigned e = 0; e < 3; ++e)
            {
                const std::int32_t m = v >> (4 + 2 * static_cast<int> (e));
                if (m >= -8192 && m <= 8191)
                    return static_cast<std::uint16_t> ((static_cast<std::uint32_t> (m) & 0x3FFFu) | (e << 14));
            }
            const std::int32_t m = std::clamp (v >> 10, -8192, 8191);
            return static_cast<std::uint16_t> ((static_cast<std::uint32_t> (m) & 0x3FFFu) | (3u << 14));
        };
        bool packMatch = true;
        const std::array<Q23, 7> edgeCenters { kQ23Min, -2097152, -524288, -131072, 131072, 524288, 2097152 };
        for (Q23 c : edgeCenters)
            for (int d = -256; d <= 256; ++d)
                packMatch = packMatch && packDelay16 (sat24 (c + d)) == refPack (sat24 (c + d));
        for (Q23 v = kQ23Min; v <= kQ23Max - 67; v += 67)
            packMatch = packMatch && packDelay16 (v) == refPack (v);
        check (packMatch, "branchless packDelay16 is bit-exact across 24-bit domain");

        bool unpackMatch = true;
        for (std::uint32_t code = 0; code <= 0xFFFFu; ++code)
        {
            const std::int32_t raw = static_cast<std::int32_t> (code & 0x3FFFu);
            const std::int32_t mantissa = (raw & 0x2000) != 0 ? (raw - 0x4000) : raw;
            const int shift = 4 + 2 * static_cast<int> (code >> 14);
            const Q23 refUnpack = static_cast<Q23> (static_cast<std::int64_t> (mantissa) * (1 << shift));
            if (unpackDelay16 (static_cast<std::uint16_t> (code)) != refUnpack)
            {
                unpackMatch = false;
                break;
            }
        }
        check (unpackMatch, "branchless unpackDelay16 is bit-exact across all 65536 codes");

        bool sawMatch = true;
        for (double hz : { 55.0, 440.0, 3520.0, 12000.0 })
        {
            const Q23 inc = toQ23 (2.0 * hz / fs);
            const osc::SawWindowCoeffs w = osc::makeSawWindowCoeffs (inc, 4);
            for (Q23 p : { kQ23Min, kQ23Min + 4096, -1000, -1, 0, 1, 1000, kQ23Max - 4096, kQ23Max })
                sawMatch = sawMatch && osc::osc2SawFastQ23 (p, w) == osc::osc2SawQ23 (p, inc);
            Q23 phase = toQ23 (-1.0);
            for (int i = 0; i < 512; ++i)
            {
                phase = wrap24 (phase + inc);
                sawMatch = sawMatch && osc::osc2SawFastQ23 (phase, w) == osc::osc2SawQ23 (phase, inc);
            }
        }
        check (sawMatch, "osc2SawFastQ23 is bit-exact with full window path");

        SuperSaw ssScalar, ssBlock;
        Random rA (91u), rB (91u);
        ssScalar.trigger (rA);
        ssBlock.trigger (rB);
        ssScalar.configure (220.0, 0.4, 0.7);
        ssBlock.configure (220.0, 0.4, 0.7);
        std::array<Q23, 64> blk {};
        bool ssMatch = true;
        for (int b = 0; b < 32; ++b)
        {
            if (b == 8)
            {
                ssScalar.configure (330.0, 0.9, 0.3);
                ssBlock.configure (330.0, 0.9, 0.3);
            }
            ssBlock.processBlockQ23 (blk.data(), 64);
            for (int i = 0; i < 64; ++i)
                ssMatch = ssMatch && blk[static_cast<std::size_t> (i)] == ssScalar.processQ23();
        }
        check (ssMatch, "Int32x4 SuperSaw block matches scalar processQ23 bit-for-bit");

        {
            SuperSaw sScalar[4], sSimd[4];
            Q23 ssBufs[4][64];
            SuperSaw* const sPtrs[4] { &sSimd[0], &sSimd[1], &sSimd[2], &sSimd[3] };
            Q23* const ssOutPtrs[4] { ssBufs[0], ssBufs[1], ssBufs[2], ssBufs[3] };
            for (int k = 0; k < 4; ++k)
            {
                Random rkA (300u + static_cast<std::uint32_t> (k)), rkB (300u + static_cast<std::uint32_t> (k));
                sScalar[k].trigger (rkA);
                sSimd[k].trigger (rkB);
                sScalar[k].configure (110.0 * (1.0 + 0.25 * k), 0.35 + 0.1 * k, 0.65);
                sSimd[k].configure (110.0 * (1.0 + 0.25 * k), 0.35 + 0.1 * k, 0.65);
            }
            bool ss4Match = true;
            for (int blkIdx = 0; blkIdx < 4; ++blkIdx)
            {
                SuperSaw::process4BlockQ23 (sPtrs, ssOutPtrs, 64);
                for (int k = 0; k < 4; ++k)
                    for (int i = 0; i < 64; ++i)
                        ss4Match = ss4Match && ssBufs[k][i] == sScalar[k].processQ23();
            }
            check (ss4Match, "Int32x4 4-voice SuperSaw matches scalar processQ23 bit-for-bit");
        }

        {
            auto checkOp4Case = [&] (const char* label, const std::array<double, 4>& gains, const std::array<Wave2, 4>& w2s)
            {
                OscillatorPair oScalar[4], oSimd[4];
                Q23 opBufs[4][64];
                OscillatorPair* const opPtrs[4] { &oSimd[0], &oSimd[1], &oSimd[2], &oSimd[3] };
                Q23* const opOutPtrs[4] { opBufs[0], opBufs[1], opBufs[2], opBufs[3] };
                Random rScal (700u), rVec (700u);
                OscillatorPair::Controls ctrls[4];
                for (int k = 0; k < 4; ++k)
                {
                    oScalar[k].prepare();
                    oSimd[k].prepare();
                    Random rsA (701u + static_cast<std::uint32_t> (k)), rsB (701u + static_cast<std::uint32_t> (k));
                    oScalar[k].trigger (rsA, Wave1::SuperSaw, true);
                    oSimd[k].trigger (rsB, Wave1::SuperSaw, true);
                    ctrls[k] = {
                        Wave1::SuperSaw, w2s[static_cast<std::size_t> (k)],
                        110.0 * (1.0 + 0.3 * k), 220.0 * (1.0 + 0.3 * k),
                        0.5, 0.75, 0.35, 0.0,
                        1.0 - gains[static_cast<std::size_t> (k)], gains[static_cast<std::size_t> (k)],
                        false, false
                    };
                    oScalar[k].configure (ctrls[k]);
                    oSimd[k].configure (ctrls[k]);
                }
                bool match = OscillatorPair::canProcess4SuperSaw (opPtrs);
                for (int blkIdx = 0; blkIdx < 4; ++blkIdx)
                {
                    OscillatorPair::process4SuperSawBlockQ23 (opPtrs, opOutPtrs, 64);
                    for (int k = 0; k < 4; ++k)
                        for (int i = 0; i < 64; ++i)
                            match = match && opBufs[k][i] == oScalar[k].processQ23 (rScal);
                }
                for (int k = 0; k < 4; ++k)
                {
                    ctrls[k].wave1 = Wave1::Saw;
                    ctrls[k].xmod = 0.35;
                    oScalar[k].configure (ctrls[k]);
                    oSimd[k].configure (ctrls[k]);
                    for (int s = 0; s < 8; ++s)
                        match = match && oSimd[k].processQ23 (rVec) == oScalar[k].processQ23 (rScal);
                }
                check (match, label);
            };
            checkOp4Case ("4-voice SuperSaw OscPair (osc1-only) matches scalar bit-for-bit",
                          { 0.0, 0.0, 0.0, 0.0 },
                          { Wave2::Saw, Wave2::Saw, Wave2::Saw, Wave2::Saw });
            checkOp4Case ("4-voice SuperSaw OscPair (Wave2 Saw) matches scalar bit-for-bit",
                          { 0.4, 0.5, 0.6, 0.7 },
                          { Wave2::Saw, Wave2::Saw, Wave2::Saw, Wave2::Saw });
            checkOp4Case ("4-voice SuperSaw OscPair (mixed Wave2) matches scalar bit-for-bit",
                          { 0.0, 0.5, 0.5, 0.5 },
                          { Wave2::Saw, Wave2::Saw, Wave2::Square, Wave2::Triangle });
        }

        for (FilterType t : { FilterType::LowPass, FilterType::BandPass, FilterType::HighPass })
        {
            for (bool steep : { false, true })
            {
                Filter fScalar[4], fSimd[4];
                Q23 bufs[4][64];
                Filter* const fPtrs[4] { &fSimd[0], &fSimd[1], &fSimd[2], &fSimd[3] };
                Q23* const bPtrs[4] { bufs[0], bufs[1], bufs[2], bufs[3] };
                for (int k = 0; k < 4; ++k)
                {
                    fScalar[k].configure (0.04 + 0.03 * k, 0.2 * k, t, steep);
                    fSimd[k].configure (0.04 + 0.03 * k, 0.2 * k, t, steep);
                    fScalar[k].configure (0.15 + 0.02 * k, 0.1 + 0.2 * k, t, steep);
                    fSimd[k].configure (0.15 + 0.02 * k, 0.1 + 0.2 * k, t, steep);
                }
                bool fltMatch = true;
                for (int ch = 0; ch < 8; ++ch)
                {
                    Q23 refOut[4][64];
                    for (int k = 0; k < 4; ++k)
                        for (int i = 0; i < 64; ++i)
                        {
                            const Q23 in = toQ23 (0.3 * std::sin (kTwoPi * (110.0 * (k + 1)) * (ch * 64 + i) / fs));
                            bufs[k][i] = in;
                            refOut[k][i] = fScalar[k].processQ23 (in);
                        }
                    Filter::process4BlockQ23 (fPtrs, bPtrs, 64, t, steep);
                    for (int k = 0; k < 4; ++k)
                        for (int i = 0; i < 64; ++i)
                            fltMatch = fltMatch && bufs[k][i] == refOut[k][i];
                }
                check (fltMatch, "Int32x4 4-voice Filter matches scalar processQ23 bit-for-bit");
            }
        }

        AnalogOutput aRef, aBlk;
        aRef.prepare (OutputNode::Rack);
        aBlk.prepare (OutputNode::Rack);
        std::array<StereoQ23, 64> words {};
        std::array<Stereo, 64> outBlk {};
        for (int i = 0; i < 64; ++i)
            words[static_cast<std::size_t> (i)] = { toQ23 (0.3 * std::sin (kTwoPi * 440.0 * i / fs)),
                                                    toQ23 (0.3 * std::cos (kTwoPi * 440.0 * i / fs)) };
        aBlk.processBlockQ23 (words.data(), outBlk.data(), 64);
        double maxErr = 0.0;
        for (int i = 0; i < 64; ++i)
        {
            const Stereo y = aRef.processQ23 (words[static_cast<std::size_t> (i)]);
            maxErr = std::max ({ maxErr, std::abs (outBlk[static_cast<std::size_t> (i)].l - y.l),
                                         std::abs (outBlk[static_cast<std::size_t> (i)].r - y.r) });
        }
        check (maxErr < 1.0e-13, "Double2 AnalogOutput block matches per-sample output");

        // Pre-trigger and post-release true-idle silence check.
        Engine idleEng;
        idleEng.prepare (48000.0, 555u);
        Patch shortPat;
        shortPat.osc1 = { Wave1::Triangle, 0, 0 };
        shortPat.balance = -64;
        shortPat.amp = { 0, 10, 0, 5 };
        shortPat.lfo1 = { LfoWave::Square, 64, 0 };
        idleEng.setPatch (Part::Upper, shortPat);
        std::vector<float> il (48000), ir (48000);
        idleEng.render (il.data(), ir.data(), 48000);
        check (std::all_of (il.begin(), il.end(), [] (float v) { return v == 0.0f; })
               && std::all_of (ir.begin(), ir.end(), [] (float v) { return v == 0.0f; }),
               "true-idle pre-trigger render emits exact zeros");
        idleEng.noteOn (60, 100);
        idleEng.allNotesOff();
        for (int b = 0; b < 18; ++b)
            idleEng.render (il.data(), ir.data(), 48000);
        check (std::all_of (il.end() - 4096, il.end(), [] (float v) { return v == 0.0f; }),
               "released voice decays into true-idle exact zero");
    }
}

void automationGlitch()
{
    auto maxSecondDiff = [] (const std::vector<float>& L, const std::vector<float>& R, int start, int end)
    {
        double m = 0.0;
        const int hi = std::min (static_cast<int> (L.size()), end);
        for (int pos = std::max (2, start); pos < hi; ++pos)
        {
            const double d2L = std::abs (static_cast<double> (L[static_cast<std::size_t> (pos)])
                                         - 2.0 * L[static_cast<std::size_t> (pos - 1)]
                                         + L[static_cast<std::size_t> (pos - 2)]);
            const double d2R = std::abs (static_cast<double> (R[static_cast<std::size_t> (pos)])
                                         - 2.0 * R[static_cast<std::size_t> (pos - 1)]
                                         + R[static_cast<std::size_t> (pos - 2)]);
            m = std::max ({ m, d2L, d2R });
        }
        return m;
    };

    Patch smooth;
    smooth.osc1 = { Wave1::Triangle, 64, 16 };
    smooth.osc2 = { Wave2::Square, false, 7, 10, 32, 16 };
    smooth.balance = 0;
    smooth.xmod = 20;
    smooth.filter = { FilterType::LowPass, true, 90, 48, 0, 20, 40, { 0, 0, 127, 40 } };
    smooth.amp = { 0, 0, 127, 40 };
    smooth.level = 100;
    smooth.ampLfo = 20;
    smooth.pan = PanMode::Off;
    smooth.lfo1 = { LfoWave::Triangle, 50, 0 };
    smooth.lfo2 = { 60, 10, 10, 10 };
    smooth.effects = { 0, 0, ChorusType::SuperSlow, 0, DelayType::PanLR, 40, 30, 0 };
    smooth.pitchLfo = 10;
    smooth.bendUp = 2;
    smooth.bendDown = 2;

    constexpr double hostFs = 48000.0;
    constexpr int pre = 8192;
    constexpr int total = 24576;

    auto verifyStep = [&] (const std::string& label, Patch startPatch, bool startColor, auto&& evtFn, int revAt = -1, std::function<void (Engine&)> revFn = {})
    {
        Engine eRef, eEvt;
        eRef.prepare (hostFs, 1234u);
        eEvt.prepare (hostFs, 1234u);
        eRef.setAnalogColor (startColor);
        eEvt.setAnalogColor (startColor);
        eRef.setPatch (Part::Upper, startPatch);
        eEvt.setPatch (Part::Upper, startPatch);
        eRef.noteOn (60, 110);
        eEvt.noteOn (60, 110);
        eRef.pitchBend (0.5);
        eEvt.pitchBend (0.5);
        eRef.modWheel (0.5);
        eEvt.modWheel (0.5);

        std::vector<float> rL (total), rR (total), eL (total), eR (total);
        const long allocBefore = allocations;
        countAllocations = true;
        for (int pos = 0; pos < total; pos += 64)
        {
            if (pos == pre)
                evtFn (eEvt);
            if (revAt > 0 && pos == pre + revAt)
                revFn (eEvt);
            eRef.render (rL.data() + pos, rR.data() + pos, 64);
            eEvt.render (eL.data() + pos, eR.data() + pos, 64);
        }
        countAllocations = false;
        check (allocations == allocBefore, label + ": zero allocations on callback");

        bool finite = true;
        float peak = 0.0f;
        for (int pos = 0; pos < total; ++pos)
        {
            const float vl = eL[static_cast<std::size_t> (pos)];
            const float vr = eR[static_cast<std::size_t> (pos)];
            finite = finite && std::isfinite (vl) && std::isfinite (vr);
            peak = std::max ({ peak, std::abs (vl), std::abs (vr) });
        }
        check (finite && peak < 3.5f, label + ": finite bounded output");

        const double preD2 = maxSecondDiff (rL, rR, pre - 2048, pre + 2048);
        const double transD2 = maxSecondDiff (eL, eR, pre, pre + 2048);
        const double postD2 = maxSecondDiff (eL, eR, pre + 4096, total);
        const double baselineD2 = std::max (preD2, postD2);
        const double limit = std::max (0.015, 2.2 * baselineD2);
        check (transD2 <= limit,
               label + ": glitch-free transition D2=" + std::to_string (transD2) + " limit=" + std::to_string (limit));

        int latency = -1;
        for (int pos = pre; pos < total; ++pos)
        {
            if (std::abs (eL[static_cast<std::size_t> (pos)] - rL[static_cast<std::size_t> (pos)]) > 1e-7f
                || std::abs (eR[static_cast<std::size_t> (pos)] - rR[static_cast<std::size_t> (pos)]) > 1e-7f)
            {
                latency = pos - pre;
                break;
            }
        }
        return latency;
    };

    // 1. All non-mono/non-unison canonical controls (both min->max and max->min).
    for (int ctrl = 0; ctrl < kControlCount; ++ctrl)
    {
        if (ctrl == kMono || ctrl == kUnison)
            continue;
        Patch startMin = smooth;
        if (ctrl == kChorusType || ctrl == kChorusLevel)
            startMin.effects.chorus = 80;
        if (ctrl == kDelayType || ctrl == kDelayTime || ctrl == kDelayFeedback || ctrl == kDelayLevel || ctrl == kDelaySync)
            startMin.effects.delay = 80;
        if (ctrl == kResonance)
        {
            startMin.filter.steep = false;
            startMin.filter.cutoff = 85;
        }
        if (ctrl == kUnisonDetune || ctrl == kUnisonSpread)
            startMin.unison = true;
        if (ctrl == kFilterVelocity || ctrl == kAftertouchCutoff || ctrl == kAftertouchLfo2)
            startMin.filter.cutoff = 64;
        if (ctrl == kArpMode || ctrl == kArpRange || ctrl == kArpRate || ctrl == kArpGate || ctrl == kArpHold)
            startMin.arp.on = true;
        applyControl (startMin, ctrl, kControls[static_cast<std::size_t> (ctrl)].min);
        if (ctrl == kArpMode || ctrl == kArpRange || ctrl == kArpRate || ctrl == kArpGate)
            startMin.arp.on = true;
        Patch startMax = startMin;
        applyControl (startMax, ctrl, kControls[static_cast<std::size_t> (ctrl)].max);

        const std::string id (kControls[static_cast<std::size_t> (ctrl)].id);
        auto drivePatch = [&] (Engine& e, const Patch& next)
        {
            if (ctrl == kAftertouchCutoff || ctrl == kAftertouchLfo2)
                e.aftertouch (1.0);
            e.setPatch (Part::Upper, next);
        };
        const int latUp = verifyStep (id + " min->max", startMin, true, [&] (Engine& e) { drivePatch (e, startMax); });
        verifyStep (id + " max->min", startMax, true, [&] (Engine& e) { drivePatch (e, startMin); });

        if (ctrl == kCutoff || ctrl == kTreble || ctrl == kLevel || ctrl == kOsc1Wave
            || ctrl == kFilterType || ctrl == kFilterSlope)
        {
            check (latUp >= 0 && latUp < 96, id + ": immediate pickup latency=" + std::to_string (latUp));
        }
    }

    // 2. Performance controls & DAC color with immediate pickup latency.
    const int latColorOff = verifyStep ("analog_color off", smooth, true, [] (Engine& e) { e.setAnalogColor (false); });
    const int latColorOn = verifyStep ("analog_color on", smooth, false, [] (Engine& e) { e.setAnalogColor (true); });
    const int latBend = verifyStep ("pitchBend", smooth, true, [] (Engine& e) { e.pitchBend (-1.0); });
    const int latMod = verifyStep ("modWheel", smooth, true, [] (Engine& e) { e.modWheel (0.0); });
    Patch atPatch = smooth;
    atPatch.aftertouchCutoff = 48;
    atPatch.aftertouchLfo2 = 96;
    const int latAftertouch = verifyStep ("aftertouch", atPatch, true, [] (Engine& e) { e.aftertouch (1.0); });
    const int latTune = verifyStep ("setTune", smooth, true, [] (Engine& e) { e.setTune (100.0); });
    check (latColorOff >= 0 && latColorOff < 96, "analog_color off immediate pickup");
    check (latColorOn >= 0 && latColorOn < 96, "analog_color on immediate pickup");
    check (latBend >= 0 && latBend < 96, "pitchBend immediate pickup");
    check (latMod >= 0 && latMod < 96, "modWheel immediate pickup");
    check (latAftertouch >= 0 && latAftertouch < 96, "aftertouch immediate pickup");
    check (latTune >= 0 && latTune < 96, "setTune immediate pickup");

    // 3. Fast mid-crossfade reversals.
    verifyStep ("filter_slope rev", smooth, true,
                [&] (Engine& e) { Patch alt = smooth; alt.filter.steep = false; e.setPatch (Part::Upper, alt); },
                64, [&] (Engine& e) { e.setPatch (Part::Upper, smooth); });
    verifyStep ("filter_type rev", smooth, true,
                [&] (Engine& e) { Patch alt = smooth; alt.filter.type = FilterType::HighPass; e.setPatch (Part::Upper, alt); },
                64, [&] (Engine& e) { e.setPatch (Part::Upper, smooth); });
    verifyStep ("osc1_wave rev", smooth, true,
                [&] (Engine& e) { Patch alt = smooth; alt.osc1.wave = Wave1::Square; e.setPatch (Part::Upper, alt); },
                64, [&] (Engine& e) { e.setPatch (Part::Upper, smooth); });
    verifyStep ("chorus_type rev", smooth, true,
                [&] (Engine& e) { Patch alt = smooth; alt.effects.chorus = 80; alt.effects.chorusType = ChorusType::FlangerFast; e.setPatch (Part::Upper, alt); },
                64, [&] (Engine& e) { Patch alt = smooth; alt.effects.chorus = 80; alt.effects.chorusType = ChorusType::SuperSlow; e.setPatch (Part::Upper, alt); });
    Patch distBase = smooth;
    distBase.effects.chorus = 80;
    distBase.effects.chorusType = ChorusType::Distortion;
    verifyStep ("chorus_distortion rev", distBase, true,
                [&] (Engine& e) { Patch alt = distBase; alt.effects.chorusType = ChorusType::Freeze2; e.setPatch (Part::Upper, alt); },
                64, [&] (Engine& e) { e.setPatch (Part::Upper, distBase); });
    Patch chorusBase = smooth;
    chorusBase.effects.chorus = 80;
    chorusBase.effects.chorusType = ChorusType::SuperSlow;
    verifyStep ("chorus_to_distortion rev", chorusBase, true,
                [&] (Engine& e) { Patch alt = chorusBase; alt.effects.chorusType = ChorusType::Distortion; e.setPatch (Part::Upper, alt); },
                64, [&] (Engine& e) { e.setPatch (Part::Upper, chorusBase); });
    verifyStep ("analog_color rev", smooth, true,
                [] (Engine& e) { e.setAnalogColor (false); },
                64, [] (Engine& e) { e.setAnalogColor (true); });

    // 4. Block-size independence (1 vs 64 vs 997) across simultaneous automation transitions.
    auto renderWithChunk = [&] (int chunkSize)
    {
        constexpr int len = 12000;
        Engine e;
        e.prepare (hostFs, 4242u);
        e.setAnalogColor (true);
        Patch p0 = smooth;
        p0.effects.chorus = 60;
        p0.effects.delay = 40;
        e.setPatch (Part::Upper, p0);
        e.noteOn (60, 100);
        std::vector<float> outL (len), outR (len);
        bool switched1 = false, switched2 = false;
        int pos = 0;
        while (pos < len)
        {
            if (! switched1 && pos >= 2000)
            {
                switched1 = true;
                Patch p1 = p0;
                p1.osc1.wave = Wave1::SuperSaw;
                p1.osc1.ctrl1 = 70;
                p1.filter.type = FilterType::BandPass;
                p1.filter.steep = false;
                p1.filter.resonance = 70;
                p1.effects.treble = 40;
                p1.effects.chorusType = ChorusType::JetPhasing;
                e.setPatch (Part::Upper, p1);
                e.setAnalogColor (false);
                e.pitchBend (0.5);
                e.modWheel (0.6);
                e.setTune (-30.0);
            }
            if (! switched2 && pos >= 6000)
            {
                switched2 = true;
                e.setPatch (Part::Upper, p0);
                e.setAnalogColor (true);
            }
            int nextEvent = ! switched1 ? 2000 : (! switched2 ? 6000 : len);
            int step = std::min ({ chunkSize, nextEvent - pos, len - pos });
            e.render (outL.data() + pos, outR.data() + pos, step);
            pos += step;
        }
        return std::pair<std::vector<float>, std::vector<float>> { std::move (outL), std::move (outR) };
    };
    const auto [l1, r1] = renderWithChunk (1);
    const auto [l64, r64] = renderWithChunk (64);
    const auto [l997, r997] = renderWithChunk (997);
    check (l1 == l64 && r1 == r64 && l1 == l997 && r1 == r997,
           "automation transitions are bit-identical across block sizes 1, 64, and 997");
}

void unisonAndArp()
{
    // 1. Unison live detune spreads 8 stacked voices smoothly without clicks or allocations.
    {
        Patch p;
        p.osc1 = { Wave1::Saw, 0, 0 };
        p.balance = -64;
        p.filter = { FilterType::LowPass, true, 110, 0, 0, 0, 0, { 0, 0, 127, 10 } };
        p.amp = { 0, 0, 127, 10 };
        p.mono = true;
        p.unison = true;
        p.unisonDetune = 0;

        Engine e0, eSpread;
        e0.prepare (48000.0, 101u);
        eSpread.prepare (48000.0, 101u);
        e0.setPatch (Part::Upper, p);
        eSpread.setPatch (Part::Upper, p);
        e0.noteOn (60, 100);
        eSpread.noteOn (60, 100);

        std::vector<float> l0 (24000), r0 (24000), l1 (24000), r1 (24000);
        const long allocBefore = allocations;
        countAllocations = true;
        e0.render (l0.data(), r0.data(), 12000);
        eSpread.render (l1.data(), r1.data(), 12000);
        const int stackedVoices = eSpread.activeVoices();

        Patch pSpread = p;
        pSpread.unisonDetune = 45;
        eSpread.setPatch (Part::Upper, pSpread);
        e0.render (l0.data() + 12000, r0.data() + 12000, 12000);
        eSpread.render (l1.data() + 12000, r1.data() + 12000, 12000);
        countAllocations = false;
        check (allocations == allocBefore, "live unison_detune update allocates zero heap memory");
        check (stackedVoices == 8, "unison engages all 8 voices");

        double diff = 0.0;
        for (int i = 13000; i < 24000; ++i)
            diff = std::max (diff, static_cast<double> (std::abs (l1[static_cast<std::size_t> (i)] - l0[static_cast<std::size_t> (i)])));
        check (diff > 0.02, "live unison_detune detunes held stacked voices");

        Patch legatoUni = pSpread;
        legatoUni.legato = true;
        legatoUni.portamento = true;
        legatoUni.portamentoTime = 20;
        eSpread.setPatch (Part::Upper, legatoUni);
        eSpread.noteOn (72, 100);
        eSpread.render (l1.data(), r1.data(), 480);
        check (eSpread.activeVoices() == 8, "unison legato/portamento keeps all 8 voices active");

        Patch fb = pSpread;
        fb.osc1.wave = Wave1::FeedbackOsc;
        Engine eFb;
        eFb.prepare (48000.0);
        eFb.setPatch (Part::Upper, fb);
        eFb.noteOn (60, 100);
        eFb.render (l0.data(), r0.data(), 480);
        check (eFb.activeVoices() == 1, "unison falls back to 1 voice on FeedbackOsc");
    }

    // 2. Arpeggiator modes, octave expansion, hold latch replacement, and sustain interaction.
    {
        auto collect = [] (ArpMode mode, int range)
        {
            Arpeggiator arp;
            arp.reset (77u);
            arp.setTempo (120.0);
            ArpParams ap { true, mode, range, 5, 50, false }; // 1/16 at 120 BPM = 11025 ticks
            arp.configure (ap, false);
            arp.noteOn (64, 100, false);
            arp.noteOn (60, 100, false);
            arp.noteOn (67, 100, false);
            std::vector<int> notes;
            for (int step = 0; step < 8; ++step)
            {
                auto ev = arp.advance (step == 0 ? 0 : 11025);
                if (ev.onNote >= 0)
                    notes.push_back (ev.onNote);
            }
            return notes;
        };

        check ((collect (ArpMode::Up, 1) == std::vector<int> { 60, 64, 67, 72, 76, 79, 60, 64 }), "arp Up 2-oct sequence");
        check ((collect (ArpMode::Down, 1) == std::vector<int> { 79, 76, 72, 67, 64, 60, 79, 76 }), "arp Down 2-oct sequence");
        check ((collect (ArpMode::UpDown, 1) == std::vector<int> { 60, 64, 67, 72, 76, 79, 76, 72 }), "arp Up/Down 2-oct sequence");
        check ((collect (ArpMode::AsPlayed, 1) == std::vector<int> { 64, 60, 67, 76, 72, 79, 64, 60 }), "arp AsPlayed 2-oct sequence");

        const auto rndNotes = collect (ArpMode::Random, 1);
        const std::vector<int> valid { 60, 64, 67, 72, 76, 79 };
        bool allValid = rndNotes.size() == 8;
        bool hasVariety = false;
        for (std::size_t i = 0; i < rndNotes.size(); ++i)
        {
            allValid = allValid && std::find (valid.begin(), valid.end(), rndNotes[i]) != valid.end();
            hasVariety = hasVariety || rndNotes[i] != rndNotes[0];
        }
        check (allValid && hasVariety, "arp Random sequence picks valid deterministic steps");
    }

    // 3. End-to-end Engine tempo sync, gate off, Hold replacement, Sustain+Arp, idle, and chunk independence.
    {
        Patch p;
        p.osc1 = { Wave1::Triangle, 0, 0 };
        p.balance = -64;
        p.amp = { 0, 0, 127, 0 };
        p.filter = { FilterType::LowPass, true, 127, 0, 0, 0, 0, { 0, 0, 127, 0 } };
        p.arp = { true, ArpMode::Up, 0, 3, 50, true }; // 1/8 note, 50% gate, Hold on

        Engine eng;
        eng.prepare (48000.0, 2026u);
        eng.setTempo (120.0); // 1/8 = 0.25 s = 12000 host frames at 48 kHz; gate 50% = 6000 frames
        eng.setPatch (Part::Upper, p);
        eng.noteOn (57, 100); // A3 220 Hz
        eng.noteOn (69, 100); // A4 440 Hz
        eng.noteOff (57);
        eng.noteOff (69);

        std::vector<float> l (24000), r (24000);
        const long allocBefore = allocations;
        countAllocations = true;
        eng.render (l.data(), r.data(), 24000);
        countAllocations = false;
        check (allocations == allocBefore, "arpeggiator engine render allocates zero memory");

        std::vector<double> step0 (l.begin() + 600, l.begin() + 5400);
        std::vector<double> step1 (l.begin() + 12600, l.begin() + 17400);
        near (peakHz (step0, 150.0, 600.0, 1.0, 48000.0), 220.0, 6.0, "arp step 0 plays held note 57 (220 Hz)");
        near (peakHz (step1, 150.0, 600.0, 1.0, 48000.0), 440.0, 8.0, "arp step 1 plays held note 69 (440 Hz) at 120 BPM");

        // Gate 50% drops voice during second half of step 0 (coupling cap DAC tail < 1e-3).
        double gapAc = 0.0;
        for (int i = 7200; i < 11400; ++i)
            gapAc = std::max (gapAc, static_cast<double> (std::abs (l[static_cast<std::size_t> (i)] - l[7199])));
        check (gapAc < 1e-4, "arp 50% gate closes note cleanly between steps");

        // Hold latches new chord cleanly when old keys are released, then drops when Hold goes false.
        eng.noteOn (64, 100);
        eng.noteOff (64);
        eng.render (l.data(), r.data(), 12000);
        std::vector<double> heldSolo (l.begin() + 600, l.begin() + 5400);
        near (peakHz (heldSolo, 200.0, 500.0, 1.0, 48000.0), 329.63, 8.0, "arp Hold replaces latched chord on new key");

        Patch offHold = p;
        offHold.arp.hold = false;
        eng.setPatch (Part::Upper, offHold);
        eng.render (l.data(), r.data(), 4800);
        check (eng.activeVoices() == 0, "turning arp Hold off releases latched chord with zero stuck voices");

        // Tempo change doubles speed (240 BPM = 0.125 s per 1/8 step = 6000 frames).
        Engine fast;
        fast.prepare (48000.0, 2026u);
        fast.setTempo (240.0);
        fast.setPatch (Part::Upper, p);
        fast.noteOn (57, 100);
        fast.noteOn (69, 100);
        std::vector<float> fl (12000), fr (12000);
        fast.render (fl.data(), fr.data(), 12000);
        std::vector<double> f1 (fl.begin() + 6300, fl.begin() + 8700);
        near (peakHz (f1, 150.0, 600.0, 1.0, 48000.0), 440.0, 12.0, "arp follows host tempo 240 BPM");

        // Sustain holds arp pool while each step still obeys gate-off.
        Engine sus;
        sus.prepare (48000.0, 2026u);
        sus.setTempo (120.0);
        Patch susPatch = p;
        susPatch.arp.hold = false;
        sus.setPatch (Part::Upper, susPatch);
        sus.sustain (true);
        sus.noteOn (60, 100);
        sus.noteOff (60);
        sus.render (fl.data(), fr.data(), 9600);
        double gateAc = 0.0;
        for (int i = 7200; i < 9600; ++i)
            gateAc = std::max (gateAc, static_cast<double> (std::abs (fl[static_cast<std::size_t> (i)] - fl[7199])));
        check (gateAc < 1e-4, "sustain pedal holds arp pattern without blocking step gate-off");
        sus.sustain (false);
        sus.render (fl.data(), fr.data(), 4800);
        check (sus.activeVoices() == 0, "releasing sustain pedal stops non-held arp with zero stuck voices");

        // Block-size independence (1 vs 64 vs 997) with Unison + Arp + Tempo running.
        auto runChunks = [] (int chunk)
        {
            Patch uniArp;
            uniArp.osc1 = { Wave1::SuperSaw, 40, 60 };
            uniArp.balance = -64;
            uniArp.unison = true;
            uniArp.unisonDetune = 18;
            uniArp.arp = { true, ArpMode::UpDown, 1, 5, 65, true };
            Engine e;
            e.prepare (48000.0, 99u);
            e.setTempo (138.0);
            e.setPatch (Part::Upper, uniArp);
            e.noteOn (48, 100);
            e.noteOn (55, 90);
            e.noteOn (60, 110);
            constexpr int len = 18000;
            std::vector<float> outL (len), outR (len);
            int pos = 0;
            while (pos < len)
            {
                const int step = std::min (chunk, len - pos);
                e.render (outL.data() + pos, outR.data() + pos, step);
                pos += step;
            }
            return std::pair<std::vector<float>, std::vector<float>> { std::move (outL), std::move (outR) };
        };
        const auto [u1L, u1R] = runChunks (1);
        const auto [u64L, u64R] = runChunks (64);
        const auto [u997L, u997R] = runChunks (997);
        check (u1L == u64L && u1R == u64R && u1L == u997L && u1R == u997R,
               "Unison + Arp rendering is bit-identical across chunk sizes 1, 64, and 997");
    }
}

void tier1Features()
{
    constexpr double hostFs = 48000.0;
    auto totalRms = [] (const std::vector<float>& L, const std::vector<float>& R, int from, int to)
    {
        double sum = 0.0;
        for (int i = from; i < to; ++i)
        {
            const double vl = L[static_cast<std::size_t> (i)];
            const double vr = R[static_cast<std::size_t> (i)];
            sum += 0.5 * (vl * vl + vr * vr);
        }
        return std::sqrt (sum / std::max (1, to - from));
    };

    // 1. Filter Velocity (positive, negative, and off).
    {
        auto renderVel = [] (int filterVel, int vel)
        {
            Patch p;
            p.osc1 = { Wave1::Saw, 0, 0 };
            p.balance = -64;
            p.filter = { FilterType::LowPass, true, 54, 10, 0, 0, 0, { 0, 0, 127, 10 } };
            p.amp = { 0, 0, 127, 10 };
            p.velocityFilter = filterVel;
            Engine e;
            e.prepare (hostFs, 11u);
            e.setPatch (Part::Upper, p);
            e.noteOn (48, vel);
            std::vector<float> L (24000), R (24000);
            e.render (L.data(), R.data(), 24000);
            std::vector<double> x (L.begin() + 4096, L.end());
            double hf = 0.0;
            for (double hz = 2000.0; hz <= 6000.0; hz += 250.0)
                hf += tone (x, hz, hostFs);
            return std::pair<double, std::vector<float>> { hf, std::move (L) };
        };
        const auto [hfPosHigh, posHigh] = renderVel (60, 127);
        const auto [hfPosLow, posLow] = renderVel (60, 20);
        const auto [hfNegHigh, negHigh] = renderVel (-60, 127);
        const auto [hfNegLow, negLow] = renderVel (-60, 20);
        const auto [hfZeroHigh, zeroHigh] = renderVel (0, 127);
        const auto [hfZeroLow, zeroLow] = renderVel (0, 20);
        check (hfPosHigh > hfPosLow * 3.0, "filter_velocity +60 brightens high velocity notes vs low velocity");
        check (hfNegLow > hfNegHigh * 3.0, "filter_velocity -60 brightens low velocity notes vs high velocity");
        (void) posHigh; (void) posLow; (void) negHigh; (void) negLow;
        check (zeroHigh == zeroLow, "filter_velocity 0 keeps note tone velocity-independent");
    }

    // 2. MIDI Channel Aftertouch -> Filter Cutoff & LFO 2 depth (independent of Mod Wheel).
    {
        Patch p;
        p.osc1 = { Wave1::Saw, 0, 0 };
        p.balance = -64;
        p.filter = { FilterType::LowPass, true, 48, 8, 0, 0, 0, { 0, 0, 127, 10 } };
        p.amp = { 0, 0, 127, 10 };
        p.aftertouchCutoff = 50;
        Engine e;
        e.prepare (hostFs, 22u);
        e.setPatch (Part::Upper, p);
        e.noteOn (48, 100);
        std::vector<float> L (24000), R (24000);
        e.render (L.data(), R.data(), 12000);
        const long allocBefore = allocations;
        countAllocations = true;
        e.aftertouch (1.0);
        e.render (L.data() + 12000, R.data() + 12000, 12000);
        countAllocations = false;
        check (allocations == allocBefore, "aftertouch updates allocate zero heap memory");

        std::vector<double> pre (L.begin() + 2000, L.begin() + 12000);
        std::vector<double> post (L.begin() + 14000, L.end());
        auto sumHf = [&] (const std::vector<double>& s)
        {
            double v = 0.0;
            for (double hz = 2000.0; hz <= 6000.0; hz += 250.0)
                v += tone (s, hz, hostFs);
            return v;
        };
        check (sumHf (post) > sumHf (pre) * 4.0, "aftertouch_cutoff +50 opens low-pass filter on held key");

        // LFO 2 depth from aftertouch even with modWheel at 0.
        Patch pLfo;
        pLfo.osc1 = { Wave1::Triangle, 0, 0 };
        pLfo.balance = -64;
        pLfo.filter = { FilterType::LowPass, true, 127, 0, 0, 0, 0, { 0, 0, 127, 10 } };
        pLfo.amp = { 0, 0, 127, 10 };
        pLfo.lfo2 = { 96, 48, 0, 0 };
        pLfo.aftertouchLfo2 = 127;
        Engine eDry, eAt;
        eDry.prepare (hostFs, 33u);
        eAt.prepare (hostFs, 33u);
        eDry.setPatch (Part::Upper, pLfo);
        eAt.setPatch (Part::Upper, pLfo);
        eDry.modWheel (0.0);
        eAt.modWheel (0.0);
        eDry.noteOn (69, 100);
        eAt.noteOn (69, 100);
        eAt.aftertouch (1.0);
        std::vector<float> dryL (24000), dryR (24000), atL (24000), atR (24000);
        eDry.render (dryL.data(), dryR.data(), 24000);
        eAt.render (atL.data(), atR.data(), 24000);
        double maxDiff = 0.0;
        for (int i = 2000; i < 24000; ++i)
            maxDiff = std::max (maxDiff, static_cast<double> (std::abs (atL[static_cast<std::size_t> (i)] - dryL[static_cast<std::size_t> (i)])));
        check (maxDiff > 0.15, "aftertouch_lfo2 drives LFO 2 pitch vibrato when modWheel is zero");
    }

    // 3. Poly Unison (2 voices/note), Mono Unison (8 voices), and Stereo Unison Spread.
    {
        Patch p;
        p.osc1 = { Wave1::Saw, 0, 0 };
        p.balance = -64;
        p.filter = { FilterType::LowPass, true, 120, 0, 0, 0, 0, { 0, 0, 127, 2 } };
        p.amp = { 0, 0, 127, 2 };
        p.mono = false;
        p.unison = true;
        p.unisonDetune = 15;
        p.unisonSpread = 0;

        Engine e;
        e.prepare (hostFs, 44u);
        e.setAnalogColor (false);
        e.setPatch (Part::Upper, p);
        std::vector<float> L (24000), R (24000);
        e.noteOn (60, 100);
        e.noteOn (64, 100);
        e.noteOn (67, 100);
        e.render (L.data(), R.data(), 4800);
        check (e.activeVoices() == 6, "poly unison 3-note chord engages 6 voices (2 per note)");

        e.noteOn (71, 100);
        e.render (L.data() + 4800, R.data() + 4800, 4800);
        check (e.activeVoices() == 8, "poly unison 4-note chord engages all 8 voices");

        e.noteOff (71);
        e.render (L.data() + 9600, R.data() + 9600, 9600);
        check (e.activeVoices() == 6, "releasing 1 note in poly unison leaves 6 chord voices active");
        check (L == R, "unisonSpread 0 produces bit-exact L == R (analogColor off)");

        // Stereo Unison Spread preserves total energy and stays click-free across live sweep.
        Patch pWide = p;
        pWide.unisonSpread = 100;
        Engine eWide;
        eWide.prepare (hostFs, 44u);
        eWide.setPatch (Part::Upper, pWide);
        for (int n : { 60, 64, 67 })
            eWide.noteOn (n, 100);
        std::vector<float> wL (24000), wR (24000);
        eWide.render (wL.data(), wR.data(), 24000);
        double sideSq = 0.0, midSq = 0.0;
        for (int i = 4000; i < 24000; ++i)
        {
            const double l = wL[static_cast<std::size_t> (i)];
            const double r = wR[static_cast<std::size_t> (i)];
            sideSq += (l - r) * (l - r);
            midSq += (l + r) * (l + r);
        }
        const double widthRatio = std::sqrt (sideSq / std::max (midSq, 1e-12));
        check (widthRatio > 0.25, "unisonSpread 100 produces strong stereo L/R difference");

        Engine eCenter;
        eCenter.prepare (hostFs, 44u);
        eCenter.setPatch (Part::Upper, p);
        for (int n : { 60, 64, 67 })
            eCenter.noteOn (n, 100);
        std::vector<float> cL (24000), cR (24000);
        eCenter.render (cL.data(), cR.data(), 24000);
        const double wideDb = db (totalRms (wL, wR, 4000, 24000) / std::max (totalRms (cL, cR, 4000, 24000), 1e-12));
        check (std::abs (wideDb) <= 1.0, "unisonSpread 100 preserves total stereo RMS within +/- 1 dB");

        // Live sweep 0 -> 100 -> 0 in Mono 8-voice Unison (zero allocations, zero clicks).
        Patch monoWide = p;
        monoWide.mono = true;
        monoWide.unison = true;
        Engine eSweep;
        eSweep.prepare (hostFs, 55u);
        eSweep.setPatch (Part::Upper, monoWide);
        eSweep.noteOn (48, 100);
        std::vector<float> sL (19200), sR (19200);
        const long allocBefore = allocations;
        countAllocations = true;
        for (int pos = 0; pos < 19200; pos += 64)
        {
            if (pos == 6400)
            {
                monoWide.unisonSpread = 100;
                eSweep.setPatch (Part::Upper, monoWide);
            }
            if (pos == 12800)
            {
                monoWide.unisonSpread = 0;
                eSweep.setPatch (Part::Upper, monoWide);
            }
            eSweep.render (sL.data() + pos, sR.data() + pos, 64);
        }
        countAllocations = false;
        check (allocations == allocBefore, "live unisonSpread sweep allocates zero heap memory");
        auto maxD2 = [&] (int a, int b)
        {
            double m = 0.0;
            for (int i = std::max (2, a); i < b; ++i)
            {
                const double dL = std::abs (static_cast<double> (sL[static_cast<std::size_t> (i)])
                                            - 2.0 * sL[static_cast<std::size_t> (i - 1)] + sL[static_cast<std::size_t> (i - 2)]);
                const double dR = std::abs (static_cast<double> (sR[static_cast<std::size_t> (i)])
                                            - 2.0 * sR[static_cast<std::size_t> (i - 1)] + sR[static_cast<std::size_t> (i - 2)]);
                m = std::max ({ m, dL, dR });
            }
            return m;
        };
        const double preD2 = maxD2 (4000, 6400);
        const double sweepD2 = std::max (maxD2 (6400, 7424), maxD2 (12800, 13824));
        check (sweepD2 <= std::max (0.015, 2.2 * preD2), "live unisonSpread 0 -> 100 -> 0 is glitch-free");
    }

    // 4. Delay Tempo Sync (exact timing, PanLR stereo bounce, tempo change, long tail, and Free passthrough).
    {
        auto findEchoFrame = [&] (const std::vector<float>& ch, int minFrame, int maxFrame)
        {
            int best = minFrame;
            double bestAbs = -1.0;
            for (int i = minFrame; i < std::min (maxFrame, static_cast<int> (ch.size())); ++i)
            {
                const double v = std::abs (static_cast<double> (ch[static_cast<std::size_t> (i)]));
                if (v > bestAbs)
                {
                    bestAbs = v;
                    best = i;
                }
            }
            return std::pair<int, double> { best, bestAbs };
        };

        Patch dp;
        dp.osc1 = { Wave1::Triangle, 0, 0 };
        dp.balance = -64;
        dp.filter = { FilterType::LowPass, true, 127, 0, 0, 0, 0, { 0, 0, 127, 0 } };
        dp.amp = { 0, 0, 127, 0 };
        dp.effects.delayType = DelayType::MonoLong;
        dp.effects.delaySync = 8; // 1/8D = 0.75 beat: 0.375 s (18000 frames @ 48 kHz) at 120 BPM
        dp.effects.delay = 127;
        dp.effects.feedback = 0;

        Engine eMono;
        eMono.prepare (hostFs, 66u);
        eMono.setAnalogColor (false);
        eMono.setOutputNode (OutputNode::Digital);
        eMono.setTempo (120.0);
        eMono.setPatch (Part::Upper, dp);
        eMono.noteOn (69, 127);
        std::vector<float> mL (48000), mR (48000);
        eMono.render (mL.data(), mR.data(), 1000);
        eMono.noteOff (69);
        eMono.render (mL.data() + 1000, mR.data() + 1000, 47000);
        const auto [monoPeak, monoMag] = findEchoFrame (mL, 10000, 30000);
        near (static_cast<double> (monoPeak), 18000.0, 200.0, "MonoLong 1/8D echo at 120 BPM lands at 18000 frames");
        check (monoMag > 0.05, "MonoLong 1/8D echo is clearly audible");

        // Tempo change 120 -> 180 BPM shifts 1/8D echo from 18000 to 12000 frames.
        eMono.setTempo (180.0);
        std::vector<float> warmL (24000), warmR (24000);
        eMono.render (warmL.data(), warmR.data(), 24000); // let delay time finish slewing & old buffer clear
        std::vector<float> fL (48000), fR (48000);
        eMono.noteOn (69, 127);
        eMono.render (fL.data(), fR.data(), 1000);
        eMono.noteOff (69);
        eMono.render (fL.data() + 1000, fR.data() + 1000, 47000);
        const auto [fastPeak, fastMag] = findEchoFrame (fL, 8000, 22000);
        near (static_cast<double> (fastPeak), 12000.0, 200.0, "MonoLong 1/8D echo at 180 BPM lands at 12000 frames");
        check (fastMag > 0.05, "180 BPM synced echo is audible");

        // PanLR 1/8D at 120 BPM: Left first echo at 18000, Right first echo at 36000.
        Patch panP = dp;
        panP.effects.delayType = DelayType::PanLR;
        Engine ePan;
        ePan.prepare (hostFs, 66u);
        ePan.setAnalogColor (false);
        ePan.setOutputNode (OutputNode::Digital);
        ePan.setTempo (120.0);
        ePan.setPatch (Part::Upper, panP);
        ePan.noteOn (69, 127);
        std::vector<float> pL (48000), pR (48000);
        ePan.render (pL.data(), pR.data(), 1000);
        ePan.noteOff (69);
        ePan.render (pL.data() + 1000, pR.data() + 1000, 47000);
        const auto [panLPk, panLMag] = findEchoFrame (pL, 12000, 24000);
        const auto [panRPk, panRMag] = findEchoFrame (pR, 30000, 42000);
        near (static_cast<double> (panLPk), 18000.0, 200.0, "PanLR 1/8D L bounce at 120 BPM lands at 18000 frames");
        near (static_cast<double> (panRPk), 36000.0, 200.0, "PanLR 1/8D R bounce at 120 BPM lands at 36000 frames");
        check (panLMag > 0.05 && panRMag > 0.05, "PanLR synced bounces are audible");

        // Long synced delay: PanLR at 1/2 note (2.0 beats = 1.0 s L, 2.0 s R @ 120 BPM -> 96000 frames)
        // must survive without idle-gate cutoff.
        Patch longP = dp;
        longP.effects.delayType = DelayType::PanLR;
        longP.effects.delaySync = 11; // 1/2
        Engine eLong;
        eLong.prepare (hostFs, 66u);
        eLong.setAnalogColor (false);
        eLong.setOutputNode (OutputNode::Digital);
        eLong.setTempo (120.0);
        eLong.setPatch (Part::Upper, longP);
        eLong.noteOn (69, 127);
        std::vector<float> lL (120000), lR (120000);
        eLong.render (lL.data(), lR.data(), 1000);
        eLong.noteOff (69);
        eLong.render (lL.data() + 1000, lR.data() + 1000, 119000);
        const auto [longRPk, longRMag] = findEchoFrame (lR, 90000, 102000);
        near (static_cast<double> (longRPk), 96000.0, 250.0, "PanLR 1/2 right bounce at 2.0 s survives idle gating");
        check (longRMag > 0.05, "PanLR 1/2 right bounce has full amplitude");

        // delaySync == 0 behaves identically to Free delay regardless of tempo.
        Patch freeP = dp;
        freeP.effects.delaySync = 0;
        Engine eA, eB;
        eA.prepare (hostFs, 77u);
        eB.prepare (hostFs, 77u);
        eA.setTempo (90.0);
        eB.setTempo (180.0);
        eA.setPatch (Part::Upper, freeP);
        eB.setPatch (Part::Upper, freeP);
        eA.noteOn (60, 100);
        eB.noteOn (60, 100);
        std::vector<float> aL (16000), aR (16000), bL (16000), bR (16000);
        eA.render (aL.data(), aR.data(), 16000);
        eB.render (bL.data(), bR.data(), 16000);
        check (aL == bL && aR == bR, "delaySync 0 ignores tempo bit-identically");
    }

    // 5. Block-size independence across all Tier 1 features active together.
    {
        auto runCombined = [] (int chunk)
        {
            Patch p;
            p.osc1 = { Wave1::SuperSaw, 50, 80 };
            p.balance = -64;
            p.filter = { FilterType::LowPass, true, 64, 30, 0, 0, 0, { 0, 10, 80, 20 } };
            p.lfo2 = { 70, 20, 20, 0 };
            p.velocityFilter = 48;
            p.aftertouchCutoff = 40;
            p.aftertouchLfo2 = 90;
            p.mono = false;
            p.unison = true;
            p.unisonDetune = 18;
            p.unisonSpread = 85;
            p.effects.delayType = DelayType::PanLR;
            p.effects.delaySync = 6; // 1/8
            p.effects.delay = 60;
            p.effects.feedback = 35;

            Engine e;
            e.prepare (hostFs, 88u);
            e.setTempo (138.0);
            e.setPatch (Part::Upper, p);
            e.noteOn (60, 110);
            e.noteOn (64, 95);
            e.noteOn (67, 120);
            constexpr int len = 18000;
            std::vector<float> outL (len), outR (len);
            int pos = 0;
            bool atOn = false;
            while (pos < len)
            {
                if (! atOn && pos >= 6000)
                {
                    atOn = true;
                    e.aftertouch (0.75);
                }
                const int nextEvt = atOn ? len : 6000;
                const int step = std::min ({ chunk, nextEvt - pos, len - pos });
                e.render (outL.data() + pos, outR.data() + pos, step);
                pos += step;
            }
            return std::pair<std::vector<float>, std::vector<float>> { std::move (outL), std::move (outR) };
        };
        const auto [t1L, t1R] = runCombined (1);
        const auto [t64L, t64R] = runCombined (64);
        const auto [t997L, t997R] = runCombined (997);
        check (t1L == t64L && t1R == t64R && t1L == t997L && t1R == t997R,
               "Tier 1 combined features are bit-identical across block sizes 1, 64, and 997");
    }
}

void dacBypass()
{
    const double fs = kEngineRate;

    // 1. DAC = Off on OutputNode::Rack emits exact 37-sample-delayed Q23 digital words (single & block).
    {
        AnalogOutput refSingle, blkOut;
        refSingle.prepare (OutputNode::Rack);
        refSingle.setDacEnabled (false);
        blkOut.prepare (OutputNode::Rack);
        blkOut.setDacEnabled (false);

        constexpr int total = 1024;
        std::vector<StereoQ23> in (total);
        for (int i = 0; i < total; ++i)
        {
            const double ph = 0.013 * i;
            in[static_cast<std::size_t> (i)] = { toQ23 (0.7 * std::sin (ph)), toQ23 (0.5 * std::cos (1.7 * ph)) };
        }
        std::vector<Stereo> singleOut (total), blockOut (total);
        for (int i = 0; i < total; ++i)
            singleOut[static_cast<std::size_t> (i)] = refSingle.processQ23 (in[static_cast<std::size_t> (i)]);
        int pos = 0;
        for (const int step : { 1, 17, 64, 129, 513, 300 })
        {
            const int n = std::min (step, total - pos);
            if (n > 0)
            {
                blkOut.processBlockQ23 (in.data() + pos, blockOut.data() + pos, n);
                pos += n;
            }
        }
        bool exactDelay = true;
        for (int i = 0; i < total; ++i)
        {
            const Stereo& s = singleOut[static_cast<std::size_t> (i)];
            const Stereo& b = blockOut[static_cast<std::size_t> (i)];
            const double expL = i >= 37 ? fromQ23 (in[static_cast<std::size_t> (i - 37)].l) : 0.0;
            const double expR = i >= 37 ? fromQ23 (in[static_cast<std::size_t> (i - 37)].r) : 0.0;
            exactDelay = exactDelay && s.l == b.l && s.r == b.r && s.l == expL && s.r == expR;
        }
        check (exactDelay, "DAC Off emits exact 37-sample-delayed 24-bit Q23 words bit-for-bit");
    }

    // 2. DAC = Off is spectrally flat across 50 Hz..20 kHz and distortionless at 0.99 FS; DAC = On applies analog response & rail.
    auto measureNode = [&] (bool dacOn, double amp, double hz)
    {
        AnalogOutput a;
        a.prepare (OutputNode::Rack);
        a.setDacEnabled (dacOn);
        std::vector<double> out (44100);
        for (int i = -88200; i < 44100; ++i)
        {
            const Q23 w = toQ23 (amp * std::sin (kTwoPi * hz * i / fs));
            const Stereo y = a.processQ23 ({ w, w });
            if (i >= 0)
                out[static_cast<std::size_t> (i)] = y.l;
        }
        return out;
    };
    const auto thd = [] (const std::vector<double>& x, double hz)
    {
        double h = 0.0;
        for (int k = 2; k <= 7; ++k)
            h += std::pow (tone (x, hz * k), 2.0);
        return std::sqrt (h) / tone (x, hz);
    };
    const auto dig1k = measureNode (false, 0.25, 1000.0);
    for (const double hz : { 50.0, 1000.0, 10000.0, 20000.0 })
        near (db (tone (measureNode (false, 0.25, hz), hz) / tone (dig1k, 1000.0)), 0.0, 0.002,
              "DAC Off is flat within 0.002 dB at " + std::to_string (static_cast<int> (hz)) + " Hz");
    const auto dacBass = measureNode (true, 0.25, 50.0);
    const auto dac1k = measureNode (true, 0.25, 1000.0);
    check (db (tone (dacBass, 50.0) / tone (dac1k, 1000.0)) > 1.5, "DAC On exhibits analog bass shelf boost at 50 Hz");
    const auto digHot = measureNode (false, 0.99, 50.0), dacHot = measureNode (true, 0.99, 50.0);
    check (thd (digHot, 50.0) < 1.0e-5 && thd (dacHot, 50.0) > 1.0e-3,
           "DAC Off stays clean near full scale while DAC On rounds peaks near the op-amp rail");

    // 3. Impulse group delay alignment: DAC On and DAC Off peak at the same sample (37 at 88.2 kHz).
    {
        AnalogOutput aOn, aOff;
        aOn.prepare (OutputNode::Rack);
        aOn.setDacEnabled (true);
        aOff.prepare (OutputNode::Rack);
        aOff.setDacEnabled (false);
        constexpr int t0 = 200;
        int pkOn = -1, pkOff = -1;
        double maxOn = 0.0, maxOff = 0.0;
        for (int i = 0; i < 400; ++i)
        {
            const Q23 w = (i == t0) ? toQ23 (0.5) : 0;
            const Stereo yOn = aOn.processQ23 ({ w, w });
            const Stereo yOff = aOff.processQ23 ({ w, w });
            if (std::abs (yOn.l) > maxOn) { maxOn = std::abs (yOn.l); pkOn = i; }
            if (std::abs (yOff.l) > maxOff) { maxOff = std::abs (yOff.l); pkOff = i; }
        }
        check (pkOff == t0 + 37, "DAC Off impulse peaks at exact +37 samples");
        check (std::abs (pkOn - pkOff) <= 2, "DAC On impulse aligns within 2 samples of DAC Off (Downsampler8 + analog IIR group delay)");
    }

    // 4. Engine true-idle silence with DAC Off.
    {
        Engine idleEng;
        idleEng.prepare (48000.0, 991u);
        idleEng.setDacEnabled (false);
        Patch shortPat;
        shortPat.osc1 = { Wave1::Saw, 0, 0 };
        shortPat.balance = -64;
        shortPat.amp = { 0, 8, 0, 4 };
        idleEng.setPatch (Part::Upper, shortPat);
        std::vector<float> il (48000), ir (48000);
        idleEng.render (il.data(), ir.data(), 48000);
        check (std::all_of (il.begin(), il.end(), [] (float v) { return v == 0.0f; })
               && std::all_of (ir.begin(), ir.end(), [] (float v) { return v == 0.0f; }),
               "DAC Off pre-trigger render emits exact zeros");
        idleEng.noteOn (60, 100);
        idleEng.allNotesOff();
        for (int b = 0; b < 8; ++b)
            idleEng.render (il.data(), ir.data(), 48000);
        check (std::all_of (il.end() - 4096, il.end(), [] (float v) { return v == 0.0f; }),
               "DAC Off released voice decays into exact idle zeros");
    }

    // 5. Cold Off->On, hot On->Off, mid-warmup cancel (Off->On->Off), and mid-fade reversal are click-free & chunk-invariant.
    {
        auto maxD2 = [] (const std::vector<float>& L, const std::vector<float>& R, int a, int b)
        {
            double m = 0.0;
            for (int i = std::max (2, a); i < std::min (static_cast<int> (L.size()), b); ++i)
            {
                const double dL = std::abs (double (L[size_t (i)]) - 2.0 * L[size_t (i - 1)] + L[size_t (i - 2)]);
                const double dR = std::abs (double (R[size_t (i)]) - 2.0 * R[size_t (i - 1)] + R[size_t (i - 2)]);
                m = std::max ({ m, dL, dR });
            }
            return m;
        };
        auto runScript = [] (int chunk)
        {
            Patch p;
            p.osc1 = { Wave1::Triangle, 0, 0 };
            p.balance = -64;
            p.filter = { FilterType::LowPass, true, 90, 20, 0, 0, 0, { 0, 0, 127, 40 } };
            p.amp = { 0, 0, 127, 40 };
            p.level = 110;
            p.effects.bass = 20;

            Engine e;
            e.prepare (48000.0, 2026u);
            e.setDacEnabled (false);
            e.setPatch (Part::Upper, p);
            e.noteOn (52, 110);

            constexpr int len = 16000;
            std::vector<float> L (len), R (len);
            const std::array<std::pair<int, bool>, 6> events { {
                { 4000, true },   // cold Off -> On
                { 4005, false },  // cancel during warmup!
                { 5000, true },   // cold Off -> On completes
                { 7000, false },  // hot On -> Off
                { 7035, true },   // mid-crossfade reversal Off -> On
                { 9500, false }   // hot On -> Off completes
            } };
            std::size_t evIdx = 0;
            int pos = 0;
            while (pos < len)
            {
                while (evIdx < events.size() && pos == events[evIdx].first)
                    e.setDacEnabled (events[evIdx++].second);
                const int nextEv = evIdx < events.size() ? events[evIdx].first : len;
                const int step = std::min ({ chunk, nextEv - pos, len - pos });
                e.render (L.data() + pos, R.data() + pos, step);
                pos += step;
            }
            return std::pair<std::vector<float>, std::vector<float>> { std::move (L), std::move (R) };
        };
        const auto [L1, R1] = runScript (1);
        const auto [L64, R64] = runScript (64);
        const auto [L997, R997] = runScript (997);
        check (L1 == L64 && R1 == R64 && L1 == L997 && R1 == R997,
               "DAC transitions (incl. warmup cancel & fade reversal) are bit-identical across block sizes 1, 64, 997");
        const double baseD2 = maxD2 (L64, R64, 2000, 3900);
        const double transD2 = maxD2 (L64, R64, 3990, 11000);
        check (transD2 <= std::max (0.01, 2.5 * baseD2),
               "DAC toggle stress test stays glitch-free D2=" + std::to_string (transD2) + " base=" + std::to_string (baseD2));
    }
}

void sysexCodec()
{
    using namespace trancera::sysex;

    // 1. 7-bit address arithmetic & Roland checksum boundaries.
    check (linearOffset (0x00, 0x7F) == 127 && linearOffset (0x01, 0x00) == 128,
           "7-bit base-128 offset steps seamlessly across 0x007F -> 0x0100");
    check (linearOffset (0x01, 0x72) == 242 && splitOffset (242) == std::pair<std::uint8_t, std::uint8_t> { 0x01, 0x72 },
           "JP-8080 hardware split offset 0x0172 decodes/encodes to 242");
    check (formatSlotLabel (SlotKind::UserPatch, 0) == "U-A11"
           && formatSlotLabel (SlotKind::UserPatch, 63) == "U-A88"
           && formatSlotLabel (SlotKind::UserPatch, 64) == "U-B11"
           && formatSlotLabel (SlotKind::UserPatch, 127) == "U-B88",
           "user slot octal labels span U-A11..U-B88");

    // 2. Full Patch + 40-destination VelocityMatrix round-trip (JP-8000 single packet).
    Patch full {};
    full.osc1 = { Wave1::FeedbackOsc, 77, 103 };
    full.osc2 = { Wave2::Triangle, true, -19, 37, 91, 44 };
    full.ring = true;
    full.xmod = 85;
    full.balance = 29;
    full.destination = ModDestination::XMod;
    full.pitchLfo = -41;
    full.pitchEnvDepth = 52;
    full.pitchAttack = 17;
    full.pitchDecay = 88;
    full.filter = { FilterType::BandPass, false, 63, 99, -22, 31, -48, { 12, 54, 87, 33 } };
    full.amp = { 9, 44, 110, 61 };
    full.level = 115;
    full.pan = PanMode::Auto;
    full.ampLfo = -18;
    full.lfo1 = { LfoWave::SampleHold, 93, 41 };
    full.lfo2 = { 77, -12, 39, -27 };
    full.effects = { 21, -33, ChorusType::JetPhasing, 72, DelayType::PanShort, 83, 66, 49, 0 };
    full.mono = true;
    full.legato = true;
    full.portamento = true;
    full.portamentoTime = 65;
    full.oscShift = -2;
    full.bendUp = 12;
    full.bendDown = 5;
    full.velocityMatrix = {
        true,
        -127, -1, 0, 1, 127,
        -99, 88, -45, 19, -64,
        120, -111, -50, 100, -1, 127,
        -80, 55, -13, 42, -31,
        127, -127, 64, -64, 32,
        105, -20, 15, -10, 5, -5, 44,
        -127, 127, 60, -30, 15, -7, 127
    };

    const auto pkt8k = encodePatch (full, "AcidLead 16CharX", Format::JP8000, SlotKind::TempUpper);
    check (pkt8k.size() == 6 + 4 + kKeyboardPatchBytes + 2, "JP-8000 single DT1 packet has exact 251-byte wire length");
    const ParseResult dec8k = parseStream (pkt8k.data(), pkt8k.size());
    check (dec8k.patches.size() == 1 && dec8k.messagesParsed == 1 && dec8k.badChecksums == 0,
           "JP-8000 single patch decodes cleanly");
    if (! dec8k.patches.empty())
    {
        const PatchRecord& r = dec8k.patches[0];
        const Patch& g = r.patch;
        check (r.name == "AcidLead 16CharX" && r.detectedFormat == Format::JP8000, "16-char name & JP-8000 format preserved");
        check (g.osc1.wave == full.osc1.wave && g.osc1.ctrl1 == full.osc1.ctrl1 && g.osc1.ctrl2 == full.osc1.ctrl2
               && g.osc2.wave == full.osc2.wave && g.osc2.sync == full.osc2.sync && g.osc2.range == full.osc2.range
               && g.osc2.fine == full.osc2.fine && g.osc2.pulseWidth == full.osc2.pulseWidth && g.osc2.pwmDepth == full.osc2.pwmDepth
               && g.ring == full.ring && g.xmod == full.xmod && g.balance == full.balance && g.destination == full.destination
               && g.pitchLfo == full.pitchLfo && g.pitchEnvDepth == full.pitchEnvDepth && g.pitchAttack == full.pitchAttack && g.pitchDecay == full.pitchDecay
               && g.filter.type == full.filter.type && g.filter.steep == full.filter.steep && g.filter.cutoff == full.filter.cutoff
               && g.filter.resonance == full.filter.resonance && g.filter.keyFollow == full.filter.keyFollow
               && g.filter.lfoDepth == full.filter.lfoDepth && g.filter.envDepth == full.filter.envDepth
               && g.filter.env.attack == full.filter.env.attack && g.filter.env.decay == full.filter.env.decay
               && g.filter.env.sustain == full.filter.env.sustain && g.filter.env.release == full.filter.env.release
               && g.level == full.level && g.ampLfo == full.ampLfo && g.pan == full.pan
               && g.amp.attack == full.amp.attack && g.amp.decay == full.amp.decay && g.amp.sustain == full.amp.sustain && g.amp.release == full.amp.release
               && g.lfo1.wave == full.lfo1.wave && g.lfo1.rate == full.lfo1.rate && g.lfo1.fade == full.lfo1.fade
               && g.lfo2.rate == full.lfo2.rate && g.lfo2.pitch == full.lfo2.pitch && g.lfo2.filter == full.lfo2.filter && g.lfo2.amp == full.lfo2.amp
               && g.effects.bass == full.effects.bass && g.effects.treble == full.effects.treble
               && g.effects.chorusType == full.effects.chorusType && g.effects.chorus == full.effects.chorus
               && g.effects.delayType == full.effects.delayType && g.effects.delayTime == full.effects.delayTime
               && g.effects.feedback == full.effects.feedback && g.effects.delay == full.effects.delay
               && g.mono == full.mono && g.legato == full.legato && g.portamento == full.portamento
               && g.portamentoTime == full.portamentoTime && g.oscShift == full.oscShift
               && g.bendUp == full.bendUp && g.bendDown == full.bendDown,
               "JP-8000 all 50+ synth controls survive DT1 round-trip bit-exact");

        const VelocityMatrix& a = g.velocityMatrix;
        const VelocityMatrix& b = full.velocityMatrix;
        check (a.on && a.lfo1Rate == b.lfo1Rate && a.lfo1Fade == b.lfo1Fade && a.lfo2Rate == b.lfo2Rate
               && a.xmod == b.xmod && a.balance == b.balance && a.pitchLfo == b.pitchLfo && a.pitchLfo2 == b.pitchLfo2
               && a.pitchEnvDepth == b.pitchEnvDepth && a.pitchAttack == b.pitchAttack && a.pitchDecay == b.pitchDecay
               && a.osc1Ctrl1 == b.osc1Ctrl1 && a.osc1Ctrl2 == b.osc1Ctrl2 && a.osc2Range == b.osc2Range && a.osc2Fine == b.osc2Fine
               && a.osc2Ctrl1 == b.osc2Ctrl1 && a.osc2Ctrl2 == b.osc2Ctrl2 && a.cutoff == b.cutoff && a.resonance == b.resonance
               && a.keyFollow == b.keyFollow && a.filterLfo == b.filterLfo && a.filterLfo2 == b.filterLfo2
               && a.envDepth == b.envDepth && a.filterAttack == b.filterAttack && a.filterDecay == b.filterDecay
               && a.filterSustain == b.filterSustain && a.filterRelease == b.filterRelease && a.level == b.level
               && a.ampLfo == b.ampLfo && a.ampLfo2 == b.ampLfo2 && a.ampAttack == b.ampAttack && a.ampDecay == b.ampDecay
               && a.ampSustain == b.ampSustain && a.ampRelease == b.ampRelease && a.bass == b.bass && a.treble == b.treble
               && a.multiFx == b.multiFx && a.delayTime == b.delayTime && a.delayFeedback == b.delayFeedback
               && a.delayLevel == b.delayLevel && a.portamento == b.portamento,
               "all 40 two-byte VelocityMatrix fields survive 0x011B..0x016A round-trip exact");
    }

    // 3. JP-8080 248B split (242 + 6 at +01 72) vs single 248B equivalence.
    Patch rackPatch = full;
    rackPatch.unison = true;
    rackPatch.unisonDetune = 37;
    const auto split8080 = encodePatch (rackPatch, "Rack Unison", Format::JP8080, SlotKind::TempUpper, 0, kDefaultDeviceId, true);
    const auto flat8080 = encodePatch (rackPatch, "Rack Unison", Format::JP8080, SlotKind::TempUpper, 0, kDefaultDeviceId, false);
    const ParseResult dSplit = parseStream (split8080.data(), split8080.size());
    const ParseResult dFlat = parseStream (flat8080.data(), flat8080.size());
    check (dSplit.patches.size() == 1 && dSplit.messagesParsed == 2 && dFlat.patches.size() == 1 && dFlat.messagesParsed == 1,
           "JP-8080 split emits 2 packets and flat emits 1 packet");
    check (dSplit.patches[0].detectedFormat == Format::JP8080
           && dSplit.patches[0].patch.unison && dSplit.patches[0].patch.unisonDetune == 37
           && dFlat.patches[0].patch.unison && dFlat.patches[0].patch.unisonDetune == 37,
           "JP-8080 Unison + UnisonDetune survive both split and flat encodings");

    // 4. 128-patch User Bank U:A11..U:B88 round-trip.
    std::vector<std::pair<std::string, Patch>> bankIn;
    bankIn.reserve (kMaxUserPatches);
    for (int i = 0; i < static_cast<int> (kMaxUserPatches); ++i)
    {
        Patch pi {};
        pi.osc1.ctrl1 = i;
        pi.filter.cutoff = 127 - i;
        pi.unison = (i % 3 == 0);
        pi.unisonDetune = i % 51;
        bankIn.emplace_back ("User " + std::to_string (i), pi);
    }
    const auto bankSyx = encodeUserBank (bankIn, Format::JP8080, 0x13, true);
    const ParseResult bankOut = parseStream (bankSyx.data(), bankSyx.size());
    bool bankOk = bankOut.patches.size() == kMaxUserPatches && bankOut.badChecksums == 0;
    for (int i = 0; i < static_cast<int> (kMaxUserPatches) && bankOk; ++i)
    {
        const PatchRecord& pr = bankOut.patches[static_cast<std::size_t> (i)];
        bankOk = pr.slotKind == SlotKind::UserPatch && pr.slotIndex == i
              && pr.name == bankIn[static_cast<std::size_t> (i)].first
              && pr.patch.osc1.ctrl1 == i && pr.patch.filter.cutoff == 127 - i
              && pr.patch.unison == (i % 3 == 0) && pr.patch.unisonDetune == (i % 51);
    }
    check (bankOk, "128-patch User Bank U:A11..U:B88 round-trips in order");

    // 5. Performance Common + PartUpper/PartLower DelaySync overlay + warnings + SMF .mid unwrapping.
    auto makeDt1 = [] (std::uint8_t a3, std::uint8_t a2, std::uint8_t a1, std::uint8_t a0, std::vector<std::uint8_t> data)
    {
        std::vector<std::uint8_t> msg { 0xF0, 0x41, 0x10, 0x00, 0x06, 0x12, a3, a2, a1, a0 };
        msg.insert (msg.end(), data.begin(), data.end());
        msg.push_back (computeChecksum (msg.data() + 6, 4 + data.size()));
        msg.push_back (0xF7);
        return msg;
    };
    std::vector<std::uint8_t> perfCommon (0x25, 0);
    perfCommon[0x15] = 0; // UPPER & LOWER arp
    perfCommon[0x17] = 1; // Arp ON
    perfCommon[0x18] = 2; // Up/Down
    perfCommon[0x19] = 3; // 1/12 -> Trancera 1/8T (4)
    perfCommon[0x1A] = 2; // 3 Octaves
    perfCommon[0x1B] = 1; // Hold ON
    std::vector<std::uint8_t> perfStream = makeDt1 (0x01, 0x00, 0x00, 0x00, perfCommon);
    auto partUp = makeDt1 (0x01, 0x00, 0x10, 0x00, { 0, 0, 0, 0, 7 }); // DelaySync 7 = 1/4 -> Trancera 9
    auto partLo = makeDt1 (0x01, 0x00, 0x11, 0x00, { 0, 0, 0, 0, 1 }); // DelaySync 1 = 1/16 -> Trancera 3
    perfStream.insert (perfStream.end(), partUp.begin(), partUp.end());
    perfStream.insert (perfStream.end(), partLo.begin(), partLo.end());
    Patch pUp {}, pLo {};
    auto patchUp = encodePatch (pUp, "Perf Upper", Format::JP8000, SlotKind::TempUpper);
    auto patchLo = encodePatch (pLo, "Perf Lower", Format::JP8000, SlotKind::TempLower);
    // Test Osc2=Noise (3) on Upper and MultiFX=Distortion (12) on Lower by tweaking payload & recomputing checksum.
    patchUp[6 + 4 + 0x21] = 3;
    patchUp[patchUp.size() - 2] = computeChecksum (patchUp.data() + 6, patchUp.size() - 8);
    patchLo[6 + 4 + 0x3D] = 12;
    patchLo[patchLo.size() - 2] = computeChecksum (patchLo.data() + 6, patchLo.size() - 8);
    perfStream.insert (perfStream.end(), patchUp.begin(), patchUp.end());
    perfStream.insert (perfStream.end(), patchLo.begin(), patchLo.end());

    const ParseResult perfRes = parseStream (perfStream.data(), perfStream.size());
    check (perfRes.patches.size() == 2
           && perfRes.patches[0].patch.arp.on
           && perfRes.patches[0].patch.arp.mode == ArpMode::UpDown
           && perfRes.patches[0].patch.arp.rate == 4
           && perfRes.patches[0].patch.arp.range == 2
           && perfRes.patches[0].patch.arp.hold
           && perfRes.patches[0].patch.effects.delaySync == 9
           && perfRes.patches[0].patch.osc2.wave == Wave2::Triangle
           && perfRes.patches[0].warnings.osc2NoiseMappedToTriangle
           && perfRes.patches[1].patch.effects.delaySync == 3
           && perfRes.patches[1].patch.effects.chorusType == ChorusType::Distortion
           && ! perfRes.patches[1].warnings.distortionMappedToChorus,
           "Performance overlay maps Arp + DelaySync + native JP-8080 Distortion and Osc2Noise fallback");

    // 5b. Exporting ChorusType::Distortion emits byte 12 in JP-8080 format and maps to 0 (SuperSlow) in JP-8000 format.
    {
        Patch distPatch = full;
        distPatch.effects.chorusType = ChorusType::Distortion;
        distPatch.effects.chorus = 77;
        std::array<std::uint8_t, kRackPatchBytes> rackBytes {};
        std::array<std::uint8_t, kKeyboardPatchBytes> kbBytes {};
        check (writePatchBytes (distPatch, "DistLead", Format::JP8080, rackBytes.data(), rackBytes.size()) == kRackPatchBytes
               && rackBytes[0x3D] == 12,
               "JP-8080 raw export encodes ChorusType::Distortion as MultiEffectsType 12");
        check (writePatchBytes (distPatch, "DistLead", Format::JP8000, kbBytes.data(), kbBytes.size()) == kKeyboardPatchBytes
               && kbBytes[0x3D] == 0,
               "JP-8000 raw export clamps ChorusType::Distortion cleanly to 0 (Super Chorus Slow)");

        const auto dist8080Pkt = encodePatch (distPatch, "DistLead", Format::JP8080);
        const ParseResult dist8080Res = parseStream (dist8080Pkt.data(), dist8080Pkt.size());
        check (dist8080Res.patches.size() == 1
               && dist8080Res.patches[0].detectedFormat == Format::JP8080
               && dist8080Res.patches[0].patch.effects.chorusType == ChorusType::Distortion
               && ! dist8080Res.patches[0].warnings.distortionMappedToChorus,
               "JP-8080 Distortion SysEx round-trips natively without fallback warnings");

        const auto dist8kPkt = encodePatch (distPatch, "DistLead", Format::JP8000);
        const ParseResult dist8kRes = parseStream (dist8kPkt.data(), dist8kPkt.size());
        check (dist8kRes.patches.size() == 1
               && dist8kRes.patches[0].detectedFormat == Format::JP8000
               && dist8kRes.patches[0].patch.effects.chorusType == ChorusType::SuperSlow,
               "JP-8000 SysEx export maps Distortion safely to SuperSlow");
    }

    // 6. Standard MIDI File (.mid) extraction + corrupt checksum rejection.
    std::vector<std::uint8_t> trackBody;
    trackBody.push_back (0x00); // delta 0
    trackBody.push_back (0xF0);
    std::uint32_t sxLen = static_cast<std::uint32_t> (pkt8k.size() - 1); // excludes leading F0
    trackBody.push_back (static_cast<std::uint8_t> (((sxLen >> 7) & 0x7Fu) | 0x80u));
    trackBody.push_back (static_cast<std::uint8_t> (sxLen & 0x7Fu));
    trackBody.insert (trackBody.end(), pkt8k.begin() + 1, pkt8k.end());
    trackBody.insert (trackBody.end(), { 0x00, 0xFF, 0x2F, 0x00 }); // End of track
    std::vector<std::uint8_t> smf { 'M', 'T', 'h', 'd', 0, 0, 0, 6, 0, 0, 0, 1, 0, 96,
                                    'M', 'T', 'r', 'k',
                                    static_cast<std::uint8_t> ((trackBody.size() >> 24) & 0xFF),
                                    static_cast<std::uint8_t> ((trackBody.size() >> 16) & 0xFF),
                                    static_cast<std::uint8_t> ((trackBody.size() >> 8) & 0xFF),
                                    static_cast<std::uint8_t> (trackBody.size() & 0xFF) };
    smf.insert (smf.end(), trackBody.begin(), trackBody.end());
    const ParseResult smfRes = parseStream (smf.data(), smf.size());
    check (smfRes.patches.size() == 1 && smfRes.patches[0].name == "AcidLead 16CharX",
           "SMF (.mid) wrapped DT1 SysEx unwraps cleanly");

    auto corrupt = pkt8k;
    corrupt[corrupt.size() - 2] ^= 0x01u;
    const ParseResult badStrict = parseStream (corrupt.data(), corrupt.size(), true);
    const ParseResult badLenient = parseStream (corrupt.data(), corrupt.size(), false);
    check (badStrict.badChecksums == 1 && badStrict.patches.empty()
           && badLenient.badChecksums == 1 && badLenient.patches.size() == 1 && badLenient.patches[0].warnings.checksumErrors,
           "checksum verification rejects strictly and flags leniently");

    // 7. Velocity bridge round-trip for 70-control exports.
    Patch baked = full;
    bakeVelocityMatrixToControls (baked);
    check (baked.velocityAmp == 105 && baked.velocityFilter == -40,
           "bakeVelocityMatrixToControls bridges VelocityMatrix level & cutoff to panel controls");
    Patch panelOnly {};
    panelOnly.velocityAmp = 72;
    panelOnly.velocityFilter = -32;
    const auto exportedPanel = encodePatch (panelOnly, "PanelVel", Format::JP8000);
    const ParseResult reimportedPanel = parseStream (exportedPanel.data(), exportedPanel.size());
    check (reimportedPanel.patches.size() == 1 && reimportedPanel.patches[0].patch.velocityMatrix.on
           && reimportedPanel.patches[0].patch.velocityMatrix.level == 72
           && std::abs (reimportedPanel.patches[0].patch.velocityMatrix.cutoff - (-65)) <= 1,
           "panel Velocity & FilterVelocity export into JP VelocityMatrix");
}

int main (int argc, char** argv)
{
    const std::pair<const char*, std::function<void()>> suites[] {
        { "SuperSaw", superSaw }, { "HardwareRecording", hardwareRecording }, { "MeasuredLaws", measuredLaws }, { "Filter", filter }, { "Circuit", circuit },
        { "Engine", engine }, { "Feedback", feedback }, { "Effects", effects }, { "Stability", stability }, { "Timing", timing }, { "Modulation", modulation }, { "ToneControl", toneControl }, { "SustainLaw", sustainLaw }, { "KeyFollowShift", keyFollowShift }, { "XmodDestination", xmodDestination }, { "EffectLevels", effectLevels }, { "Controls", controls },
        { "TriangleShape", triangleShape }, { "MultiFxTypes", multiFxTypes }, { "EnvelopeSmoother", envelopeSmoother }, { "ToneSaturation", toneSaturation }, { "Osc2AndSync", osc2AndSync }, { "ControlAccuracy", controlAccuracy }, { "AnalogColor", analogColor },
        { "FixedPointAndMatrix", fixedPointAndMatrix }, { "AutomationGlitch", automationGlitch }, { "UnisonAndArp", unisonAndArp }, { "Tier1Features", tier1Features }, { "DacBypass", dacBypass }, { "SysEx", sysexCodec } };
    for (const auto& [name, run] : suites)
        if (argc < 2 || std::string (argv[1]) == name)
            run();
    std::printf ("%d checks, %d failed\n", checks, failures);
    return failures || ! checks ? 1 : 0;
}
