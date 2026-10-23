#pragma once

#include <cstdint>

#include "rb/core/result.hpp"
#include "rb/core/span.hpp"

namespace rb::domain {

class DiskImage;

// Outbound port owned by the domain (ADR-002, option A): read-only random access
// to the bytes of a disk image. Implemented by infrastructure adapters (FATFS in
// H3, TNFS later) and by test doubles; depends on rb_core only.
//
// Contract:
//   * readAt() is all-or-nothing: if offset + out.size() > size() it returns
//     ErrorCode::OutOfBounds and writes nothing. It never allocates.
//   * size() is stable while the source is open.
//
// Ownership (H1-8): the source object is owned by whoever created it (a
// repository slot); DiskImage only borrows it. A source is "attached" while a
// DiskImage references it. Only DiskImage changes that flag, and an attached
// source must not be closed or its slot reused.
class ImageByteSource {
public:
    ImageByteSource(const ImageByteSource&) = delete;
    ImageByteSource& operator=(const ImageByteSource&) = delete;

    virtual uint32_t size() const = 0;
    virtual rb::core::Result<void> readAt(uint32_t offset, rb::core::Span<uint8_t> out) = 0;

    bool isAttached() const noexcept { return attached_; }

protected:
    ImageByteSource() = default;
    ~ImageByteSource() = default;  // never deleted through the interface

private:
    friend class DiskImage;
    bool attached_ = false;
};

}  // namespace rb::domain
