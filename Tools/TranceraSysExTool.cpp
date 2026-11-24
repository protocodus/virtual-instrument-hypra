#include "../Source/DSP/TranceraControls.h"
#include "../Source/DSP/TranceraSysEx.h"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <map>
#include <sstream>
#include <string>
#include <string_view>
#include <vector>

namespace fs = std::filesystem;
using namespace trancera;
using namespace trancera::sysex;

namespace
{
std::string formatRepatchNumber (const ControlInfo& info, int value)
{
    if (info.steps > 0)
        return std::to_string (value - info.min);
    const double norm = static_cast<double> (value - info.min) / static_cast<double> (info.max - info.min);
    const double rounded = std::round (norm * 1e12) / 1e12 + 0.0;
    char buf[64] {};
    std::snprintf (buf, sizeof (buf), "%.12f", rounded);
    std::string s (buf);
    while (! s.empty() && s.back() == '0')
        s.pop_back();
    if (! s.empty() && s.back() == '.')
        s.pop_back();
    if (s.empty() || s == "-0")
        s = "0";
    return s;
}

std::string encodeRepatchXml (const Patch& patch, std::string_view deviceVersion, bool dacOn)
{
    std::ostringstream oss;
    oss << "<?xml version=\"1.0\"?>\n"
        << "<JukeboxPatch version=\"1.0\">\n"
        << "  <DeviceNameInEnglish>Trancera Virtual Analog Synth</DeviceNameInEnglish>\n"
        << "  <Properties deviceProductID=\"cz.protocodus.Trancera\" deviceVersion=\"" << deviceVersion << "\">\n"
        << "    <Object name=\"custom_properties\">\n";
    for (int i = 0; i < kControlCount; ++i)
    {
        const ControlInfo& info = kControls[static_cast<std::size_t> (i)];
        const int v = controlValue (patch, i);
        oss << "      <Value property=\"" << info.id << "\" type=\"number\">"
            << formatRepatchNumber (info, v) << "</Value>\n";
    }
    oss << "      <Value property=\"analog_color\" type=\"number\">" << (dacOn ? "1" : "0") << "</Value>\n"
        << "    </Object>\n"
        << "  </Properties>\n"
        << "</JukeboxPatch>\n";
    return oss.str();
}

std::string extractAttr (std::string_view tag, std::string_view attrName)
{
    const std::string needle = std::string (attrName) + "=\"";
    const auto p = tag.find (needle);
    if (p == std::string_view::npos)
        return {};
    const auto start = p + needle.size();
    const auto end = tag.find ('"', start);
    if (end == std::string_view::npos)
        return {};
    return std::string (tag.substr (start, end - start));
}

bool decodeRepatchXml (std::string_view xml, Patch& outPatch, bool& outOk)
{
    outOk = false;
    outPatch = Patch {};

    std::map<std::string, int> byId;
    for (int i = 0; i < kControlCount; ++i)
        byId.emplace (kControls[static_cast<std::size_t> (i)].id, i);

    int matchedControls = 0;
    std::size_t pos = 0;
    while (pos < xml.size())
    {
        const auto open = xml.find ("<Value", pos);
        if (open == std::string_view::npos)
            break;
        const auto gt = xml.find ('>', open);
        if (gt == std::string_view::npos)
            break;
        if (gt > open && xml[gt - 1] == '/')
        {
            pos = gt + 1;
            continue;
        }
        const auto close = xml.find ("</Value>", gt + 1);
        if (close == std::string_view::npos)
            break;

        const std::string_view tag = xml.substr (open, gt - open + 1);
        const std::string prop = extractAttr (tag, "property");
        const std::string type = extractAttr (tag, "type");
        if (! prop.empty() && (type.empty() || type == "number"))
        {
            const std::string valStr (xml.substr (gt + 1, close - (gt + 1)));
            char* endPtr = nullptr;
            const double val = std::strtod (valStr.c_str(), &endPtr);
            if (endPtr != valStr.c_str() && std::isfinite (val))
            {
                if (auto it = byId.find (prop); it != byId.end())
                {
                    const int idx = it->second;
                    const ControlInfo& info = kControls[static_cast<std::size_t> (idx)];
                    const int decoded = info.steps > 0
                        ? info.min + static_cast<int> (std::clamp (std::floor (val + 0.5), 0.0, static_cast<double> (info.steps - 1)))
                        : info.min + static_cast<int> (std::floor (std::clamp (val, 0.0, 1.0) * static_cast<double> (info.max - info.min) + 0.5));
                    applyControl (outPatch, idx, decoded);
                    ++matchedControls;
                }
            }
        }
        pos = close + 8;
    }
    outOk = matchedControls > 0;
    return outOk;
}

std::vector<std::uint8_t> readBinaryFile (const fs::path& p, std::string& err)
{
    std::ifstream f (p, std::ios::binary | std::ios::ate);
    if (! f)
    {
        err = "cannot open file: " + p.string();
        return {};
    }
    const auto sz = f.tellg();
    if (sz < 0)
    {
        err = "cannot size file: " + p.string();
        return {};
    }
    std::vector<std::uint8_t> buf (static_cast<std::size_t> (sz));
    f.seekg (0);
    if (sz > 0 && ! f.read (reinterpret_cast<char*> (buf.data()), sz))
    {
        err = "read error: " + p.string();
        return {};
    }
    return buf;
}

std::string sanitizeFilename (std::string_view name)
{
    std::string out;
    out.reserve (name.size());
    for (const char ch : name)
    {
        const auto uc = static_cast<unsigned char> (ch);
        if (uc < 0x20 || ch == '/' || ch == '\\' || ch == ':' || ch == '*' || ch == '?'
            || ch == '"' || ch == '<' || ch == '>' || ch == '|')
            out.push_back ('_');
        else
            out.push_back (ch);
    }
    while (! out.empty() && (out.back() == ' ' || out.back() == '.'))
        out.pop_back();
    while (! out.empty() && out.front() == ' ')
        out.erase (out.begin());
    return out.empty() ? std::string ("Patch") : out;
}

bool parseSlotArg (const std::string& s, SlotKind& kind, int& index)
{
    if (s == "temp" || s == "temp-upper") { kind = SlotKind::TempUpper; index = 0; return true; }
    if (s == "temp-lower") { kind = SlotKind::TempLower; index = 0; return true; }
    if (s == "bank") { kind = SlotKind::UserPatch; index = -1; return true; }
    // Accepts A11..B88 or U-A11..U-B88
    std::string t = s;
    for (char& ch : t)
        ch = static_cast<char> (std::toupper (static_cast<unsigned char> (ch)));
    if (t.rfind ("U-", 0) == 0 || t.rfind ("U:", 0) == 0)
        t = t.substr (2);
    if (t.size() == 3 && (t[0] == 'A' || t[0] == 'B')
        && t[1] >= '1' && t[1] <= '8' && t[2] >= '1' && t[2] <= '8')
    {
        kind = SlotKind::UserPatch;
        index = (t[0] == 'B' ? 64 : 0) + (t[1] - '1') * 8 + (t[2] - '1');
        return true;
    }
    return false;
}

void printUsage()
{
    std::cerr <<
        "TranceraSysEx — JP-8000 / JP-8080 .syx/.mid <-> Reason .repatch converter\n\n"
        "Usage:\n"
        "  TranceraSysEx info <input.syx|.mid> [--lenient]\n"
        "  TranceraSysEx to-repatch <input.syx|.mid> <out.repatch|out_dir> [options]\n"
        "      --version VER    deviceVersion attribute (default 0.9.6d1)\n"
        "      --dac on|off     analog_color property value (default on)\n"
        "      --all-parts      also export Lower parts from performance dumps\n"
        "      --lenient        import packets even if checksum fails\n"
        "  TranceraSysEx to-syx <in.repatch|in_dir>... <out.syx> [options]\n"
        "      --format jp8080|jp8000  target hardware SysEx format (default jp8080)\n"
        "      --device-id N           Roland Device ID 17..32 or 0x10..0x1F (default 17)\n"
        "      --slot SLOT             temp | bank | A11..B88 (default temp for 1, bank for >1)\n"
        "      --name TEXT             override 16-char patch name for single-file export\n"
        "      --flat                  do not split 248-byte JP-8080 dumps at 242 bytes\n";
}
} // namespace

int main (int argc, char** argv)
{
    if (argc < 2)
    {
        printUsage();
        return 1;
    }

    const std::string cmd = argv[1];
    if (cmd == "-h" || cmd == "--help")
    {
        printUsage();
        return 0;
    }

    if (cmd == "info")
    {
        if (argc < 3) { printUsage(); return 1; }
        bool lenient = false;
        fs::path input;
        for (int i = 2; i < argc; ++i)
        {
            const std::string arg = argv[i];
            if (arg == "--lenient") lenient = true;
            else if (input.empty()) input = arg;
            else { std::cerr << "Unexpected argument: " << arg << "\n"; return 1; }
        }
        std::string err;
        const auto bytes = readBinaryFile (input, err);
        if (! err.empty()) { std::cerr << err << "\n"; return 1; }
        const ParseResult res = parseStream (bytes.data(), bytes.size(), ! lenient);
        std::cout << "File: " << input.string() << "\n"
                  << "Packets parsed: " << res.messagesParsed
                  << ", bad checksums: " << res.badChecksums
                  << ", ignored: " << res.ignoredMessages
                  << ", patches: " << res.patches.size() << "\n";
        for (std::size_t i = 0; i < res.patches.size(); ++i)
        {
            const PatchRecord& rec = res.patches[i];
            std::cout << "  [" << (i + 1) << "] " << formatSlotLabel (rec.slotKind, rec.slotIndex)
                      << " | " << (rec.detectedFormat == Format::JP8080 ? "JP-8080" : "JP-8000")
                      << " | \"" << rec.name << "\"";
            if (rec.warnings.hasVelocityMatrix) std::cout << " [VelMatrix]";
            if (rec.warnings.osc2NoiseMappedToTriangle) std::cout << " [Osc2:Noise->Tri]";
            if (rec.warnings.distortionMappedToChorus) std::cout << " [FX:Dist->Chorus]";
            if (rec.warnings.checksumErrors) std::cout << " [ChecksumError]";
            std::cout << "\n";
        }
        return res.patches.empty() ? 2 : 0;
    }

    if (cmd == "to-repatch")
    {
        fs::path input, output;
        std::string version = "0.9.6d1";
        bool dacOn = true;
        bool allParts = false;
        bool lenient = false;

        for (int i = 2; i < argc; ++i)
        {
            const std::string arg = argv[i];
            if (arg == "--version" && i + 1 < argc) version = argv[++i];
            else if (arg == "--dac" && i + 1 < argc) { const std::string v = argv[++i]; dacOn = (v != "off" && v != "0" && v != "false"); }
            else if (arg == "--all-parts") allParts = true;
            else if (arg == "--lenient") lenient = true;
            else if (input.empty()) input = arg;
            else if (output.empty()) output = arg;
            else { std::cerr << "Unknown or duplicate argument: " << arg << "\n"; return 1; }
        }
        if (input.empty() || output.empty()) { printUsage(); return 1; }

        std::string err;
        const auto bytes = readBinaryFile (input, err);
        if (! err.empty()) { std::cerr << err << "\n"; return 1; }
        const ParseResult res = parseStream (bytes.data(), bytes.size(), ! lenient);
        if (res.patches.empty())
        {
            std::cerr << "No JP-8000/JP-8080 patch dumps found in " << input.string()
                      << " (bad checksums: " << res.badChecksums << ")\n";
            return 2;
        }

        const bool hasUpper = std::any_of (res.patches.begin(), res.patches.end(), [] (const PatchRecord& r) {
            return r.slotKind != SlotKind::TempLower && r.slotKind != SlotKind::PerfLower;
        });
        std::vector<PatchRecord> selected;
        for (PatchRecord rec : res.patches)
        {
            const bool isLower = rec.slotKind == SlotKind::TempLower || rec.slotKind == SlotKind::PerfLower;
            if (isLower && hasUpper && ! allParts)
                continue;
            bakeVelocityMatrixToControls (rec.patch);
            selected.push_back (std::move (rec));
        }

        const bool singleFileTarget = selected.size() == 1 && output.extension() == ".repatch";
        if (singleFileTarget)
        {
            if (output.has_parent_path())
                fs::create_directories (output.parent_path());
            std::ofstream out (output, std::ios::binary);
            if (! out) { std::cerr << "Cannot write " << output.string() << "\n"; return 1; }
            out << encodeRepatchXml (selected[0].patch, version, dacOn);
            std::cout << "Wrote " << output.string() << " (" << selected[0].name << ")\n";
            return 0;
        }

        fs::create_directories (output);
        std::map<std::string, int> usedNames;
        int written = 0;
        for (const PatchRecord& rec : selected)
        {
            std::string base = sanitizeFilename (rec.name);
            if (selected.size() > 1 && (rec.slotKind == SlotKind::UserPatch || rec.slotKind == SlotKind::TempLower || rec.slotKind == SlotKind::PerfLower))
            {
                if (rec.slotKind == SlotKind::TempLower || rec.slotKind == SlotKind::PerfLower)
                    base += " (" + formatSlotLabel (rec.slotKind, rec.slotIndex) + ")";
            }
            std::string stem = base;
            const int n = ++usedNames[stem];
            if (n > 1)
                stem += " (" + formatSlotLabel (rec.slotKind, rec.slotIndex) + "_" + std::to_string (n) + ")";
            const fs::path dst = output / (stem + ".repatch");
            std::ofstream out (dst, std::ios::binary);
            if (! out) { std::cerr << "Cannot write " << dst.string() << "\n"; return 1; }
            out << encodeRepatchXml (rec.patch, version, dacOn);
            ++written;
        }
        std::cout << "Wrote " << written << " .repatch file(s) to " << output.string() << "\n";
        return 0;
    }

    if (cmd == "to-syx")
    {
        Format format = Format::JP8080;
        std::uint8_t deviceId = kDefaultDeviceId;
        bool slotExplicit = false;
        SlotKind slot = SlotKind::TempUpper;
        int slotIndex = 0;
        std::string overrideName;
        bool hardwareSplit = true;
        std::vector<fs::path> positional;

        for (int i = 2; i < argc; ++i)
        {
            const std::string arg = argv[i];
            if (arg == "--format" && i + 1 < argc)
            {
                const std::string f = argv[++i];
                if (f == "jp8000" || f == "8000") format = Format::JP8000;
                else if (f == "jp8080" || f == "8080") format = Format::JP8080;
                else { std::cerr << "Unknown format: " << f << "\n"; return 1; }
            }
            else if (arg == "--device-id" && i + 1 < argc)
            {
                const std::string val = argv[++i];
                const bool isHex = val.rfind ("0x", 0) == 0 || val.rfind ("0X", 0) == 0;
                const int raw = std::stoi (val, nullptr, isHex ? 16 : 10);
                if (! isHex && raw >= 17 && raw <= 32) deviceId = static_cast<std::uint8_t> (0x10 + (raw - 17));
                else if (isHex && raw >= 0x10 && raw <= 0x1F) deviceId = static_cast<std::uint8_t> (raw);
                else { std::cerr << "Device ID must be 17..32 or 0x10..0x1F\n"; return 1; }
            }
            else if (arg == "--slot" && i + 1 < argc)
            {
                if (! parseSlotArg (argv[++i], slot, slotIndex))
                {
                    std::cerr << "Invalid slot (expected temp, bank, or A11..B88)\n";
                    return 1;
                }
                slotExplicit = true;
            }
            else if (arg == "--name" && i + 1 < argc)
                overrideName = argv[++i];
            else if (arg == "--flat")
                hardwareSplit = false;
            else
                positional.emplace_back (arg);
        }

        if (positional.size() < 2) { printUsage(); return 1; }
        const fs::path output = positional.back();
        positional.pop_back();

        std::vector<fs::path> inputs;
        for (const fs::path& in : positional)
        {
            if (fs::is_directory (in))
            {
                std::vector<fs::path> dirFiles;
                for (const auto& entry : fs::directory_iterator (in))
                    if (entry.is_regular_file() && entry.path().extension() == ".repatch")
                        dirFiles.push_back (entry.path());
                std::sort (dirFiles.begin(), dirFiles.end());
                inputs.insert (inputs.end(), dirFiles.begin(), dirFiles.end());
            }
            else
            {
                inputs.push_back (in);
            }
        }
        if (inputs.empty()) { std::cerr << "No .repatch files found\n"; return 1; }

        std::vector<std::pair<std::string, Patch>> patches;
        for (const fs::path& in : inputs)
        {
            std::string err;
            const auto raw = readBinaryFile (in, err);
            if (! err.empty()) { std::cerr << err << "\n"; return 1; }
            const std::string xml (raw.begin(), raw.end());
            Patch p;
            bool ok = false;
            if (! decodeRepatchXml (xml, p, ok))
            {
                std::cerr << "No valid Trancera properties in " << in.string() << "\n";
                return 1;
            }
            const std::string name = (! overrideName.empty() && inputs.size() == 1)
                ? overrideName
                : in.stem().string();
            patches.emplace_back (sanitizePatchName (name), p);
        }

        std::vector<std::uint8_t> syx;
        if (patches.size() == 1 && (! slotExplicit || slot != SlotKind::UserPatch || slotIndex >= 0))
        {
            if (slotExplicit && slot == SlotKind::UserPatch && slotIndex < 0)
                syx = encodeUserBank (patches, format, deviceId, hardwareSplit);
            else
                syx = encodePatch (patches[0].second, patches[0].first, format, slot, std::max (0, slotIndex), deviceId, hardwareSplit);
        }
        else
        {
            if (patches.size() > kMaxUserPatches)
            {
                std::cerr << "Warning: truncating " << patches.size() << " patches to 128 bank slots (U:A11..U:B88)\n";
                patches.resize (kMaxUserPatches);
            }
            syx = encodeUserBank (patches, format, deviceId, hardwareSplit);
        }

        if (output.has_parent_path())
            fs::create_directories (output.parent_path());
        std::ofstream out (output, std::ios::binary);
        if (! out) { std::cerr << "Cannot write " << output.string() << "\n"; return 1; }
        out.write (reinterpret_cast<const char*> (syx.data()), static_cast<std::streamsize> (syx.size()));
        std::cout << "Wrote " << patches.size() << " patch(es) (" << syx.size() << " bytes) to " << output.string() << "\n";
        return 0;
    }

    printUsage();
    return 1;
}
