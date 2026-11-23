#pragma once

// What D1..D8 hold, remembered between power-ups (menu-unidades-persistentes): per drive, nothing,
// a name of the microSD catalog, or an image of a TNFS server (menu-tnfs-persistente: the server,
// the path and the name shown). The codec packs it into one fixed-size slot for the two-slot store
// (SlotFileStore); a slot that is truncated, damaged or written halfway is rejected, and of two
// valid slots the one with the higher sequence wins. Fixed storage, no allocation; trivially
// copyable (the save queue copies it byte by byte). ~3.5 KB: never on a task's stack.

#include <array>
#include <cstddef>
#include <cstdint>

#include "rb/app/image_location.hpp"

namespace rb::host {

class DriveMemory {
public:
    static constexpr unsigned kDrives = 8;
    static constexpr std::size_t kMaxName = rb::app::ImageLocation::kMaxLength;
    static constexpr std::size_t kMaxServer = 32;  // MenuSources::kMaxName (drive_memory.cpp checks)
    static constexpr std::size_t kMaxPath = 255;   // kTnfsMaxPath (drive_memory.cpp checks)

    // Where a drive's image came from. The values are stored in the slot: never renumber.
    enum class Origin : uint8_t { Empty = 0, Local = 1, NetAtr = 2, NetXex = 3 };

    Origin origin(unsigned drive) const noexcept;
    // The name remembered for drive 1..8: the catalog name (Local) or the name shown (Net*);
    // "" when the drive is empty or out of range.
    const char* name(unsigned drive) const noexcept;
    // Net*: the server as the source named it when mounted, and the absolute path; "" otherwise.
    const char* server(unsigned drive) const noexcept;
    const char* path(unsigned drive) const noexcept;

    // Remembers the catalog name `name` for drive 1..8 ("" empties it). False, and nothing changes,
    // if the drive is out of range or the name is longer than kMaxName or has a character outside
    // printable ASCII.
    bool set(unsigned drive, const char* name) noexcept;
    // Remembers a TNFS image for drive 1..8. False, and nothing changes, unless `origin` is NetAtr
    // or NetXex, `server` is 1..kMaxServer printable ASCII, `path` is 1..kMaxPath bytes starting
    // with '/' and without control characters (UTF-8 allowed), and `shown` is a valid name.
    bool setNet(unsigned drive, Origin origin, const char* server, const char* path, const char* shown) noexcept;

    // menu-cinta (AC5): the tape on C:, the same way ("" = no tape).
    const char* tape() const noexcept { return names_[kTapeField].data(); }
    bool setTape(const char* name) noexcept { return setField(kTapeField, name); }

    // Every drive and C: empty, in place: no temporary on the caller's stack (the SIO task).
    void clear() noexcept
    {
        for (auto& field : names_) {
            field.fill('\0');
        }
        origins_.fill(Origin::Empty);
        for (auto& field : servers_) {
            field.fill('\0');
        }
        for (auto& field : paths_) {
            field.fill('\0');
        }
    }

    bool operator==(const DriveMemory& other) const noexcept;
    bool operator!=(const DriveMemory& other) const noexcept { return !(*this == other); }

private:
    static constexpr std::size_t kTapeField = kDrives;  // after D1..D8
    bool setField(std::size_t field, const char* name) noexcept;
    void clearNet(std::size_t index) noexcept;
    std::array<std::array<char, kMaxName + 1>, kDrives + 1> names_{};
    std::array<Origin, kDrives> origins_{};
    std::array<std::array<char, kMaxServer + 1>, kDrives> servers_{};
    std::array<std::array<char, kMaxPath + 1>, kDrives> paths_{};
};

struct DriveMemorySlot {
    DriveMemory memory;
    uint32_t seq = 0;
};

namespace drive_memory_codec {

// Slot layout, little-endian: magic (4), version (2), reserved 0 (2), seq (4), the name fields of
// kNameField bytes each (the name, then zeros), [version 3: the network fields], CRC-32 of
// everything before it (4).
// Version 3 (menu-tnfs-persistente): the 9 name fields of version 2, then for D1..D8 one network
// field of kNetField bytes: origin (1), server (kMaxServer + 1), path (kMaxPath + 1), each string
// followed by zeros. Version 2 (menu-cinta): 9 name fields, D1..D8 then the tape on C:.
// Version 1: 8 name fields. Versions 1 and 2 are still read (a named drive is Local), never written.
constexpr uint32_t kMagic = 0x554D4252u;  // "RBMU"
constexpr uint16_t kVersion = 3;
constexpr uint16_t kVersion2 = 2;
constexpr uint16_t kVersion1 = 1;
constexpr std::size_t kNameField = DriveMemory::kMaxName + 1;
constexpr std::size_t kNamesOffset = 12;
constexpr std::size_t kNetField = 1 + (DriveMemory::kMaxServer + 1) + (DriveMemory::kMaxPath + 1);
constexpr std::size_t kNetOffset = kNamesOffset + (DriveMemory::kDrives + 1) * kNameField;
constexpr std::size_t kSlotSizeV1 = kNamesOffset + DriveMemory::kDrives * kNameField + 4;
constexpr std::size_t kSlotSizeV2 = kNetOffset + 4;
constexpr std::size_t kSlotSize = kNetOffset + DriveMemory::kDrives * kNetField + 4;
using Block = std::array<uint8_t, kSlotSize>;

// Always writes version 3.
void encode(const DriveMemory& memory, uint32_t seq, Block& out) noexcept;
// True, filling `out`, only for a slot of exactly kSlotSize bytes with version 3, kSlotSizeV2 with
// version 2, or kSlotSizeV1 with version 1 (the tape then reads empty), with the right magic,
// reserved field, NUL-terminated names followed by zeros, a matching CRC and, in version 3,
// network fields that agree with the names (Empty: no name; Local: a name and no server or path;
// Net*: a name, a server and a path starting with '/').
bool decode(const uint8_t* data, std::size_t size, DriveMemorySlot& out) noexcept;
// The valid slot with the higher sequence (nullptr = not valid); `a` on a tie; nullptr if none.
const DriveMemorySlot* newest(const DriveMemorySlot* a, const DriveMemorySlot* b) noexcept;

}  // namespace drive_memory_codec

}  // namespace rb::host
