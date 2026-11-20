#pragma once

#include <cstdint>

#include "rb/core/result.hpp"
#include "rb/core/span.hpp"
#include "rb/domain/atr_layout.hpp"
#include "rb/domain/disk_geometry.hpp"
#include "rb/domain/image_byte_source.hpp"
#include "rb/domain/sector_number.hpp"

namespace rb::domain {

// The disk "inserted" in a drive: an ATR layout over a borrowed byte source.
//
// Ownership policy (H1-8):
//   * Created only by open(), which refuses a source that is already attached:
//     one source backs at most one DiskImage.
//   * Move-only. Moving transfers the attachment; the moved-from object is
//     invalid (isValid() == false) and refuses reads.
//   * Destroying or move-assigning over a valid image detaches its source.
// Hence a source is attached exactly while one live DiskImage references it.
//
// Writable exactly when its source is (menu-escritura-disco, M45-1/M45-3).
// Identity (ImageLocation) is added in H2 with ImageRepository.
class DiskImage {
public:
    static rb::core::Result<DiskImage> open(ImageByteSource& source);

    DiskImage(DiskImage&& other) noexcept;
    DiskImage& operator=(DiskImage&& other) noexcept;
    DiskImage(const DiskImage&) = delete;
    DiskImage& operator=(const DiskImage&) = delete;
    ~DiskImage();

    bool isValid() const noexcept { return source_ != nullptr; }
    bool readOnly() const noexcept { return source_ == nullptr || !source_->writable(); }
    const DiskGeometry& geometry() const noexcept { return layout_.geometry(); }
    const AtrLayout& layout() const noexcept { return layout_; }

    // The borrowed source (nullptr once moved from). For identity only.
    ImageByteSource* source() const noexcept { return source_; }

    // Copies sector `sector` into the start of `out`; returns its size in bytes.
    rb::core::Result<uint16_t> readSector(SectorNumber sector, rb::core::Span<uint8_t> out);

    // Writes the first bytes of `data` (the sector's size) as sector `sector`;
    // returns that size. BufferTooSmall, SectorOutOfRange and WriteProtected
    // write nothing; IoError comes from the source.
    rb::core::Result<uint16_t> writeSector(SectorNumber sector, rb::core::Span<const uint8_t> data);

    // Writes $00 to every sector, like a drive's format command (menu-formatear-disco,
    // F001). `density` must be the disk's own, Single or Enhanced: anything else is
    // DensityMismatch. WriteProtected and DensityMismatch write nothing; the first
    // IoError from the source stops it (sectors before it are already zero).
    rb::core::Result<void> format(Density density);

private:
    DiskImage(ImageByteSource& source, const AtrLayout& layout) noexcept;
    void detach() noexcept;

    ImageByteSource* source_;
    AtrLayout layout_;
};

}  // namespace rb::domain
