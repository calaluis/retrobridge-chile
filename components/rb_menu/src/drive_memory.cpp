#include "rb/menu/drive_memory.hpp"

#include <cstring>

#include "rb/menu/sio_write_log.hpp"

namespace rb::host {

namespace {

bool printable(char c) noexcept { return c >= 0x20 && c <= 0x7E; }

void put32(uint8_t* at, uint32_t value) noexcept
{
    for (int i = 0; i < 4; ++i) {
        at[i] = static_cast<uint8_t>(value >> (8 * i));
    }
}

uint32_t get32(const uint8_t* at) noexcept
{
    return static_cast<uint32_t>(at[0]) | static_cast<uint32_t>(at[1]) << 8 | static_cast<uint32_t>(at[2]) << 16 |
           static_cast<uint32_t>(at[3]) << 24;
}

}  // namespace

const char* DriveMemory::name(unsigned drive) const noexcept
{
    return drive >= 1 && drive <= kDrives ? names_[drive - 1].data() : "";
}

bool DriveMemory::set(unsigned drive, const char* name) noexcept
{
    if (drive < 1 || drive > kDrives) {
        return false;
    }
    return setField(drive - 1, name);
}

bool DriveMemory::setField(std::size_t index, const char* name) noexcept
{
    if (name == nullptr) {
        return false;
    }
    std::size_t length = 0;
    for (; name[length] != '\0'; ++length) {
        if (length == kMaxName || !printable(name[length])) {
            return false;
        }
    }
    std::array<char, kMaxName + 1>& field = names_[index];
    field.fill('\0');
    std::memcpy(field.data(), name, length);
    return true;
}

bool DriveMemory::operator==(const DriveMemory& other) const noexcept { return names_ == other.names_; }

namespace drive_memory_codec {

void encode(const DriveMemory& memory, uint32_t seq, Block& out) noexcept
{
    out.fill(0);
    put32(out.data(), kMagic);
    out[4] = static_cast<uint8_t>(kVersion);
    out[5] = static_cast<uint8_t>(kVersion >> 8);
    put32(out.data() + 8, seq);
    for (unsigned drive = 1; drive <= DriveMemory::kDrives; ++drive) {
        const char* name = memory.name(drive);
        std::memcpy(out.data() + kNamesOffset + (drive - 1) * kNameField, name, std::strlen(name));
    }
    const char* tape = memory.tape();
    std::memcpy(out.data() + kNamesOffset + DriveMemory::kDrives * kNameField, tape, std::strlen(tape));
    put32(out.data() + kSlotSize - 4, crc32(out.data(), kSlotSize - 4));
}

bool decode(const uint8_t* data, std::size_t size, DriveMemorySlot& out) noexcept
{
    // The size says the version it must carry: 9 fields for version 2, 8 for version 1.
    if (data == nullptr || (size != kSlotSize && size != kSlotSizeV1)) {
        return false;
    }
    const uint16_t version = size == kSlotSize ? kVersion : kVersion1;
    const unsigned fields = size == kSlotSize ? DriveMemory::kDrives + 1 : DriveMemory::kDrives;
    if (get32(data + size - 4) != crc32(data, size - 4) || get32(data) != kMagic ||
        (data[4] | data[5] << 8) != version || data[6] != 0 || data[7] != 0) {
        return false;
    }
    // Every name field is checked before `out` is touched, so a rejected slot leaves `out` as it was
    // without a DriveMemorySlot copy on the stack (~1 KB: it overflowed the 3584 B app_main stack).
    for (unsigned drive = 1; drive <= fields; ++drive) {
        const uint8_t* field = data + kNamesOffset + (drive - 1) * kNameField;
        const void* nul = std::memchr(field, 0, kNameField);
        if (nul == nullptr) {
            return false;
        }
        const std::size_t length = static_cast<std::size_t>(static_cast<const uint8_t*>(nul) - field);
        for (std::size_t i = 0; i < kNameField; ++i) {
            if (i < length ? !printable(static_cast<char>(field[i])) : field[i] != 0) {
                return false;  // a character outside printable ASCII, or garbage after the NUL
            }
        }
    }
    for (unsigned drive = 1; drive <= DriveMemory::kDrives; ++drive) {
        out.memory.set(drive, reinterpret_cast<const char*>(data + kNamesOffset + (drive - 1) * kNameField));
    }
    out.memory.setTape(fields > DriveMemory::kDrives
                           ? reinterpret_cast<const char*>(data + kNamesOffset + DriveMemory::kDrives * kNameField)
                           : "");
    out.seq = get32(data + 8);
    return true;
}

const DriveMemorySlot* newest(const DriveMemorySlot* a, const DriveMemorySlot* b) noexcept
{
    if (a == nullptr) {
        return b;
    }
    if (b == nullptr) {
        return a;
    }
    return b->seq > a->seq ? b : a;
}

}  // namespace drive_memory_codec

}  // namespace rb::host
