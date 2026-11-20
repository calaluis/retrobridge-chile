#pragma once

#include <array>
#include <cstddef>
#include <cstring>

#include "rb/app/image_location.hpp"
#include "rb/app/ports/image_repository.hpp"
#include "rb/core/assert.hpp"
#include "rb/core/span.hpp"
#include "rb/storage/image_path.hpp"
#include "rb/storage/stdio_byte_source.hpp"

namespace rb::storage {

// ImageRepository over files under a base directory (ADR-015): "/sd" on the
// ESP32 (microSD mounted by rb_esp_storage), any directory on the host.
// `Slots` fixed StdioByteSource slots, no allocation by the repository itself
// (fopen may allocate inside the C library; opening happens in the storage
// context, outside the SIO path, H3-13).
// Used only from the storage context (ADR-014). The ownership rules (no close
// or reuse while attached) are enforced by the ImageRepository base (H2-11).
// The base path is configuration from outside (a command-line folder, a build
// directory): an empty one or one longer than kMaxBasePath is not copied and
// every open() then fails with InvalidArgument; baseValid() reports it.
// Writes (menu-escritura-disco): off by default, so every source is read-only.
// With `writes`, a file opens for writing when it allows it and no other open
// slot already writes the same path (compared without case, as on Windows);
// otherwise it opens read-only. One writer per file (M45, spec assumption).
template <std::size_t Slots>
class StdioImageRepository final : public rb::app::ImageRepository {
public:
    static constexpr std::size_t kMaxBasePath = kMaxImageBase;

    explicit StdioImageRepository(const char* basePath, bool writes = false) : writes_(writes)
    {
        RB_ASSERT(basePath != nullptr);  // programming error
        const std::size_t length = std::strlen(basePath);
        if (length > 0 && length <= kMaxBasePath) {
            std::memcpy(base_.data(), basePath, length + 1);
            baseValid_ = true;
        }
    }

    bool baseValid() const noexcept { return baseValid_; }

    std::size_t open_count() const noexcept
    {
        std::size_t count = 0;
        for (const StdioByteSource& slot : slots_) {
            count += slot.isOpen() ? 1u : 0u;
        }
        return count;
    }

protected:
    // InvalidArgument (unusable base), InvalidLocation, NoFreeSlot, NotFound or IoError.
    rb::core::Result<rb::domain::ImageByteSource*> doOpen(const rb::app::ImageLocation& location) override
    {
        if (!baseValid_) {
            return rb::core::Failure{rb::core::ErrorCode::InvalidArgument};
        }
        std::array<char, kMaxImagePath + 1> path{};
        const rb::core::Result<std::size_t> built =
            build_image_path(base_.data(), location, rb::core::Span<char>{path});
        if (!built.ok()) {
            return rb::core::Failure{built.error()};
        }
        const StdioByteSource::Access access = writes_ && !writerOpen(path.data())
                                                   ? StdioByteSource::Access::ReadWriteIfAllowed
                                                   : StdioByteSource::Access::ReadOnly;
        for (std::size_t i = 0; i < Slots; ++i) {
            StdioByteSource& slot = slots_[i];
            if (slot.isOpen()) {
                continue;
            }
            const rb::core::Result<void> opened = slot.open(path.data(), access);
            if (!opened.ok()) {
                return rb::core::Failure{opened.error()};
            }
            std::memcpy(paths_[i].data(), path.data(), path.size());
            return static_cast<rb::domain::ImageByteSource*>(&slot);
        }
        return rb::core::Failure{rb::core::ErrorCode::NoFreeSlot};
    }

    void doClose(rb::domain::ImageByteSource& source) override
    {
        for (StdioByteSource& slot : slots_) {
            if (&slot == &source) {
                slot.close();  // idempotent
                return;
            }
        }
        RB_ASSERT(false);  // not a source from this repository
    }

private:
    bool writerOpen(const char* path) const noexcept
    {
        for (std::size_t i = 0; i < Slots; ++i) {
            if (slots_[i].isOpen() && slots_[i].writable() && sameText(paths_[i].data(), path)) {
                return true;
            }
        }
        return false;
    }

    static bool sameText(const char* a, const char* b) noexcept
    {
        for (;; ++a, ++b) {
            const unsigned char x = static_cast<unsigned char>(*a);
            const unsigned char y = static_cast<unsigned char>(*b);
            const unsigned char lx = (x >= 'A' && x <= 'Z') ? static_cast<unsigned char>(x + 32) : x;
            const unsigned char ly = (y >= 'A' && y <= 'Z') ? static_cast<unsigned char>(y + 32) : y;
            if (lx != ly) {
                return false;
            }
            if (x == 0) {
                return true;
            }
        }
    }

    std::array<StdioByteSource, Slots> slots_{};
    std::array<std::array<char, kMaxImagePath + 1>, Slots> paths_{};
    std::array<char, kMaxBasePath + 1> base_{};
    bool baseValid_ = false;
    bool writes_ = false;
};

}  // namespace rb::storage
