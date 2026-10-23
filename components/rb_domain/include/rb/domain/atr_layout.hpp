#pragma once

#include <cstdint>

#include "rb/core/result.hpp"
#include "rb/core/span.hpp"
#include "rb/domain/disk_geometry.hpp"
#include "rb/domain/sector_number.hpp"

namespace rb::domain {

// Position of a sector inside the image file.
struct ByteRange {
    uint32_t offset;
    uint16_t length;
};

// Where each sector lives inside an ATR file. Pure knowledge of the ATR format.
//
// Header (16 bytes): magic $0296, image size in 16-byte paragraphs (low word
// plus high byte), sector size 128 or 256. Sectors 1-3 are always 128 bytes.
//
// Rules applied by parse():
//   D-8  The sector count comes from the real file size, not the header; a
//        disagreement is reported through headerSizeMismatch(), and a partial
//        trailing sector is ignored and reported through ignoredTrailingBytes().
//   D-6  Double-density images whose boot sectors are 256 bytes are rejected
//        (data size a multiple of 256, which the standard layout never is).
//   H1-5 More than 65535 sectors is rejected.
// Invariant: every range returned by locate() lies inside the file.
class AtrLayout {
public:
    static constexpr uint32_t kHeaderSize = 16;

    static rb::core::Result<AtrLayout> parse(rb::core::Span<const uint8_t> header, uint32_t fileSize);

    const DiskGeometry& geometry() const noexcept { return geometry_; }
    bool headerSizeMismatch() const noexcept { return headerSizeMismatch_; }
    uint32_t ignoredTrailingBytes() const noexcept { return ignoredTrailingBytes_; }

    rb::core::Result<ByteRange> locate(SectorNumber sector) const;

private:
    AtrLayout(DiskGeometry geometry, bool headerSizeMismatch, uint32_t ignoredTrailingBytes) noexcept
        : geometry_(geometry), headerSizeMismatch_(headerSizeMismatch), ignoredTrailingBytes_(ignoredTrailingBytes)
    {
    }

    DiskGeometry geometry_;
    bool headerSizeMismatch_;
    uint32_t ignoredTrailingBytes_;
};

}  // namespace rb::domain
