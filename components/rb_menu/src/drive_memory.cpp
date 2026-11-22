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
    if (drive < 1 || drive > kDrives || name == nullptr) {
        return false;
    }
    std::size_t length = 0;
    for (; name[length] != '\0'; ++length) {
        if (length == kMaxName || !printable(name[length])) {
            return false;
        }
    }
    std::array<char, kMaxName + 1>& field = names_[drive - 1];
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
    put32(out.data() + kSlotSize - 4, crc32(out.data(), kSlotSize - 4));
}

bool decode(const uint8_t* data, std::size_t size, DriveMemorySlot& out) noexcept
{
    if (data == nullptr || size != kSlotSize || get32(data + kSlotSize - 4) != crc32(data, kSlotSize - 4) ||
        get32(data) != kMagic || (data[4] | data[5] << 8) != kVersion || data[6] != 0 || data[7] != 0) {
        return false;
    }
    DriveMemorySlot slot;
    slot.seq = get32(data + 8);
    for (unsigned drive = 1; drive <= DriveMemory::kDrives; ++drive) {
        const uint8_t* field = data + kNamesOffset + (drive - 1) * kNameField;
        const void* nul = std::memchr(field, 0, kNameField);
        if (nul == nullptr) {
            return false;
        }
        const std::size_t length = static_cast<std::size_t>(static_cast<const uint8_t*>(nul) - field);
        for (std::size_t i = length; i < kNameField; ++i) {
            if (field[i] != 0) {
                return false;
            }
        }
        if (!slot.memory.set(drive, reinterpret_cast<const char*>(field))) {
            return false;  // a character outside printable ASCII
        }
    }
    out = slot;
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
