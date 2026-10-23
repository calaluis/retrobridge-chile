#pragma once

#include <cstddef>
#include <cstdint>

#include "rb/app/image_location.hpp"
#include "rb/app/ports/image_repository.hpp"
#include "rb/core/error_code.hpp"
#include "rb/core/result.hpp"

namespace rb::app {

// CRC-32 (IEEE) of a buffer: esp_rom_crc32_le on the ESP32, a test routine on
// the host (H3-8). A verification tool only, never the SIO checksum.
using Crc32Function = uint32_t (*)(const uint8_t* data, std::size_t length);

// Receives the result of each sector, in order (the console prints them).
class SectorCrcSink {
public:
    virtual void onSector(uint16_t sector, uint16_t length, uint32_t crc) = 0;
    virtual void onReadError(uint16_t sector, rb::core::ErrorCode error) = 0;

protected:
    SectorCrcSink() = default;
    SectorCrcSink(const SectorCrcSink&) = default;
    SectorCrcSink& operator=(const SectorCrcSink&) = default;
    ~SectorCrcSink() = default;
};

struct VerifyReport {
    uint32_t sectors;
    uint32_t readErrors;
};

// Storage-context diagnostic for Meta A (H3-6, H3-8): reads every sector of an
// image through DiskImage and reports its CRC-32. A failed sector is reported
// and the scan continues.
class VerifySectors {
public:
    VerifySectors(ImageRepository& repository, Crc32Function crc32) noexcept : repository_(repository), crc32_(crc32) {}

    rb::core::Result<VerifyReport> execute(const ImageLocation& location, SectorCrcSink& sink);

private:
    ImageRepository& repository_;
    Crc32Function crc32_;
};

}  // namespace rb::app
