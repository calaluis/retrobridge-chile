#pragma once

#include <cstdint>
#include <cstdio>

#include "rb/core/result.hpp"
#include "rb/core/span.hpp"
#include "rb/domain/image_byte_source.hpp"

namespace rb::storage {

// ImageByteSource over a C stdio FILE (ADR-015). On the ESP32 the stdio calls go
// through the ESP-IDF VFS to FATFS; on the host they reach the operating system.
// Lives in a StdioImageRepository slot, which owns it and opens/closes it.
//
// Contract (architecture §5.1):
//   * size() is read once at open and stays fixed.
//   * readAt() checks bounds before any I/O: OutOfBounds writes nothing.
//   * A short read (file shrank, card removed) or a stdio error is IoError; the
//     buffer content is then unspecified (H3-3).
//   * The stream is unbuffered (_IONBF): no stdio buffer on the heap and no
//     second cache on top of FATFS; reads do not allocate.
//   * Access::ReadOnly (the default) opens with "rb" and is never writable.
//     Access::ReadWriteIfAllowed tries "r+b" and falls back to "rb" when the
//     file cannot be written (read-only attribute, permissions): the source is
//     then not writable (menu-escritura-disco, M45-3).
//   * writeAt() checks bounds and writability first (nothing written), then
//     seeks, writes and flushes before returning; a short write or a stdio
//     error is IoError.
class StdioByteSource final : public rb::domain::ImageByteSource {
public:
    enum class Access : uint8_t { ReadOnly, ReadWriteIfAllowed };

    StdioByteSource() = default;
    ~StdioByteSource() { close(); }

    uint32_t size() const override { return size_; }
    rb::core::Result<void> readAt(uint32_t offset, rb::core::Span<uint8_t> out) override;
    bool writable() const override { return writable_; }
    rb::core::Result<void> writeAt(uint32_t offset, rb::core::Span<const uint8_t> in) override;

    // For the owning repository. open(): NotFound (missing or not a regular file)
    // or IoError. close(): idempotent; a failing fclose still frees the slot.
    rb::core::Result<void> open(const char* path, Access access = Access::ReadOnly);
    void close() noexcept;
    bool isOpen() const noexcept { return file_ != nullptr; }

private:
    std::FILE* file_ = nullptr;
    uint32_t size_ = 0;
    bool writable_ = false;
};

}  // namespace rb::storage
