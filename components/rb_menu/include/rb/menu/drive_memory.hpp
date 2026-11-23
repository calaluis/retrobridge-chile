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

    // menu-cinta (AC5): the tape on C:, the same way ("" = no tape).
    const char* tape() const noexcept { return names_[kTapeField].data(); }
    bool setTape(const char* name) noexcept { return setField(kTapeField, name); }

    // Every drive and C: empty, in place: no ~1.1 KB temporary on the caller's stack (the SIO task).
    void clear() noexcept
    {
        for (auto& field : names_) {
            field.fill('\0');
        }
    }

    bool operator==(const DriveMemory& other) const noexcept;
    bool operator!=(const DriveMemory& other) const noexcept { return !(*this == other); }

private:
    static constexpr std::size_t kTapeField = kDrives;  // after D1..D8
    bool setField(std::size_t field, const char* name) noexcept;
    std::array<std::array<char, kMaxName + 1>, kDrives + 1> names_{};
};

struct DriveMemorySlot {
    DriveMemory memory;
    uint32_t seq = 0;
};

namespace drive_memory_codec {

// Slot layout, little-endian: magic (4), version (2), reserved 0 (2), seq (4), the name fields of
// kNameField bytes each (the name, then zeros), CRC-32 of everything before it (4).
// Version 2 (menu-cinta): 9 fields, D1..D8 then the tape on C:. Version 1: 8 fields, D1..D8;
// still read (no tape), never written.
constexpr uint32_t kMagic = 0x554D4252u;  // "RBMU"
constexpr uint16_t kVersion = 2;
constexpr uint16_t kVersion1 = 1;
constexpr std::size_t kNameField = DriveMemory::kMaxName + 1;
constexpr std::size_t kNamesOffset = 12;
constexpr std::size_t kSlotSizeV1 = kNamesOffset + DriveMemory::kDrives * kNameField + 4;
constexpr std::size_t kSlotSize = kNamesOffset + (DriveMemory::kDrives + 1) * kNameField + 4;
using Block = std::array<uint8_t, kSlotSize>;

// Always writes version 2.
void encode(const DriveMemory& memory, uint32_t seq, Block& out) noexcept;

// True, filling `out`, only for a slot of exactly kSlotSize bytes with version 2, or of exactly
// kSlotSizeV1 bytes with version 1 (the tape then reads empty), with the right magic, reserved
// field, NUL-terminated printable names followed by zeros, and a matching CRC.
bool decode(const uint8_t* data, std::size_t size, DriveMemorySlot& out) noexcept;

// The valid slot with the higher sequence (nullptr = not valid); `a` on a tie; nullptr if none.
const DriveMemorySlot* newest(const DriveMemorySlot* a, const DriveMemorySlot* b) noexcept;

}  // namespace drive_memory_codec

}  // namespace rb::host
