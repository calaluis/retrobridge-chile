#pragma once

#include <cstdint>

#include "rb/core/result.hpp"
#include "rb/core/span.hpp"

namespace rb::domain {

class DiskImage;

// Outbound port owned by the domain (ADR-002, option A): random access to the
// bytes of a disk image; read-only unless the adapter says otherwise.
// Implemented by infrastructure adapters (FATFS in H3, TNFS later) and by test
// doubles; depends on rb_core only.
//
// Contract:
//   * readAt() is all-or-nothing: if offset + out.size() > size() it returns
//     ErrorCode::OutOfBounds and writes nothing. It never allocates.
//   * size() is stable while the source is open; a write never changes it.
//   * writable() is fixed while the source is open. By default a source is not
//     writable and writeAt() returns WriteProtected (menu-escritura-disco, M45).
//   * writeAt() is all-or-nothing on its checks: OutOfBounds (same rule as
//     readAt) or WriteProtected change nothing. An I/O failure is IoError and
//     may leave the range partly written. It never allocates.
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
    virtual bool writable() const { return false; }
    virtual rb::core::Result<void> writeAt(uint32_t offset, rb::core::Span<const uint8_t> in)
    {
        if (offset > size() || in.size() > size() - offset) {
            return rb::core::Failure{rb::core::ErrorCode::OutOfBounds};
        }
        return rb::core::Failure{rb::core::ErrorCode::WriteProtected};
    }

    bool isAttached() const noexcept { return attached_; }

protected:
    ImageByteSource() = default;
    ~ImageByteSource() = default;  // never deleted through the interface

private:
    friend class DiskImage;
    bool attached_ = false;
};

}  // namespace rb::domain
