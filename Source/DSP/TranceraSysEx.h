// Canonical JUCE-free Roland JP-8000 / JP-8080 System Exclusive (DT1) patch codec.
// Specification: [settled] Roland JP-8000 (1996) & JP-8080 (1998) MIDI
// Implementation Parameter Address Map (Model ID 00 06, Command DT1 12).
#pragma once

#include "TranceraPatch.h"

#include <cstddef>
#include <cstdint>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace trancera::sysex
{
inline constexpr std::uint8_t kStatusSysEx = 0xF0;
inline constexpr std::uint8_t kStatusEOX = 0xF7;
inline constexpr std::uint8_t kManufacturerRoland = 0x41;
inline constexpr std::uint8_t kModelMsb = 0x00;
inline constexpr std::uint8_t kModelLsb = 0x06;
inline constexpr std::uint8_t kCmdDataRequest1 = 0x11;
inline constexpr std::uint8_t kCmdDataSet1 = 0x12;
inline constexpr std::uint8_t kDefaultDeviceId = 0x10; // Roland Device ID 17 (0x10..0x1F)

inline constexpr std::size_t kKeyboardPatchBytes = 239; // 00 00 .. 01 6E (linear 0..238)
inline constexpr std::size_t kRackPatchBytes = 248;     // 00 00 .. 01 77 (linear 0..247)
inline constexpr std::size_t kHardwareSplitLimit = 242; // 01 72: JP-8080 splits dumps after 242 bytes
inline constexpr std::size_t kMaxUserPatches = 128;     // U:A11 .. U:B88

enum class Format : std::uint8_t
{
    JP8000,
    JP8080
};

enum class SlotKind : std::uint8_t
{
    TempUpper,
    TempLower,
    UserPatch,
    PerfUpper,
    PerfLower
};

struct ImportWarnings
{
    bool osc2NoiseMappedToTriangle { false };
    bool distortionMappedToChorus { false };
    bool hasVelocityMatrix { false };
    bool checksumErrors { false };
};

struct PatchRecord
{
    Patch patch;
    std::string name { "Init" };
    SlotKind slotKind { SlotKind::TempUpper };
    int slotIndex { 0 }; // 0..127 for UserPatch (A11..B88), 0..63 for Perf (P11..P88)
    Format detectedFormat { Format::JP8000 };
    ImportWarnings warnings;
};

struct ParseResult
{
    std::vector<PatchRecord> patches;
    int messagesParsed { 0 };
    int badChecksums { 0 };
    int ignoredMessages { 0 };
};

// Roland 7-bit base-128 address arithmetic within a block.
[[nodiscard]] constexpr int linearOffset (std::uint8_t addrHi, std::uint8_t addrLo) noexcept
{
    return (static_cast<int> (addrHi & 0x7Fu) << 7) | static_cast<int> (addrLo & 0x7Fu);
}

[[nodiscard]] constexpr std::pair<std::uint8_t, std::uint8_t> splitOffset (int linear) noexcept
{
    return {
        static_cast<std::uint8_t> ((linear >> 7) & 0x7Fu),
        static_cast<std::uint8_t> (linear & 0x7Fu)
    };
}

// Roland DT1 checksum: (128 - (sum(addr + data) & 0x7F)) & 0x7F.
[[nodiscard]] std::uint8_t computeChecksum (const std::uint8_t* addrAndData, std::size_t length) noexcept;
[[nodiscard]] bool verifyChecksum (const std::uint8_t* addrAndData, std::size_t length, std::uint8_t checksum) noexcept;

// Printable 16-char ASCII patch name normalized for display or fixed-width wire storage.
[[nodiscard]] std::string sanitizePatchName (std::string_view name);
[[nodiscard]] std::string formatSlotLabel (SlotKind kind, int slotIndex);

// Populates Patch::velocityAmp & Patch::velocityFilter from VelocityMatrix when converting
// into flat 70-control representations (like Reason .repatch) that do not expose the matrix.
void bakeVelocityMatrixToControls (Patch& patch) noexcept;

// Writes the raw 239-byte (JP-8000) or 248-byte (JP-8080) patch buffer; returns bytes written or 0.
std::size_t writePatchBytes (const Patch& patch, std::string_view name, Format format,
                             std::uint8_t* out, std::size_t capacity) noexcept;

// Encodes one patch as complete DT1 SysEx message(s) (2 packets if JP8080 + hardwareSplit=true).
[[nodiscard]] std::vector<std::uint8_t> encodePatch (const Patch& patch, std::string_view name,
                                                     Format format = Format::JP8080,
                                                     SlotKind slot = SlotKind::TempUpper,
                                                     int slotIndex = 0,
                                                     std::uint8_t deviceId = kDefaultDeviceId,
                                                     bool hardwareSplit = true);

// Encodes up to 128 patches sequentially into User Patch bank U:A11..U:B88 (02 00 00 00..02 01 7E 00).
[[nodiscard]] std::vector<std::uint8_t> encodeUserBank (const std::vector<std::pair<std::string, Patch>>& patches,
                                                        Format format = Format::JP8080,
                                                        std::uint8_t deviceId = kDefaultDeviceId,
                                                        bool hardwareSplit = true);

// Parses raw .syx streams OR Standard MIDI File (.mid) containers holding JP-8000/JP-8080 DT1 dumps.
[[nodiscard]] ParseResult parseStream (const std::uint8_t* data, std::size_t size, bool strictChecksum = true);
} // namespace trancera::sysex
