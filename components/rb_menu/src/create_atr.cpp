#include "rb/menu/create_atr.hpp"

#include <array>
#include <cerrno>
#include <cstdio>
#include <cstring>

#include "rb/app/image_location.hpp"
#include "rb/core/span.hpp"
#include "rb/storage/image_path.hpp"

namespace rb::host {

namespace {

constexpr uint32_t kSectorSize = 128;

bool allowed(char c) noexcept
{
    return (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') || c == '-' || c == '_';
}

// Windows device names: "NUL.ATR" opens the device, not a file (CON, PRN, AUX,
// NUL, COM0-9, LPT0-9).
bool reserved(const char* name, std::size_t length) noexcept
{
    if (length == 3) {
        return std::strcmp(name, "CON") == 0 || std::strcmp(name, "PRN") == 0 ||
               std::strcmp(name, "AUX") == 0 || std::strcmp(name, "NUL") == 0;
    }
    return length == 4 && (std::strncmp(name, "COM", 3) == 0 || std::strncmp(name, "LPT", 3) == 0) &&
           name[3] >= '0' && name[3] <= '9';
}

}  // namespace

bool isNewAtrName(const char* name) noexcept
{
    if (name == nullptr) {
        return false;
    }
    std::size_t length = 0;
    for (; name[length] != '\0'; ++length) {
        if (length == kMaxNewAtrName || !allowed(name[length])) {
            return false;
        }
    }
    return length > 0 && !reserved(name, length);
}

CreateAtrResult createAtr(const char* folder, const char* name, rb::domain::Density density) noexcept
{
    if (!isNewAtrName(name)) {
        return CreateAtrResult::InvalidName;
    }
    uint32_t sectors = 0;
    if (density == rb::domain::Density::Single) {
        sectors = 720;
    } else if (density == rb::domain::Density::Enhanced) {
        sectors = 1040;
    } else {
        return CreateAtrResult::InvalidDensity;
    }

    char fileName[kMaxNewAtrName + 5];
    std::snprintf(fileName, sizeof fileName, "%s.ATR", name);
    const auto location = rb::app::ImageLocation::from(fileName);
    std::array<char, 1024> path{};
    if (!location.ok() ||
        !rb::storage::build_image_path(folder, location.value(), rb::core::Span<char>{path.data(), path.size()}).ok()) {
        return CreateAtrResult::IoError;
    }

    errno = 0;
    std::FILE* file = std::fopen(path.data(), "wbx");  // C11 exclusive creation
    if (file == nullptr) {
        return errno == EEXIST ? CreateAtrResult::AlreadyExists : CreateAtrResult::IoError;
    }
    const uint32_t paragraphs = sectors * kSectorSize / 16u;
    std::array<uint8_t, 16> header{};
    header[0] = 0x96;
    header[1] = 0x02;
    header[2] = static_cast<uint8_t>(paragraphs & 0xFF);
    header[3] = static_cast<uint8_t>((paragraphs >> 8) & 0xFF);
    header[4] = static_cast<uint8_t>(kSectorSize & 0xFF);
    header[5] = static_cast<uint8_t>((kSectorSize >> 8) & 0xFF);
    header[6] = static_cast<uint8_t>((paragraphs >> 16) & 0xFF);
    static constexpr std::array<uint8_t, kSectorSize> kZeros{};

    bool ok = std::fwrite(header.data(), 1, header.size(), file) == header.size();
    for (uint32_t n = 0; ok && n < sectors; ++n) {
        ok = std::fwrite(kZeros.data(), 1, kZeros.size(), file) == kZeros.size();
    }
    ok = std::fclose(file) == 0 && ok;
    if (!ok) {
        std::remove(path.data());  // created by this call: nothing is left behind
        return CreateAtrResult::IoError;
    }
    return CreateAtrResult::Created;
}

}  // namespace rb::host
