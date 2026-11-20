#include "rb/domain/disk_image.hpp"

#include <array>

namespace rb::domain {

using rb::core::ErrorCode;
using rb::core::Failure;
using rb::core::Result;
using rb::core::Span;

Result<DiskImage> DiskImage::open(ImageByteSource& source)
{
    if (source.isAttached()) {
        return Failure{ErrorCode::SourceInUse};
    }
    const uint32_t fileSize = source.size();
    if (fileSize < AtrLayout::kHeaderSize) {
        return Failure{ErrorCode::ImageTooSmall};
    }

    std::array<uint8_t, AtrLayout::kHeaderSize> header{};
    const Result<void> read = source.readAt(0, Span<uint8_t>{header});
    if (!read.ok()) {
        return Failure{read.error()};
    }

    Result<AtrLayout> layout = AtrLayout::parse(Span<const uint8_t>{header}, fileSize);
    if (!layout.ok()) {
        return Failure{layout.error()};
    }
    return DiskImage{source, layout.value()};
}

DiskImage::DiskImage(ImageByteSource& source, const AtrLayout& layout) noexcept : source_(&source), layout_(layout)
{
    source_->attached_ = true;
}

DiskImage::DiskImage(DiskImage&& other) noexcept : source_(other.source_), layout_(other.layout_)
{
    other.source_ = nullptr;
}

DiskImage& DiskImage::operator=(DiskImage&& other) noexcept
{
    if (this != &other) {
        detach();
        source_ = other.source_;
        layout_ = other.layout_;
        other.source_ = nullptr;
    }
    return *this;
}

DiskImage::~DiskImage()
{
    detach();
}

void DiskImage::detach() noexcept
{
    if (source_ != nullptr) {
        source_->attached_ = false;
        source_ = nullptr;
    }
}

Result<uint16_t> DiskImage::readSector(SectorNumber sector, Span<uint8_t> out)
{
    if (source_ == nullptr) {
        return Failure{ErrorCode::InvalidDiskImage};
    }
    const Result<ByteRange> located = layout_.locate(sector);
    if (!located.ok()) {
        return Failure{located.error()};
    }
    const ByteRange range = located.value();
    if (out.size() < range.length) {
        return Failure{ErrorCode::BufferTooSmall};
    }
    // The layout guarantees the range fits the size it was parsed with; this also
    // protects against a source whose size changed in breach of its contract.
    const uint32_t size = source_->size();
    if (range.offset > size || range.length > size - range.offset) {
        return Failure{ErrorCode::OutOfBounds};
    }
    const Result<void> read = source_->readAt(range.offset, out.first(range.length));
    if (!read.ok()) {
        return Failure{read.error()};
    }
    return range.length;
}

Result<uint16_t> DiskImage::writeSector(SectorNumber sector, Span<const uint8_t> data)
{
    if (source_ == nullptr) {
        return Failure{ErrorCode::InvalidDiskImage};
    }
    const Result<ByteRange> located = layout_.locate(sector);
    if (!located.ok()) {
        return Failure{located.error()};
    }
    const ByteRange range = located.value();
    if (data.size() < range.length) {
        return Failure{ErrorCode::BufferTooSmall};
    }
    if (readOnly()) {
        return Failure{ErrorCode::WriteProtected};
    }
    const uint32_t size = source_->size();
    if (range.offset > size || range.length > size - range.offset) {
        return Failure{ErrorCode::OutOfBounds};
    }
    const Result<void> written = source_->writeAt(range.offset, data.first(range.length));
    if (!written.ok()) {
        return Failure{written.error()};
    }
    return range.length;
}

Result<void> DiskImage::format(Density density)
{
    if (source_ == nullptr) {
        return Failure{ErrorCode::InvalidDiskImage};
    }
    if (readOnly()) {
        return Failure{ErrorCode::WriteProtected};
    }
    const DiskGeometry& geometry = layout_.geometry();
    if ((density != Density::Single && density != Density::Enhanced) || geometry.density() != density) {
        return Failure{ErrorCode::DensityMismatch};
    }
    static constexpr std::array<uint8_t, 128> kZeros{};  // Single and Enhanced: 128-byte sectors
    const uint32_t size = source_->size();
    for (uint32_t n = 1; n <= geometry.sectorCount(); ++n) {
        const Result<ByteRange> located = layout_.locate(SectorNumber::from(static_cast<uint16_t>(n)).value());
        if (!located.ok()) {
            return Failure{located.error()};
        }
        const ByteRange range = located.value();
        if (range.length > kZeros.size() || range.offset > size || range.length > size - range.offset) {
            return Failure{ErrorCode::OutOfBounds};
        }
        const Result<void> written = source_->writeAt(range.offset, Span<const uint8_t>{kZeros.data(), range.length});
        if (!written.ok()) {
            return Failure{written.error()};
        }
    }
    return {};
}

}  // namespace rb::domain
