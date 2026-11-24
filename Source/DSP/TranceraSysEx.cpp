#include "TranceraSysEx.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <map>
#include <tuple>

namespace trancera::sysex
{
namespace
{
int clampByte (int v, int lo, int hi) noexcept
{
    return std::clamp (v, lo, hi);
}

int decodeTwoByte (std::uint8_t msb, std::uint8_t lsb, int lo = -127, int hi = 127) noexcept
{
    const int raw = (static_cast<int> (msb & 0x01u) << 7) | static_cast<int> (lsb & 0x7Fu);
    return clampByte (raw - 127, lo, hi);
}

void encodeTwoByte (std::uint8_t* out, int offset, int val, int lo = -127, int hi = 127) noexcept
{
    const int raw = clampByte (val, lo, hi) + 127;
    out[offset] = static_cast<std::uint8_t> ((raw >> 7) & 0x01);
    out[offset + 1] = static_cast<std::uint8_t> (raw & 0x7F);
}

int mapBeatPatternToArpRate (int pattern) noexcept
{
    switch (pattern)
    {
        case 0: return 0; // NORMAL 1/4
        case 1: return 2; // NORMAL 1/6 (1/4T)
        case 2: return 3; // NORMAL 1/8
        case 3: return 4; // NORMAL 1/12 (1/8T)
        case 4: return 5; // NORMAL 1/16
        case 5: return 7; // NORMAL 1/32
        default: return 5;
    }
}

int mapHardwareDelaySync (int hw) noexcept
{
    static constexpr std::array<int, 11> kMap { 0, 3, 4, 5, 6, 7, 8, 9, 10, 10, 11 };
    if (hw < 0)
        return 0;
    if (hw >= static_cast<int> (kMap.size()))
        return 11;
    return kMap[static_cast<std::size_t> (hw)];
}

void computePatchBaseAddress (SlotKind slot, int slotIndex, std::uint8_t a[4]) noexcept
{
    switch (slot)
    {
        case SlotKind::TempUpper:
            a[0] = 0x01; a[1] = 0x00; a[2] = 0x40; a[3] = 0x00;
            break;
        case SlotKind::TempLower:
            a[0] = 0x01; a[1] = 0x00; a[2] = 0x42; a[3] = 0x00;
            break;
        case SlotKind::UserPatch:
        {
            const int idx = std::clamp (slotIndex, 0, static_cast<int> (kMaxUserPatches - 1));
            a[0] = 0x02;
            a[1] = static_cast<std::uint8_t> (idx < 64 ? 0x00 : 0x01);
            a[2] = static_cast<std::uint8_t> ((idx & 63) * 2);
            a[3] = 0x00;
            break;
        }
        case SlotKind::PerfUpper:
        case SlotKind::PerfLower:
        {
            const int perfIdx = std::clamp (slotIndex, 0, 63);
            a[0] = 0x03;
            a[1] = static_cast<std::uint8_t> (perfIdx);
            a[2] = static_cast<std::uint8_t> (slot == SlotKind::PerfUpper ? 0x40 : 0x42);
            a[3] = 0x00;
            break;
        }
    }
}

void appendDt1Packet (std::vector<std::uint8_t>& out, std::uint8_t deviceId,
                      const std::uint8_t addr[4], const std::uint8_t* payload, std::size_t len)
{
    const std::uint8_t dev = (deviceId >= 0x10 && deviceId <= 0x1F) ? deviceId : kDefaultDeviceId;
    out.push_back (kStatusSysEx);
    out.push_back (kManufacturerRoland);
    out.push_back (dev);
    out.push_back (kModelMsb);
    out.push_back (kModelLsb);
    out.push_back (kCmdDataSet1);

    int sum = 0;
    for (int i = 0; i < 4; ++i)
    {
        const std::uint8_t b = static_cast<std::uint8_t> (addr[i] & 0x7Fu);
        out.push_back (b);
        sum += b;
    }
    for (std::size_t i = 0; i < len; ++i)
    {
        const std::uint8_t b = static_cast<std::uint8_t> (payload[i] & 0x7Fu);
        out.push_back (b);
        sum += b;
    }
    out.push_back (static_cast<std::uint8_t> ((128 - (sum & 0x7F)) & 0x7F));
    out.push_back (kStatusEOX);
}

struct PerfOverlay
{
    bool hasCommon { false };
    int arpDest { 0 };
    bool arpOn { false };
    int arpMode { 0 };
    int arpBeatPattern { 4 };
    int arpRange { 0 };
    bool arpHold { false };

    bool hasUpperDelaySync { false };
    int upperDelaySync { 0 };
    bool hasLowerDelaySync { false };
    int lowerDelaySync { 0 };
};

struct RawSlotBuffer
{
    SlotKind kind { SlotKind::TempUpper };
    int slotIndex { 0 };
    std::array<std::uint8_t, kRackPatchBytes> bytes {};
    std::array<bool, kRackPatchBytes> present {};
    bool checksumError { false };
    std::size_t order { 0 };
};

PatchRecord decodeSlotBuffer (const RawSlotBuffer& slot, const PerfOverlay* overlay)
{
    PatchRecord rec;
    rec.slotKind = slot.kind;
    rec.slotIndex = slot.slotIndex;
    rec.warnings.checksumErrors = slot.checksumError;

    // Detect JP-8080 if upper bytes beyond 238 were transmitted (01 6F..01 77)
    // or if OSC2 Waveform == Noise (3) or MultiFX == Distortion (12).
    bool hasRackBytes = false;
    for (std::size_t i = kKeyboardPatchBytes; i < kRackPatchBytes; ++i)
    {
        if (slot.present[i])
        {
            hasRackBytes = true;
            break;
        }
    }

    // Read up to 16 ASCII characters (0x00..0x0F).
    std::string rawName (16, ' ');
    for (std::size_t i = 0; i < 16; ++i)
    {
        if (slot.present[i])
        {
            const std::uint8_t ch = slot.bytes[i];
            rawName[i] = (ch >= 0x20 && ch <= 0x7D) ? static_cast<char> (ch) : ' ';
        }
    }
    while (! rawName.empty() && rawName.back() == ' ')
        rawName.pop_back();
    if (rawName.empty())
        rawName = "Untitled";
    rec.name = rawName;

    Patch& p = rec.patch;
    const auto b = [&] (int off, int fallback = 0) noexcept -> int
    {
        if (off >= 0 && off < static_cast<int> (kRackPatchBytes) && slot.present[static_cast<std::size_t> (off)])
            return static_cast<int> (slot.bytes[static_cast<std::size_t> (off)]);
        return fallback;
    };

    p.lfo1.wave = static_cast<LfoWave> (clampByte (b (0x10, 0), 0, 3));
    p.lfo1.rate = clampByte (b (0x11, 48), 0, 127);
    p.lfo1.fade = clampByte (b (0x12, 0), 0, 127);
    p.lfo2.rate = clampByte (b (0x13, 60), 0, 127);

    p.ring = b (0x15, 0) != 0;
    p.xmod = clampByte (b (0x16, 0), 0, 127);
    p.balance = clampByte (b (0x17, 0) - 64, -64, 63);
    p.destination = static_cast<ModDestination> (clampByte (b (0x18, 0), 0, 2));
    p.pitchLfo = clampByte (b (0x19, 64) - 64, -64, 63);
    p.lfo2.pitch = clampByte (b (0x1A, 72) - 64, -64, 63);
    p.pitchEnvDepth = clampByte (b (0x1B, 64) - 64, -64, 63);
    p.pitchAttack = clampByte (b (0x1C, 0), 0, 127);
    p.pitchDecay = clampByte (b (0x1D, 40), 0, 127);

    p.osc1.wave = static_cast<Wave1> (clampByte (b (0x1E, 0), 0, 6));
    p.osc1.ctrl1 = clampByte (b (0x1F, 0), 0, 127);
    p.osc1.ctrl2 = clampByte (b (0x20, 0), 0, 127);

    const int rawOsc2Wave = clampByte (b (0x21, 1), 0, 3);
    if (rawOsc2Wave == 3)
    {
        p.osc2.wave = Wave2::Triangle;
        rec.warnings.osc2NoiseMappedToTriangle = true;
        hasRackBytes = true;
    }
    else
    {
        p.osc2.wave = static_cast<Wave2> (rawOsc2Wave);
    }
    p.osc2.sync = b (0x22, 0) != 0;
    p.osc2.range = clampByte (b (0x23, 25) - 25, -25, 25);
    p.osc2.fine = clampByte (b (0x24, 50) - 50, -50, 50);
    p.osc2.pulseWidth = clampByte (b (0x25, 0), 0, 127);
    p.osc2.pwmDepth = clampByte (b (0x26, 0), 0, 127);

    p.filter.type = static_cast<FilterType> (clampByte (b (0x27, 2), 0, 2));
    p.filter.steep = b (0x28, 1) != 0;
    p.filter.cutoff = clampByte (b (0x29, 127), 0, 127);
    p.filter.resonance = clampByte (b (0x2A, 0), 0, 127);
    p.filter.keyFollow = clampByte (b (0x2B, 64) - 64, -64, 63);
    p.filter.lfoDepth = clampByte (b (0x2C, 64) - 64, -64, 63);
    p.lfo2.filter = clampByte (b (0x2D, 64) - 64, -64, 63);
    p.filter.envDepth = clampByte (b (0x2E, 64) - 64, -64, 63);
    p.filter.env.attack = clampByte (b (0x2F, 0), 0, 127);
    p.filter.env.decay = clampByte (b (0x30, 64), 0, 127);
    p.filter.env.sustain = clampByte (b (0x31, 127), 0, 127);
    p.filter.env.release = clampByte (b (0x32, 5), 0, 127);

    p.level = clampByte (b (0x33, 100), 0, 127);
    p.ampLfo = clampByte (b (0x34, 64) - 64, -64, 63);
    p.lfo2.amp = clampByte (b (0x35, 64) - 64, -64, 63);
    p.amp.attack = clampByte (b (0x36, 0), 0, 127);
    p.amp.decay = clampByte (b (0x37, 64), 0, 127);
    p.amp.sustain = clampByte (b (0x38, 127), 0, 127);
    p.amp.release = clampByte (b (0x39, 5), 0, 127);
    p.pan = static_cast<PanMode> (clampByte (b (0x3A, 0), 0, 2));

    p.effects.bass = clampByte (b (0x3B, 64) - 64, -64, 63);
    p.effects.treble = clampByte (b (0x3C, 64) - 64, -64, 63);
    const int rawFxType = clampByte (b (0x3D, 0), 0, 12);
    if (rawFxType == 12)
    {
        p.effects.chorusType = ChorusType::SuperSlow;
        rec.warnings.distortionMappedToChorus = true;
        hasRackBytes = true;
    }
    else
    {
        p.effects.chorusType = static_cast<ChorusType> (rawFxType);
    }
    p.effects.chorus = clampByte (b (0x3E, 0), 0, 127);

    p.effects.delayType = static_cast<DelayType> (clampByte (b (0x3F, 0), 0, 4));
    p.effects.delayTime = clampByte (b (0x40, 64), 0, 127);
    p.effects.feedback = clampByte (b (0x41, 40), 0, 127);
    p.effects.delay = clampByte (b (0x42, 0), 0, 127);

    p.bendUp = clampByte (b (0x43, 2), 0, 24);
    p.bendDown = clampByte (b (0x44, 2), 0, 24);
    p.portamento = b (0x45, 0) != 0;
    p.portamentoTime = clampByte (b (0x46, 11), 0, 127);
    p.mono = b (0x47, 0) != 0;
    p.legato = b (0x48, 0) != 0;
    p.oscShift = clampByte (b (0x49, 2) - 2, -2, 2);

    // Velocity Matrix: Roland offset 0x011B (linear 155) + 40 two-byte destinations 0x011C..0x016A (156..235).
    constexpr int kVelSwitchOff = linearOffset (0x01, 0x1B); // 155
    p.velocityMatrix.on = b (kVelSwitchOff, 0) != 0;

    const auto readVel = [&] (int fieldIndex, int lo = -127, int hi = 127) noexcept -> int
    {
        const int off = linearOffset (0x01, 0x1C) + fieldIndex * 2;
        if (! slot.present[static_cast<std::size_t> (off)] || ! slot.present[static_cast<std::size_t> (off + 1)])
            return 0;
        return decodeTwoByte (slot.bytes[static_cast<std::size_t> (off)],
                              slot.bytes[static_cast<std::size_t> (off + 1)], lo, hi);
    };

    p.velocityMatrix.lfo1Rate = readVel (0);
    p.velocityMatrix.lfo1Fade = readVel (1);
    p.velocityMatrix.lfo2Rate = readVel (2);
    p.velocityMatrix.xmod = readVel (3);
    p.velocityMatrix.balance = readVel (4);
    p.velocityMatrix.pitchLfo = readVel (5);
    p.velocityMatrix.pitchLfo2 = readVel (6);
    p.velocityMatrix.pitchEnvDepth = readVel (7);
    p.velocityMatrix.pitchAttack = readVel (8);
    p.velocityMatrix.pitchDecay = readVel (9);
    p.velocityMatrix.osc1Ctrl1 = readVel (10);
    p.velocityMatrix.osc1Ctrl2 = readVel (11);
    p.velocityMatrix.osc2Range = readVel (12, -50, 50);
    p.velocityMatrix.osc2Fine = readVel (13, -100, 100);
    p.velocityMatrix.osc2Ctrl1 = readVel (14);
    p.velocityMatrix.osc2Ctrl2 = readVel (15);
    p.velocityMatrix.cutoff = readVel (16);
    p.velocityMatrix.resonance = readVel (17);
    p.velocityMatrix.keyFollow = readVel (18);
    p.velocityMatrix.filterLfo = readVel (19);
    p.velocityMatrix.filterLfo2 = readVel (20);
    p.velocityMatrix.envDepth = readVel (21);
    p.velocityMatrix.filterAttack = readVel (22);
    p.velocityMatrix.filterDecay = readVel (23);
    p.velocityMatrix.filterSustain = readVel (24);
    p.velocityMatrix.filterRelease = readVel (25);
    p.velocityMatrix.level = readVel (26);
    p.velocityMatrix.ampLfo = readVel (27);
    p.velocityMatrix.ampLfo2 = readVel (28);
    p.velocityMatrix.ampAttack = readVel (29);
    p.velocityMatrix.ampDecay = readVel (30);
    p.velocityMatrix.ampSustain = readVel (31);
    p.velocityMatrix.ampRelease = readVel (32);
    p.velocityMatrix.bass = readVel (33);
    p.velocityMatrix.treble = readVel (34);
    p.velocityMatrix.multiFx = readVel (35);
    p.velocityMatrix.delayTime = readVel (36);
    p.velocityMatrix.delayFeedback = readVel (37);
    p.velocityMatrix.delayLevel = readVel (38);
    p.velocityMatrix.portamento = readVel (39);

    rec.warnings.hasVelocityMatrix = p.velocityMatrix.on;

    // JP-8080 Rack extensions (0x0173 = linear 243 Unison, 0x0174 = linear 244 UnisonDetune).
    constexpr int kUnisonOff = linearOffset (0x01, 0x73);       // 243
    constexpr int kUnisonDetuneOff = linearOffset (0x01, 0x74); // 244
    if (slot.present[static_cast<std::size_t> (kUnisonOff)])
        p.unison = slot.bytes[static_cast<std::size_t> (kUnisonOff)] != 0;
    if (slot.present[static_cast<std::size_t> (kUnisonDetuneOff)])
        p.unisonDetune = clampByte (static_cast<int> (slot.bytes[static_cast<std::size_t> (kUnisonDetuneOff)]), 0, 50);

    // Performance Common & Part overlay if matching performance headers accompanied this slot.
    if (overlay != nullptr)
    {
        const bool isLower = (slot.kind == SlotKind::TempLower || slot.kind == SlotKind::PerfLower);
        if (overlay->hasCommon)
        {
            const bool destMatches = isLower ? (overlay->arpDest == 0 || overlay->arpDest == 1)
                                             : (overlay->arpDest == 0 || overlay->arpDest == 2);
            if (destMatches && overlay->arpOn)
                p.arp.on = true;
            p.arp.mode = static_cast<ArpMode> (clampByte (overlay->arpMode, 0, 4));
            p.arp.rate = mapBeatPatternToArpRate (overlay->arpBeatPattern);
            p.arp.range = clampByte (overlay->arpRange, 0, 3);
            p.arp.hold = overlay->arpHold;
        }
        if (isLower && overlay->hasLowerDelaySync)
            p.effects.delaySync = mapHardwareDelaySync (overlay->lowerDelaySync);
        else if (! isLower && overlay->hasUpperDelaySync)
            p.effects.delaySync = mapHardwareDelaySync (overlay->upperDelaySync);
    }

    rec.detectedFormat = hasRackBytes ? Format::JP8080 : Format::JP8000;
    return rec;
}

std::uint32_t readU32BE (const std::uint8_t* p) noexcept
{
    return (static_cast<std::uint32_t> (p[0]) << 24)
         | (static_cast<std::uint32_t> (p[1]) << 16)
         | (static_cast<std::uint32_t> (p[2]) << 8)
         | static_cast<std::uint32_t> (p[3]);
}

std::size_t readVlq (const std::uint8_t* data, std::size_t size, std::size_t& pos, std::uint32_t& out) noexcept
{
    out = 0;
    std::size_t count = 0;
    while (pos < size && count < 4)
    {
        const std::uint8_t byte = data[pos++];
        ++count;
        out = (out << 7) | static_cast<std::uint32_t> (byte & 0x7Fu);
        if ((byte & 0x80u) == 0)
            return count;
    }
    return 0;
}

std::vector<std::uint8_t> extractSysExFromMidi (const std::uint8_t* data, std::size_t size)
{
    std::vector<std::uint8_t> out;
    if (size < 14 || data[0] != 'M' || data[1] != 'T' || data[2] != 'h' || data[3] != 'd')
        return out;

    const std::uint32_t hdrLen = readU32BE (data + 4);
    std::size_t pos = 8 + static_cast<std::size_t> (hdrLen);
    if (pos > size)
        return out;

    while (pos + 8 <= size)
    {
        const bool isTrack = data[pos] == 'M' && data[pos + 1] == 'T' && data[pos + 2] == 'r' && data[pos + 3] == 'k';
        const std::uint32_t chunkLen = readU32BE (data + pos + 4);
        pos += 8;
        const std::size_t chunkEnd = std::min (size, pos + static_cast<std::size_t> (chunkLen));
        if (! isTrack)
        {
            pos = chunkEnd;
            continue;
        }

        std::uint8_t runningStatus = 0;
        std::vector<std::uint8_t> sysexBuf;
        bool accumulating = false;

        while (pos < chunkEnd)
        {
            std::uint32_t delta = 0;
            if (readVlq (data, chunkEnd, pos, delta) == 0 || pos >= chunkEnd)
                break;

            std::uint8_t status = data[pos];
            if (status == 0xF0 || status == 0xF7)
            {
                ++pos;
                runningStatus = 0;
                std::uint32_t msgLen = 0;
                if (readVlq (data, chunkEnd, pos, msgLen) == 0 || pos + msgLen > chunkEnd)
                    break;
                if (status == 0xF0)
                {
                    sysexBuf.assign (1, 0xF0);
                    sysexBuf.insert (sysexBuf.end(), data + pos, data + pos + msgLen);
                    accumulating = sysexBuf.back() != 0xF7;
                    if (! accumulating)
                        out.insert (out.end(), sysexBuf.begin(), sysexBuf.end());
                }
                else if (accumulating)
                {
                    sysexBuf.insert (sysexBuf.end(), data + pos, data + pos + msgLen);
                    if (! sysexBuf.empty() && sysexBuf.back() == 0xF7)
                    {
                        out.insert (out.end(), sysexBuf.begin(), sysexBuf.end());
                        accumulating = false;
                    }
                }
                pos += msgLen;
            }
            else if (status == 0xFF)
            {
                ++pos;
                runningStatus = 0;
                if (pos >= chunkEnd)
                    break;
                ++pos; // meta type
                std::uint32_t metaLen = 0;
                if (readVlq (data, chunkEnd, pos, metaLen) == 0 || pos + metaLen > chunkEnd)
                    break;
                pos += metaLen;
            }
            else if (status >= 0x80)
            {
                ++pos;
                runningStatus = status;
                const std::uint8_t hi = static_cast<std::uint8_t> (status & 0xF0u);
                const std::size_t dataBytes = (hi == 0xC0 || hi == 0xD0) ? 1u : 2u;
                pos = std::min (chunkEnd, pos + dataBytes);
            }
            else
            {
                if (runningStatus < 0x80)
                    break;
                const std::uint8_t hi = static_cast<std::uint8_t> (runningStatus & 0xF0u);
                const std::size_t dataBytes = (hi == 0xC0 || hi == 0xD0) ? 1u : 2u;
                pos = std::min (chunkEnd, pos + dataBytes);
            }
        }
        pos = chunkEnd;
    }
    return out;
}
} // namespace

std::uint8_t computeChecksum (const std::uint8_t* addrAndData, std::size_t length) noexcept
{
    int sum = 0;
    for (std::size_t i = 0; i < length; ++i)
        sum += addrAndData[i] & 0x7Fu;
    return static_cast<std::uint8_t> ((128 - (sum & 0x7F)) & 0x7F);
}

bool verifyChecksum (const std::uint8_t* addrAndData, std::size_t length, std::uint8_t checksum) noexcept
{
    int sum = checksum & 0x7Fu;
    for (std::size_t i = 0; i < length; ++i)
        sum += addrAndData[i] & 0x7Fu;
    return (sum & 0x7F) == 0;
}

std::string sanitizePatchName (std::string_view name)
{
    std::string out;
    out.reserve (name.size());
    for (const char ch : name)
    {
        const auto uc = static_cast<unsigned char> (ch);
        if (uc >= 0x20 && uc <= 0x7D)
            out.push_back (ch);
        else if (uc >= 0x80)
            out.push_back ('?');
        else
            out.push_back (' ');
    }
    while (! out.empty() && out.back() == ' ')
        out.pop_back();
    if (out.empty())
        out = "Untitled";
    if (out.size() > 16)
        out.resize (16);
    while (! out.empty() && out.back() == ' ')
        out.pop_back();
    return out.empty() ? std::string ("Untitled") : out;
}

std::string formatSlotLabel (SlotKind kind, int slotIndex)
{
    auto twoDigitOctal = [] (int idx)
    {
        const int i = std::clamp (idx, 0, 63);
        return std::to_string (i / 8 + 1) + std::to_string (i % 8 + 1);
    };
    switch (kind)
    {
        case SlotKind::TempUpper: return "Temp Upper";
        case SlotKind::TempLower: return "Temp Lower";
        case SlotKind::UserPatch:
        {
            const int idx = std::clamp (slotIndex, 0, 127);
            const char bank = idx < 64 ? 'A' : 'B';
            return std::string ("U-") + bank + twoDigitOctal (idx & 63);
        }
        case SlotKind::PerfUpper: return "P-" + twoDigitOctal (slotIndex) + " Upper";
        case SlotKind::PerfLower: return "P-" + twoDigitOctal (slotIndex) + " Lower";
    }
    return "Temp Upper";
}

void bakeVelocityMatrixToControls (Patch& patch) noexcept
{
    if (! patch.velocityMatrix.on)
        return;
    if (patch.velocityAmp == 0 && patch.velocityMatrix.level > 0)
        patch.velocityAmp = std::clamp (patch.velocityMatrix.level, 0, 127);
    if (patch.velocityFilter == 0 && patch.velocityMatrix.cutoff != 0)
        patch.velocityFilter = std::clamp (static_cast<int> (std::lround (patch.velocityMatrix.cutoff * 63.0 / 127.0)), -64, 63);
}

std::size_t writePatchBytes (const Patch& patch, std::string_view name, Format format,
                             std::uint8_t* out, std::size_t capacity) noexcept
{
    const std::size_t total = (format == Format::JP8080) ? kRackPatchBytes : kKeyboardPatchBytes;
    if (out == nullptr || capacity < total)
        return 0;

    std::fill_n (out, total, std::uint8_t { 0 });

    // 0x00..0x0F: 16 ASCII chars padded with spaces (0x20)
    const std::string cleanName = sanitizePatchName (name);
    for (std::size_t i = 0; i < 16; ++i)
        out[i] = static_cast<std::uint8_t> (i < cleanName.size() ? static_cast<unsigned char> (cleanName[i]) : 0x20u);

    // 0x10..0x49: Core synthesis parameters
    out[0x10] = static_cast<std::uint8_t> (clampByte (static_cast<int> (patch.lfo1.wave), 0, 3));
    out[0x11] = static_cast<std::uint8_t> (clampByte (patch.lfo1.rate, 0, 127));
    out[0x12] = static_cast<std::uint8_t> (clampByte (patch.lfo1.fade, 0, 127));
    out[0x13] = static_cast<std::uint8_t> (clampByte (patch.lfo2.rate, 0, 127));
    out[0x14] = 0; // LFO2 depth edit-page focus

    out[0x15] = static_cast<std::uint8_t> (patch.ring ? 1 : 0);
    out[0x16] = static_cast<std::uint8_t> (clampByte (patch.xmod, 0, 127));
    out[0x17] = static_cast<std::uint8_t> (clampByte (patch.balance + 64, 0, 127));
    out[0x18] = static_cast<std::uint8_t> (clampByte (static_cast<int> (patch.destination), 0, 2));
    out[0x19] = static_cast<std::uint8_t> (clampByte (patch.pitchLfo + 64, 0, 127));
    out[0x1A] = static_cast<std::uint8_t> (clampByte (patch.lfo2.pitch + 64, 0, 127));
    out[0x1B] = static_cast<std::uint8_t> (clampByte (patch.pitchEnvDepth + 64, 0, 127));
    out[0x1C] = static_cast<std::uint8_t> (clampByte (patch.pitchAttack, 0, 127));
    out[0x1D] = static_cast<std::uint8_t> (clampByte (patch.pitchDecay, 0, 127));

    out[0x1E] = static_cast<std::uint8_t> (clampByte (static_cast<int> (patch.osc1.wave), 0, 6));
    out[0x1F] = static_cast<std::uint8_t> (clampByte (patch.osc1.ctrl1, 0, 127));
    out[0x20] = static_cast<std::uint8_t> (clampByte (patch.osc1.ctrl2, 0, 127));

    out[0x21] = static_cast<std::uint8_t> (clampByte (static_cast<int> (patch.osc2.wave), 0, 2));
    out[0x22] = static_cast<std::uint8_t> (patch.osc2.sync ? 1 : 0);
    out[0x23] = static_cast<std::uint8_t> (clampByte (patch.osc2.range + 25, 0, 50));
    out[0x24] = static_cast<std::uint8_t> (clampByte (patch.osc2.fine + 50, 0, 100));
    out[0x25] = static_cast<std::uint8_t> (clampByte (patch.osc2.pulseWidth, 0, 127));
    out[0x26] = static_cast<std::uint8_t> (clampByte (patch.osc2.pwmDepth, 0, 127));

    out[0x27] = static_cast<std::uint8_t> (clampByte (static_cast<int> (patch.filter.type), 0, 2));
    out[0x28] = static_cast<std::uint8_t> (patch.filter.steep ? 1 : 0);
    out[0x29] = static_cast<std::uint8_t> (clampByte (patch.filter.cutoff, 0, 127));
    out[0x2A] = static_cast<std::uint8_t> (clampByte (patch.filter.resonance, 0, 127));
    out[0x2B] = static_cast<std::uint8_t> (clampByte (patch.filter.keyFollow + 64, 0, 127));
    out[0x2C] = static_cast<std::uint8_t> (clampByte (patch.filter.lfoDepth + 64, 0, 127));
    out[0x2D] = static_cast<std::uint8_t> (clampByte (patch.lfo2.filter + 64, 0, 127));
    out[0x2E] = static_cast<std::uint8_t> (clampByte (patch.filter.envDepth + 64, 0, 127));
    out[0x2F] = static_cast<std::uint8_t> (clampByte (patch.filter.env.attack, 0, 127));
    out[0x30] = static_cast<std::uint8_t> (clampByte (patch.filter.env.decay, 0, 127));
    out[0x31] = static_cast<std::uint8_t> (clampByte (patch.filter.env.sustain, 0, 127));
    out[0x32] = static_cast<std::uint8_t> (clampByte (patch.filter.env.release, 0, 127));

    out[0x33] = static_cast<std::uint8_t> (clampByte (patch.level, 0, 127));
    out[0x34] = static_cast<std::uint8_t> (clampByte (patch.ampLfo + 64, 0, 127));
    out[0x35] = static_cast<std::uint8_t> (clampByte (patch.lfo2.amp + 64, 0, 127));
    out[0x36] = static_cast<std::uint8_t> (clampByte (patch.amp.attack, 0, 127));
    out[0x37] = static_cast<std::uint8_t> (clampByte (patch.amp.decay, 0, 127));
    out[0x38] = static_cast<std::uint8_t> (clampByte (patch.amp.sustain, 0, 127));
    out[0x39] = static_cast<std::uint8_t> (clampByte (patch.amp.release, 0, 127));
    out[0x3A] = static_cast<std::uint8_t> (clampByte (static_cast<int> (patch.pan), 0, 2));

    out[0x3B] = static_cast<std::uint8_t> (clampByte (patch.effects.bass + 64, 0, 127));
    out[0x3C] = static_cast<std::uint8_t> (clampByte (patch.effects.treble + 64, 0, 127));
    out[0x3D] = static_cast<std::uint8_t> (clampByte (static_cast<int> (patch.effects.chorusType), 0, 11));
    out[0x3E] = static_cast<std::uint8_t> (clampByte (patch.effects.chorus, 0, 127));

    out[0x3F] = static_cast<std::uint8_t> (clampByte (static_cast<int> (patch.effects.delayType), 0, 4));
    out[0x40] = static_cast<std::uint8_t> (clampByte (patch.effects.delayTime, 0, 127));
    out[0x41] = static_cast<std::uint8_t> (clampByte (patch.effects.feedback, 0, 127));
    out[0x42] = static_cast<std::uint8_t> (clampByte (patch.effects.delay, 0, 127));

    out[0x43] = static_cast<std::uint8_t> (clampByte (patch.bendUp, 0, 24));
    out[0x44] = static_cast<std::uint8_t> (clampByte (patch.bendDown, 0, 24));
    out[0x45] = static_cast<std::uint8_t> (patch.portamento ? 1 : 0);
    out[0x46] = static_cast<std::uint8_t> (clampByte (patch.portamentoTime, 0, 127));
    out[0x47] = static_cast<std::uint8_t> (patch.mono ? 1 : 0);
    out[0x48] = static_cast<std::uint8_t> (patch.legato ? 1 : 0);
    out[0x49] = static_cast<std::uint8_t> (clampByte (patch.oscShift + 2, 0, 4));

    // Neutral zero (0x00, 0x7F = 0) for the 39 Control Assign pairs before MorphBendAssign (linear 74..151).
    for (int i = 0; i < 39; ++i)
        encodeTwoByte (out, 74 + i * 2, 0);
    out[linearOffset (0x01, 0x18)] = 0; // MorphBendAssign (linear 152)
    encodeTwoByte (out, linearOffset (0x01, 0x19), 0); // ControlPortamentoTime (linear 153..154)

    // Bridge front-panel Velocity & Filter Velocity knobs into VelocityMatrix if matrix isn't explicitly set.
    VelocityMatrix vm = patch.velocityMatrix;
    if (! vm.on && (patch.velocityAmp > 0 || patch.velocityFilter != 0))
    {
        vm.on = true;
        if (patch.velocityAmp > 0)
            vm.level = clampByte (patch.velocityAmp, 0, 127);
        if (patch.velocityFilter != 0)
            vm.cutoff = clampByte (static_cast<int> (std::lround (patch.velocityFilter * 127.0 / 63.0)), -127, 127);
    }

    out[linearOffset (0x01, 0x1B)] = static_cast<std::uint8_t> (vm.on ? 1 : 0); // linear 155
    const std::array<int, 40> vmVals {
        vm.lfo1Rate, vm.lfo1Fade, vm.lfo2Rate, vm.xmod, vm.balance,
        vm.pitchLfo, vm.pitchLfo2, vm.pitchEnvDepth, vm.pitchAttack, vm.pitchDecay,
        vm.osc1Ctrl1, vm.osc1Ctrl2, vm.osc2Range, vm.osc2Fine, vm.osc2Ctrl1, vm.osc2Ctrl2,
        vm.cutoff, vm.resonance, vm.keyFollow, vm.filterLfo, vm.filterLfo2,
        vm.envDepth, vm.filterAttack, vm.filterDecay, vm.filterSustain, vm.filterRelease,
        vm.level, vm.ampLfo, vm.ampLfo2, vm.ampAttack, vm.ampDecay, vm.ampSustain, vm.ampRelease,
        vm.bass, vm.treble, vm.multiFx, vm.delayTime, vm.delayFeedback, vm.delayLevel, vm.portamento
    };
    const int vmBase = linearOffset (0x01, 0x1C); // linear 156
    for (int i = 0; i < 40; ++i)
    {
        const int lo = (i == 12) ? -50 : (i == 13 ? -100 : -127);
        const int hi = (i == 12) ? 50 : (i == 13 ? 100 : 127);
        encodeTwoByte (out, vmBase + i * 2, vmVals[static_cast<std::size_t> (i)], lo, hi);
    }

    // Active indicators (0x016C..0x016E = linear 236..238)
    out[linearOffset (0x01, 0x6C)] = 0;
    out[linearOffset (0x01, 0x6D)] = static_cast<std::uint8_t> (vm.on ? 1 : 0);
    out[linearOffset (0x01, 0x6E)] = 0;

    if (format == Format::JP8080)
    {
        out[linearOffset (0x01, 0x73)] = static_cast<std::uint8_t> (patch.unison ? 1 : 0);
        out[linearOffset (0x01, 0x74)] = static_cast<std::uint8_t> (clampByte (patch.unisonDetune, 0, 50));
    }

    return total;
}

std::vector<std::uint8_t> encodePatch (const Patch& patch, std::string_view name, Format format,
                                       SlotKind slot, int slotIndex, std::uint8_t deviceId, bool hardwareSplit)
{
    std::array<std::uint8_t, kRackPatchBytes> buf {};
    const std::size_t total = writePatchBytes (patch, name, format, buf.data(), buf.size());
    std::vector<std::uint8_t> out;
    if (total == 0)
        return out;

    std::uint8_t addr[4] {};
    computePatchBaseAddress (slot, slotIndex, addr);

    if (hardwareSplit && total > kHardwareSplitLimit)
    {
        out.reserve (total + 28);
        appendDt1Packet (out, deviceId, addr, buf.data(), kHardwareSplitLimit);
        const auto [hi, lo] = splitOffset (static_cast<int> (kHardwareSplitLimit));
        std::uint8_t addr2[4] { addr[0], addr[1], static_cast<std::uint8_t> (addr[2] + hi), lo };
        appendDt1Packet (out, deviceId, addr2, buf.data() + kHardwareSplitLimit, total - kHardwareSplitLimit);
    }
    else
    {
        out.reserve (total + 14);
        appendDt1Packet (out, deviceId, addr, buf.data(), total);
    }
    return out;
}

std::vector<std::uint8_t> encodeUserBank (const std::vector<std::pair<std::string, Patch>>& patches,
                                          Format format, std::uint8_t deviceId, bool hardwareSplit)
{
    std::vector<std::uint8_t> out;
    const std::size_t count = std::min (patches.size(), kMaxUserPatches);
    out.reserve (count * 276);
    for (std::size_t i = 0; i < count; ++i)
    {
        const auto pkt = encodePatch (patches[i].second, patches[i].first, format,
                                      SlotKind::UserPatch, static_cast<int> (i), deviceId, hardwareSplit);
        out.insert (out.end(), pkt.begin(), pkt.end());
    }
    return out;
}

ParseResult parseStream (const std::uint8_t* data, std::size_t size, bool strictChecksum)
{
    ParseResult result;
    if (data == nullptr || size == 0)
        return result;

    // Transparently unwrap Standard MIDI File (.mid) containers.
    std::vector<std::uint8_t> unwrapped;
    if (size >= 14 && data[0] == 'M' && data[1] == 'T' && data[2] == 'h' && data[3] == 'd')
    {
        unwrapped = extractSysExFromMidi (data, size);
        data = unwrapped.data();
        size = unwrapped.size();
    }

    using SlotKey = std::tuple<SlotKind, int>;
    std::map<SlotKey, RawSlotBuffer> patchMap;
    // Key for Performance overlays: -1 for Temp (0x01), 0..63 for User Perf (0x03).
    std::map<int, PerfOverlay> perfOverlays;
    std::size_t nextOrder = 0;

    std::size_t pos = 0;
    while (pos < size)
    {
        if (data[pos] != kStatusSysEx)
        {
            ++pos;
            continue;
        }
        const std::size_t start = pos;
        ++pos;
        while (pos < size && data[pos] != kStatusEOX && data[pos] != kStatusSysEx)
            ++pos;
        if (pos >= size || data[pos] != kStatusEOX)
        {
            ++result.ignoredMessages;
            continue;
        }
        const std::size_t msgEnd = pos + 1;
        pos = msgEnd;

        const std::size_t msgLen = msgEnd - start;
        // Minimum DT1 message: F0 41 dev 00 06 12 aa aa aa aa d0 sum F7 = 13 bytes
        if (msgLen < 13
            || data[start + 1] != kManufacturerRoland
            || data[start + 3] != kModelMsb
            || data[start + 4] != kModelLsb
            || data[start + 5] != kCmdDataSet1)
        {
            ++result.ignoredMessages;
            continue;
        }

        const std::uint8_t dev = data[start + 2];
        if (! ((dev >= 0x10 && dev <= 0x1F) || dev == 0x7F))
        {
            ++result.ignoredMessages;
            continue;
        }

        const std::uint8_t* addrAndData = data + start + 6;
        const std::size_t addrAndDataLen = msgLen - 8; // excludes header (6) + sum (1) + F7 (1)
        const std::uint8_t checksum = data[msgEnd - 2];

        const bool checksumOk = verifyChecksum (addrAndData, addrAndDataLen, checksum);
        if (! checksumOk)
        {
            ++result.badChecksums;
            if (strictChecksum)
                continue;
        }

        ++result.messagesParsed;

        const std::uint8_t a3 = addrAndData[0] & 0x7Fu;
        const std::uint8_t a2 = addrAndData[1] & 0x7Fu;
        const std::uint8_t a1 = addrAndData[2] & 0x7Fu;
        const std::uint8_t a0 = addrAndData[3] & 0x7Fu;
        const std::uint8_t* payload = addrAndData + 4;
        const std::size_t payloadLen = addrAndDataLen - 4;

        auto writeToSlot = [&] (SlotKind kind, int slotIdx, int startOff)
        {
            const SlotKey key { kind, slotIdx };
            auto [it, inserted] = patchMap.emplace (key, RawSlotBuffer {});
            RawSlotBuffer& buf = it->second;
            if (inserted)
            {
                buf.kind = kind;
                buf.slotIndex = slotIdx;
                buf.order = nextOrder++;
            }
            if (! checksumOk)
                buf.checksumError = true;
            for (std::size_t k = 0; k < payloadLen; ++k)
            {
                const int off = startOff + static_cast<int> (k);
                if (off >= 0 && off < static_cast<int> (kRackPatchBytes))
                {
                    buf.bytes[static_cast<std::size_t> (off)] = payload[k] & 0x7Fu;
                    buf.present[static_cast<std::size_t> (off)] = true;
                }
            }
        };

        auto handlePerfRegion = [&] (int perfKey)
        {
            PerfOverlay& ov = perfOverlays[perfKey];
            if (a1 <= 0x01)
            {
                // Performance Common
                const int startOff = linearOffset (a1, a0);
                ov.hasCommon = true;
                for (std::size_t k = 0; k < payloadLen; ++k)
                {
                    const int off = startOff + static_cast<int> (k);
                    const std::uint8_t v = payload[k] & 0x7Fu;
                    switch (off)
                    {
                        case 0x15: ov.arpDest = v; break;
                        case 0x17: ov.arpOn = v != 0; break;
                        case 0x18: ov.arpMode = v; break;
                        case 0x19: ov.arpBeatPattern = v; break;
                        case 0x1A: ov.arpRange = v; break;
                        case 0x1B: ov.arpHold = v != 0; break;
                        default: break;
                    }
                }
            }
            else if (a1 == 0x10 || a1 == 0x11)
            {
                // PartUpper (0x10) / PartLower (0x11)
                const bool isLower = a1 == 0x11;
                for (std::size_t k = 0; k < payloadLen; ++k)
                {
                    const int off = static_cast<int> (a0) + static_cast<int> (k);
                    if (off == 0x04)
                    {
                        if (isLower)
                        {
                            ov.hasLowerDelaySync = true;
                            ov.lowerDelaySync = payload[k] & 0x7Fu;
                        }
                        else
                        {
                            ov.hasUpperDelaySync = true;
                            ov.upperDelaySync = payload[k] & 0x7Fu;
                        }
                    }
                }
            }
            else if ((a1 & 0xFEu) == 0x40 || (a1 & 0xFEu) == 0x42)
            {
                const bool isUpper = (a1 & 0xFEu) == 0x40;
                const SlotKind kind = (perfKey < 0)
                    ? (isUpper ? SlotKind::TempUpper : SlotKind::TempLower)
                    : (isUpper ? SlotKind::PerfUpper : SlotKind::PerfLower);
                const int startOff = linearOffset (static_cast<std::uint8_t> (a1 & 0x01u), a0);
                writeToSlot (kind, perfKey < 0 ? 0 : perfKey, startOff);
            }
        };

        if (a3 == 0x01 && a2 == 0x00)
        {
            handlePerfRegion (-1);
        }
        else if (a3 == 0x02 && a2 <= 0x01)
        {
            const int userIdx = static_cast<int> (a2) * 64 + static_cast<int> ((a1 & 0x7Eu) >> 1);
            const int startOff = linearOffset (static_cast<std::uint8_t> (a1 & 0x01u), a0);
            writeToSlot (SlotKind::UserPatch, userIdx, startOff);
        }
        else if (a3 == 0x03 && a2 <= 0x3F)
        {
            handlePerfRegion (static_cast<int> (a2));
        }
        else
        {
            // System or Pattern/Motion data — valid packet, not a sound patch.
            (void) 0;
        }
    }

    // Collect slots that carry core patch data (anything above name bytes or >= 64 bytes),
    // ordered as encountered in the SysEx stream.
    std::vector<const RawSlotBuffer*> ordered;
    ordered.reserve (patchMap.size());
    for (const auto& [k, buf] : patchMap)
        ordered.push_back (&buf);
    std::sort (ordered.begin(), ordered.end(), [] (const RawSlotBuffer* a, const RawSlotBuffer* b) {
        return a->order < b->order;
    });

    for (const RawSlotBuffer* buf : ordered)
    {
        bool hasSynthParam = false;
        for (std::size_t i = 0x10; i < kRackPatchBytes; ++i)
        {
            if (buf->present[i])
            {
                hasSynthParam = true;
                break;
            }
        }
        if (! hasSynthParam)
            continue;

        const PerfOverlay* overlay = nullptr;
        if (buf->kind == SlotKind::TempUpper || buf->kind == SlotKind::TempLower)
        {
            if (auto it = perfOverlays.find (-1); it != perfOverlays.end())
                overlay = &it->second;
        }
        else if (buf->kind == SlotKind::PerfUpper || buf->kind == SlotKind::PerfLower)
        {
            if (auto it = perfOverlays.find (buf->slotIndex); it != perfOverlays.end())
                overlay = &it->second;
        }
        result.patches.push_back (decodeSlotBuffer (*buf, overlay));
    }

    return result;
}
} // namespace trancera::sysex
