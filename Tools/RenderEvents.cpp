// Renders a timed event list through the engine, for A/B tests against
// recordings: TranceraRenderEvents <events.txt> <out.f32> <rate> <seconds>
//
// One event a line, "<seconds> <name> <value>...": note <key> <velocity>,
// off <key>, set <parameter> <value>, mod <0-127>, or node rack|printed|digital. Writes mono
// float32 (the left channel), or interleaved stereo with a fifth argument
// "stereo". Lines starting with # are ignored.
#include "TranceraEngine.h"

#include <cstdio>
#include <fstream>
#include <map>
#include <memory>
#include <sstream>
#include <string>
#include <vector>

using namespace trancera;

namespace
{
bool set (Patch& p, const std::string& name, int v)
{
    const std::map<std::string, int*> ints {
        { "osc1.ctrl1", &p.osc1.ctrl1 }, { "osc1.ctrl2", &p.osc1.ctrl2 }, { "balance", &p.balance },
        { "cutoff", &p.filter.cutoff }, { "resonance", &p.filter.resonance },
        { "keyfollow", &p.filter.keyFollow }, { "envdepth", &p.filter.envDepth },
        { "fenv.a", &p.filter.env.attack }, { "fenv.d", &p.filter.env.decay },
        { "fenv.s", &p.filter.env.sustain }, { "fenv.r", &p.filter.env.release },
        { "amp.a", &p.amp.attack }, { "amp.d", &p.amp.decay }, { "amp.s", &p.amp.sustain },
        { "amp.r", &p.amp.release }, { "level", &p.level }, { "velocityamp", &p.velocityAmp }, { "velocityfilter", &p.velocityFilter },
        { "bass", &p.effects.bass }, { "treble", &p.effects.treble }, { "osc2.range", &p.osc2.range },
        { "pitchlfo", &p.pitchLfo }, { "lfo1.rate", &p.lfo1.rate }, { "filterlfo", &p.filter.lfoDepth },
        { "pitchenv", &p.pitchEnvDepth }, { "pitchenv.a", &p.pitchAttack }, { "pitchenv.d", &p.pitchDecay },
        { "portamento.time", &p.portamentoTime }, { "lfo1.fade", &p.lfo1.fade }, { "lfo2.rate", &p.lfo2.rate },
        { "lfo2.pitch", &p.lfo2.pitch }, { "lfo2.filter", &p.lfo2.filter }, { "lfo2.amp", &p.lfo2.amp },
        { "xmod", &p.xmod }, { "amplfo", &p.ampLfo }, { "chorus", &p.effects.chorus },
        { "delay.time", &p.effects.delayTime }, { "delay.feedback", &p.effects.feedback }, { "delay", &p.effects.delay }, { "delay.sync", &p.effects.delaySync },
        { "osc2.fine", &p.osc2.fine }, { "osc2.pw", &p.osc2.pulseWidth }, { "osc2.pwm", &p.osc2.pwmDepth },
        { "oscshift", &p.oscShift }, { "unison.detune", &p.unisonDetune }, { "unison.spread", &p.unisonSpread },
        { "aftertouch.cutoff", &p.aftertouchCutoff }, { "aftertouch.lfo2", &p.aftertouchLfo2 },
        { "vel.lfo1Rate", &p.velocityMatrix.lfo1Rate },
        { "vel.lfo1Fade", &p.velocityMatrix.lfo1Fade },
        { "vel.lfo2Rate", &p.velocityMatrix.lfo2Rate },
        { "vel.xmod", &p.velocityMatrix.xmod },
        { "vel.balance", &p.velocityMatrix.balance },
        { "vel.pitchLfo", &p.velocityMatrix.pitchLfo },
        { "vel.pitchLfo2", &p.velocityMatrix.pitchLfo2 },
        { "vel.pitchEnvDepth", &p.velocityMatrix.pitchEnvDepth },
        { "vel.pitchAttack", &p.velocityMatrix.pitchAttack },
        { "vel.pitchDecay", &p.velocityMatrix.pitchDecay },
        { "vel.osc1Ctrl1", &p.velocityMatrix.osc1Ctrl1 },
        { "vel.osc1Ctrl2", &p.velocityMatrix.osc1Ctrl2 },
        { "vel.osc2Range", &p.velocityMatrix.osc2Range },
        { "vel.osc2Fine", &p.velocityMatrix.osc2Fine },
        { "vel.osc2Ctrl1", &p.velocityMatrix.osc2Ctrl1 },
        { "vel.osc2Ctrl2", &p.velocityMatrix.osc2Ctrl2 },
        { "vel.cutoff", &p.velocityMatrix.cutoff },
        { "vel.resonance", &p.velocityMatrix.resonance },
        { "vel.keyFollow", &p.velocityMatrix.keyFollow },
        { "vel.filterLfo", &p.velocityMatrix.filterLfo },
        { "vel.filterLfo2", &p.velocityMatrix.filterLfo2 },
        { "vel.envDepth", &p.velocityMatrix.envDepth },
        { "vel.filterAttack", &p.velocityMatrix.filterAttack },
        { "vel.filterDecay", &p.velocityMatrix.filterDecay },
        { "vel.filterSustain", &p.velocityMatrix.filterSustain },
        { "vel.filterRelease", &p.velocityMatrix.filterRelease },
        { "vel.level", &p.velocityMatrix.level },
        { "vel.ampLfo", &p.velocityMatrix.ampLfo },
        { "vel.ampLfo2", &p.velocityMatrix.ampLfo2 },
        { "vel.ampAttack", &p.velocityMatrix.ampAttack },
        { "vel.ampDecay", &p.velocityMatrix.ampDecay },
        { "vel.ampSustain", &p.velocityMatrix.ampSustain },
        { "vel.ampRelease", &p.velocityMatrix.ampRelease },
        { "vel.bass", &p.velocityMatrix.bass },
        { "vel.treble", &p.velocityMatrix.treble },
        { "vel.multiFx", &p.velocityMatrix.multiFx },
        { "vel.delayTime", &p.velocityMatrix.delayTime },
        { "vel.delayFeedback", &p.velocityMatrix.delayFeedback },
        { "vel.delayLevel", &p.velocityMatrix.delayLevel },
        { "vel.portamento", &p.velocityMatrix.portamento } };
    if (const auto it = ints.find (name); it != ints.end())
        *it->second = v;
    else if (name == "osc1.wave")
        p.osc1.wave = static_cast<Wave1> (v);
    else if (name == "filter.type")
        p.filter.type = static_cast<FilterType> (v);
    else if (name == "filter.steep")
        p.filter.steep = v != 0;
    else if (name == "mono")
        p.mono = v != 0;
    else if (name == "legato")
        p.legato = v != 0;
    else if (name == "portamento")
        p.portamento = v != 0;
    else if (name == "lfo1.wave")
        p.lfo1.wave = static_cast<LfoWave> (v);
    else if (name == "osc2.wave")
        p.osc2.wave = static_cast<Wave2> (v);
    else if (name == "pan")
        p.pan = static_cast<PanMode> (v);
    else if (name == "chorus.type")
        p.effects.chorusType = static_cast<ChorusType> (v);
    else if (name == "delay.type")
        p.effects.delayType = static_cast<DelayType> (v);
    else if (name == "osc2.sync")
        p.osc2.sync = v != 0;
    else if (name == "dest")
        p.destination = static_cast<ModDestination> (v);
    else if (name == "unison")
        p.unison = v != 0;
    else if (name == "ring")
        p.ring = v != 0;
    else if (name == "vel.on")
        p.velocityMatrix.on = v != 0;
    else
        return false;
    return true;
}
} // namespace

int main (int argc, char** argv)
{
    if (argc != 5 && argc != 6)
    {
        std::fprintf (stderr, "usage: TranceraRenderEvents <events.txt> <out.f32> <rate> <seconds> [stereo]\n");
        return 2;
    }
    const double rate = std::stod (argv[3]);
    const int frames = static_cast<int> (std::stod (argv[4]) * rate);
    auto engine = std::make_unique<Engine>();
    engine->prepare (rate);
    Patch patch;
    std::ifstream in (argv[1]);
    std::string line;
    std::vector<float> l (static_cast<std::size_t> (frames)), r (l.size());
    int at = 0;
    const auto until = [&] (int end)
    {
        for (end = std::min (end, frames); at < end;)
        {
            const int n = std::min (256, end - at);
            engine->render (l.data() + at, r.data() + at, n);
            at += n;
        }
    };
    while (std::getline (in, line))
    {
        if (line.empty() || line[0] == '#')
            continue;
        std::istringstream words (line);
        double t;
        std::string kind;
        words >> t >> kind;
        until (static_cast<int> (t * rate + 0.5));
        if (kind == "note")
        {
            int key, velocity;
            words >> key >> velocity;
            engine->noteOn (key, velocity);
        }
        else if (kind == "off")
        {
            int key;
            words >> key;
            engine->noteOff (key);
        }
        else if (kind == "set")
        {
            std::string name;
            int v;
            words >> name >> v;
            if (! set (patch, name, v))
            {
                std::fprintf (stderr, "unknown parameter %s\n", name.c_str());
                return 1;
            }
            engine->setPatch (Part::Upper, patch);
        }
        else if (kind == "mod")
        {
            double v;
            words >> v;
            engine->modWheel (v / 127.0);
        }
        else if (kind == "aftertouch")
        {
            double v;
            words >> v;
            engine->aftertouch (v / 127.0);
        }
        else if (kind == "tempo")
        {
            double bpm;
            words >> bpm;
            engine->setTempo (bpm);
        }
        else if (kind == "node")
        {
            std::string node;
            words >> node;
            engine->setOutputNode (node == "printed" ? OutputNode::KeyboardAsPrinted
                                   : node == "digital" ? OutputNode::Digital : OutputNode::Rack);
        }
        else if (kind == "analog")
        {
            int on;
            words >> on;
            engine->setAnalogColor (on != 0);
        }
        else
        {
            std::fprintf (stderr, "unknown event %s\n", kind.c_str());
            return 1;
        }
    }
    until (frames);
    std::FILE* out = std::fopen (argv[2], "wb");
    if (out == nullptr)
        return 1;
    if (argc > 5 && std::string (argv[5]) == "stereo")
        for (std::size_t i = 0; i < l.size(); ++i)
        {
            std::fwrite (&l[i], sizeof (float), 1, out);
            std::fwrite (&r[i], sizeof (float), 1, out);
        }
    else
        std::fwrite (l.data(), sizeof (float), l.size(), out);
    return std::fclose (out) == 0 ? 0 : 1;
}
