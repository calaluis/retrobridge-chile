#pragma once

// What D1..D8 hold, remembered between power-ups (menu-unidades-persistentes): one name of the
// microSD catalog per drive, or nothing. The codec packs it into one fixed-size slot for the
// two-slot store (SlotFileStore); a slot that is truncated, damaged or written halfway is rejected,
// and of two valid slots the one with the higher sequence wins. Fixed storage, no allocation.

#include <array>
#include <cstddef>
#include <cstdint>

#include "rb/app/image_location.hpp"

namespace rb::host {

class DriveMemory {
public:
    static constexpr unsigned kDrives = 8;
    static constexpr std::size_t kMaxName = rb::app::ImageLocation::kMaxLength;

    // The name remembered for drive 1..8; "" when the drive is empty or out of range.
    const char* name(unsigned drive) const noexcept;

    // Remembers `name` for drive 1..8 ("" empties it). False, and nothing changes, if the drive is
    // out of range or the name is longer than kMaxName or has a character outside printable ASCII.
    bool set(unsigned drive, const char* name) noexcept;

    bool operator==(const DriveMemory& other) const noexcept;
    bool operator!=(const DriveMemory& other) const noexcept { return !(*this == other); }

private:
    std::array<std::array<char, kMaxName + 1>, kDrives> names_{};
};

struct DriveMemorySlot {
    DriveMemory memory;
    uint32_t seq = 0;
};

namespace drive_memory_codec {

// Slot layout, little-endian: magic (4), version (2), reserved 0 (2), seq (4), 8 name fields of
// kNameField bytes (the name, then zeros), CRC-32 of everything before it (4).
constexpr uint32_t kMagic = 0x554D4252u;  // "RBMU"
constexpr uint16_t kVersion = 1;
constexpr std::size_t kNameField = DriveMemory::kMaxName + 1;
constexpr std::size_t kNamesOffset = 12;
constexpr std::size_t kSlotSize = kNamesOffset + DriveMemory::kDrives * kNameField + 4;
using Block = std::array<uint8_t, kSlotSize>;

void encode(const DriveMemory& memory, uint32_t seq, Block& out) noexcept;

// True, filling `out`, only for a slot of exactly kSlotSize bytes with the right magic, version,
// reserved field, NUL-terminated printable names followed by zeros, and a matching CRC.
bool decode(const uint8_t* data, std::size_t size, DriveMemorySlot& out) noexcept;

// The valid slot with the higher sequence (nullptr = not valid); `a` on a tie; nullptr if none.
const DriveMemorySlot* newest(const DriveMemorySlot* a, const DriveMemorySlot* b) noexcept;

}  // namespace drive_memory_codec

}  // namespace rb::host
