// Behavioural checks for the proof of concept. Each expectation cites what
// pins it: the JP-8000 owner's manual (OM), the service notes (SN), or
// Szabo 2010 (a black-box measurement of the hardware).
#include "../Source/DSP/TranceraControls.h"
#include "../Source/DSP/TranceraEngine.h"

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
}

int main (int argc, char** argv)
{
    const std::pair<const char*, std::function<void()>> suites[] {
        { "SuperSaw", superSaw }, { "HardwareRecording", hardwareRecording }, { "MeasuredLaws", measuredLaws }, { "Filter", filter }, { "Circuit", circuit },
        { "Engine", engine }, { "Feedback", feedback }, { "Effects", effects }, { "Stability", stability }, { "Timing", timing }, { "Modulation", modulation }, { "ToneControl", toneControl }, { "SustainLaw", sustainLaw }, { "KeyFollowShift", keyFollowShift }, { "XmodDestination", xmodDestination }, { "EffectLevels", effectLevels }, { "Controls", controls },
        { "TriangleShape", triangleShape }, { "MultiFxTypes", multiFxTypes }, { "EnvelopeSmoother", envelopeSmoother }, { "ToneSaturation", toneSaturation }, { "Osc2AndSync", osc2AndSync }, { "ControlAccuracy", controlAccuracy }, { "AnalogColor", analogColor },
        { "FixedPointAndMatrix", fixedPointAndMatrix } };
    for (const auto& [name, run] : suites)
        if (argc < 2 || std::string (argv[1]) == name)
            run();
    std::printf ("%d checks, %d failed\n", checks, failures);
    return failures || ! checks ? 1 : 0;
}
