#include "rb/menu/drive_memory.hpp"

#include <cstring>

#include "rb/menu/menu_loader.hpp"
#include "rb/menu/menu_sources.hpp"
#include "rb/menu/sio_write_log.hpp"

namespace rb::host {

static_assert(DriveMemory::kMaxServer == MenuSources::kMaxName, "a remembered server is a source's server");
static_assert(DriveMemory::kMaxPath == kTnfsMaxPath, "a remembered path is one the loader can ask for");

namespace {

bool printable(char c) noexcept { return c >= 0x20 && c <= 0x7E; }

// A path byte: no control characters (C0 or DEL); UTF-8 bytes 0x80..0xFF pass.
bool pathByte(char c) noexcept
{
    const auto byte = static_cast<uint8_t>(c);
    return byte >= 0x20 && byte != 0x7F;
}

// Length of `text` if it is 1..max bytes that all pass `ok`; 0 otherwise.
template <typename Ok>
std::size_t validLength(const char* text, std::size_t max, Ok ok) noexcept
{
    if (text == nullptr) {
        return 0;
    }
    std::size_t length = 0;
    for (; text[length] != '\0'; ++length) {
        if (length == max || !ok(text[length])) {
            return 0;
        }
    }
    return length;
}

// A stored string field of `size` bytes: some bytes that pass `ok`, a NUL, then zeros only.
// Its length, or -1 if the field is not like that.
template <typename Ok>
long fieldLength(const uint8_t* field, std::size_t size, Ok ok) noexcept
{
    const void* nul = std::memchr(field, 0, size);
    if (nul == nullptr) {
        return -1;
    }
    const std::size_t length = static_cast<std::size_t>(static_cast<const uint8_t*>(nul) - field);
    for (std::size_t i = 0; i < size; ++i) {
        if (i < length ? !ok(static_cast<char>(field[i])) : field[i] != 0) {
            return -1;
        }
    }
    return static_cast<long>(length);
}

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

DriveMemory::Origin DriveMemory::origin(unsigned drive) const noexcept
{
    return drive >= 1 && drive <= kDrives ? origins_[drive - 1] : Origin::Empty;
}

const char* DriveMemory::name(unsigned drive) const noexcept
{
    return drive >= 1 && drive <= kDrives ? names_[drive - 1].data() : "";
}

const char* DriveMemory::server(unsigned drive) const noexcept
{
    return drive >= 1 && drive <= kDrives ? servers_[drive - 1].data() : "";
}

const char* DriveMemory::path(unsigned drive) const noexcept
{
    return drive >= 1 && drive <= kDrives ? paths_[drive - 1].data() : "";
}

bool DriveMemory::set(unsigned drive, const char* name) noexcept
{
    if (drive < 1 || drive > kDrives || !setField(drive - 1, name)) {
        return false;
    }
    clearNet(drive - 1);
    origins_[drive - 1] = name[0] != '\0' ? Origin::Local : Origin::Empty;
    return true;
}

bool DriveMemory::setNet(unsigned drive, Origin origin, const char* server, const char* path,
                         const char* shown) noexcept
{
    if (drive < 1 || drive > kDrives || (origin != Origin::NetAtr && origin != Origin::NetXex)) {
        return false;
    }
    const std::size_t serverLength = validLength(server, kMaxServer, printable);
    const std::size_t pathLength = validLength(path, kMaxPath, pathByte);
    if (serverLength == 0 || pathLength == 0 || path[0] != '/' || validLength(shown, kMaxName, printable) == 0) {
        return false;
    }
    const std::size_t index = drive - 1;
    (void)setField(index, shown);  // checked above
    clearNet(index);
    std::memcpy(servers_[index].data(), server, serverLength);
    std::memcpy(paths_[index].data(), path, pathLength);
    origins_[index] = origin;
    return true;
}

void DriveMemory::clearNet(std::size_t index) noexcept
{
    servers_[index].fill('\0');
    paths_[index].fill('\0');
    origins_[index] = Origin::Empty;
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

bool DriveMemory::operator==(const DriveMemory& other) const noexcept
{
    return names_ == other.names_ && origins_ == other.origins_ && servers_ == other.servers_ &&
           paths_ == other.paths_;
}

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
    for (unsigned drive = 1; drive <= DriveMemory::kDrives; ++drive) {
        uint8_t* field = out.data() + kNetOffset + (drive - 1) * kNetField;
        field[0] = static_cast<uint8_t>(memory.origin(drive));
        const char* server = memory.server(drive);
        const char* path = memory.path(drive);
        std::memcpy(field + 1, server, std::strlen(server));
        std::memcpy(field + 1 + DriveMemory::kMaxServer + 1, path, std::strlen(path));
    }
    put32(out.data() + kSlotSize - 4, crc32(out.data(), kSlotSize - 4));
}

bool decode(const uint8_t* data, std::size_t size, DriveMemorySlot& out) noexcept
{
    // The size says the version it must carry: 3, 2 (9 name fields, no network) or 1 (8 name fields).
    if (data == nullptr || (size != kSlotSize && size != kSlotSizeV2 && size != kSlotSizeV1)) {
        return false;
    }
    const uint16_t version = size == kSlotSize ? kVersion : size == kSlotSizeV2 ? kVersion2 : kVersion1;
    const unsigned fields = size == kSlotSizeV1 ? DriveMemory::kDrives : DriveMemory::kDrives + 1;
    if (get32(data + size - 4) != crc32(data, size - 4) || get32(data) != kMagic ||
        (data[4] | data[5] << 8) != version || data[6] != 0 || data[7] != 0) {
        return false;
    }
    // Every field is checked before `out` is touched, so a rejected slot leaves `out` as it was
    // without a DriveMemorySlot copy on the stack (it overflowed the 3584 B app_main stack at ~1 KB).
    std::array<bool, DriveMemory::kDrives> named{};
    for (unsigned drive = 1; drive <= fields; ++drive) {
        const long length = fieldLength(data + kNamesOffset + (drive - 1) * kNameField, kNameField, printable);
        if (length < 0) {
            return false;  // a character outside printable ASCII, no NUL, or garbage after it
        }
        if (drive <= DriveMemory::kDrives) {
            named[drive - 1] = length > 0;
        }
    }
    if (version == kVersion) {
        for (unsigned drive = 1; drive <= DriveMemory::kDrives; ++drive) {
            const uint8_t* field = data + kNetOffset + (drive - 1) * kNetField;
            const long server = fieldLength(field + 1, DriveMemory::kMaxServer + 1, printable);
            const uint8_t* pathField = field + 1 + DriveMemory::kMaxServer + 1;
            const long path = fieldLength(pathField, DriveMemory::kMaxPath + 1, pathByte);
            if (server < 0 || path < 0) {
                return false;
            }
            const bool net = server > 0 || path > 0;
            switch (static_cast<DriveMemory::Origin>(field[0])) {
            case DriveMemory::Origin::Empty:
                if (named[drive - 1] || net) {
                    return false;
                }
                break;
            case DriveMemory::Origin::Local:
                if (!named[drive - 1] || net) {
                    return false;
                }
                break;
            case DriveMemory::Origin::NetAtr:
            case DriveMemory::Origin::NetXex:
                if (!named[drive - 1] || server == 0 || path == 0 || pathField[0] != '/') {
                    return false;
                }
                break;
            default:
                return false;  // an origin this firmware does not know
            }
        }
    }
    out.memory.clear();
    for (unsigned drive = 1; drive <= DriveMemory::kDrives; ++drive) {
        const char* name = reinterpret_cast<const char*>(data + kNamesOffset + (drive - 1) * kNameField);
        const uint8_t* field = data + kNetOffset + (drive - 1) * kNetField;
        const auto origin = version == kVersion ? static_cast<DriveMemory::Origin>(field[0]) : DriveMemory::Origin::Local;
        if (origin == DriveMemory::Origin::NetAtr || origin == DriveMemory::Origin::NetXex) {
            (void)out.memory.setNet(drive, origin, reinterpret_cast<const char*>(field + 1),
                                    reinterpret_cast<const char*>(field + 1 + DriveMemory::kMaxServer + 1), name);
        } else {
            (void)out.memory.set(drive, name);  // "" leaves it Empty
        }
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
