// Behavioural checks for the proof of concept. Each expectation cites what
// pins it: the JP-8000 owner's manual (OM), the service notes (SN), or
// Szabo 2010 (a black-box measurement of the hardware).
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
    near (mapping::superCentre (0.0), 0.99785, 1e-9, "centre gain at mix 0");
    near (mapping::superSide (1.0), -0.73764 + 1.2841 + 0.044372, 1e-9, "side gain at mix 1");
    check (mapping::superDetune (1.0) > 0.98 && mapping::superDetune (1.0) < 1.02, "detune law end");
    check (mapping::superDetune (0.5) < 0.15, "detune law is flat through its first half");

    // Pitch-tracked high-pass: what aliases below f0 is well under the fundamental.
    SuperSaw high;
    high.trigger (random);
    high.configure (6000.0, 0.0, 0.0);
    for (auto& v : x)
        v = high.process();
    check (db (tone (x, 1800.0) / tone (x, 6000.0)) < -35.0, "alias below f0 is filtered");
}

void filter()
{
    const auto gain = [] (FilterType type, bool steep, double hz)
    {
        Filter f;
        f.configure (1000.0, 0.0, type, steep);
        std::vector<double> x (44100);
        for (int i = -8820; i < 44100; ++i)
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

    for (const bool steep : { false, true })
    {
        // OM p.60: fully raised resonance oscillates.
        Filter f;
        f.configure (1000.0, 1.0, FilterType::LowPass, steep);
        f.process (0.5);
        std::vector<double> x (44100);
        for (int i = 0; i < 88200 * 2; ++i)
            f.process (0.0);
        for (auto& v : x)
            v = f.process (0.0);
        check (tone (x, peakHz (x, 950, 1050, 2)) > 0.02, "self-oscillation is sustained");
        near (peakHz (x, 950, 1050, 2), 1000.0, 10.0, "self-oscillation tracks cutoff");
        check (*std::max_element (x.begin(), x.end()) < 4.0, "self-oscillation is bounded");
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
    double previous = 0.0;
    for (const int harmonics : { 18, 64, 110 })
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
        const double n = best / 220.0;
        near (n, std::round (n), 0.03, "howl sits on a harmonic");
        check (n > previous + 0.5, "HARMONICS raises the howling harmonic");
        check (db (level / tone (x, 220.0, 48000)) > 6.0, "howl stands over the fundamental");
        previous = n;
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
            if (i > 0 && std::abs (y.l) > 0.05)
                first = i;
        }
        near (first, seconds * fs, 3.0, "delay maximum");
    }
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
        { "SuperSaw", superSaw }, { "Filter", filter }, { "Circuit", circuit },
        { "Engine", engine }, { "Feedback", feedback }, { "Effects", effects }, { "Stability", stability } };
    for (const auto& [name, run] : suites)
        if (argc < 2 || std::string (argv[1]) == name)
            run();
    std::printf ("%d checks, %d failed\n", checks, failures);
    return failures || ! checks ? 1 : 0;
}
