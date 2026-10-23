#include "rb/domain/atr_layout.hpp"

namespace rb::domain {

using rb::core::ErrorCode;
using rb::core::Failure;
using rb::core::Result;
using rb::core::Span;

namespace {

constexpr uint8_t kMagicLow = 0x96;
constexpr uint8_t kMagicHigh = 0x02;
constexpr uint32_t kParagraphSize = 16;
constexpr uint32_t kBootAreaSize = DiskGeometry::kBootSectorCount * DiskGeometry::kBootSectorSize;

}  // namespace

Result<AtrLayout> AtrLayout::parse(Span<const uint8_t> header, uint32_t fileSize)
{
    if (header.size() < kHeaderSize || header[0] != kMagicLow || header[1] != kMagicHigh) {
        return Failure{ErrorCode::InvalidAtrHeader};
    }

    const uint16_t rawSectorSize = static_cast<uint16_t>(header[4] | (header[5] << 8));
    if (rawSectorSize != 128 && rawSectorSize != 256) {
        return Failure{ErrorCode::UnsupportedSectorSize};
    }
    const SectorSize sectorSize = static_cast<SectorSize>(rawSectorSize);

    if (fileSize < kHeaderSize) {
        return Failure{ErrorCode::ImageTooSmall};
    }
    const uint32_t dataSize = fileSize - kHeaderSize;

    // At most 0xFFFFFF paragraphs: the product stays below 2^28.
    const uint32_t paragraphs =
        static_cast<uint32_t>(header[2]) | (static_cast<uint32_t>(header[3]) << 8) | (static_cast<uint32_t>(header[6]) << 16);
    const uint32_t declaredSize = paragraphs * kParagraphSize;

    uint32_t sectorCount = 0;
    uint32_t trailing = 0;
    if (sectorSize == SectorSize::Bytes128 || dataSize < kBootAreaSize) {
        sectorCount = dataSize / DiskGeometry::kBootSectorSize;
        trailing = dataSize % DiskGeometry::kBootSectorSize;
    } else {
        if (dataSize % 256 == 0) {
            return Failure{ErrorCode::UnsupportedAtrLayout};  // D-6: 256-byte boot sectors
        }
        const uint32_t afterBoot = dataSize - kBootAreaSize;
        sectorCount = DiskGeometry::kBootSectorCount + afterBoot / 256;
        trailing = afterBoot % 256;
    }

    if (sectorCount == 0) {
        return Failure{ErrorCode::ImageTooSmall};
    }
    if (sectorCount > DiskGeometry::kMaxSectorCount) {
        return Failure{ErrorCode::ImageTooLarge};
    }

    Result<DiskGeometry> geometry = DiskGeometry::create(sectorCount, sectorSize);
    if (!geometry.ok()) {
        return Failure{geometry.error()};
    }
    return AtrLayout{geometry.value(), declaredSize != dataSize, trailing};
}

Result<ByteRange> AtrLayout::locate(SectorNumber sector) const
{
    if (!geometry_.contains(sector)) {
        return Failure{ErrorCode::SectorOutOfRange};
    }
    // sector <= 65535, so every product below stays under 2^25.
    const uint32_t n = sector.value();
    uint32_t offset = kHeaderSize;
    if (n <= DiskGeometry::kBootSectorCount) {
        offset += (n - 1) * DiskGeometry::kBootSectorSize;
    } else {
        offset += kBootAreaSize + (n - 1 - DiskGeometry::kBootSectorCount) * static_cast<uint32_t>(geometry_.sectorSize());
    }
    return ByteRange{offset, geometry_.sizeOf(sector)};
}

}  // namespace rb::domain
