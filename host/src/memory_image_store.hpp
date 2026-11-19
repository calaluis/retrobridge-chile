#pragma once

// MemoryImageStore of retrobridge_host (menu-tnfs T009, M41-2): nine slots
// (one per drive D1..D8 and one more being validated) that hold whole images
// brought over TNFS. Each slot exposes an ImageByteSource over its bytes, so a
// drive serves them like any other image; HostImageRepository opens slot n
// under the reserved location "*tnfs*<n>".
//
// Ownership follows the H1 rules of ImageRepository: a slot is never freed
// while its source is open or attached to a drive. adopt() and release() run on
// the loader thread; open(), close() and the reads run on the SIO thread. The
// mutex guards the slot states; a slot's bytes never change while it is in use,
// and the memory of a released slot is freed outside the lock. The SIO-thread
// calls do not allocate (ADR-011).
//
// The bytes live in an ImageBuffer, allocated with new (std::nothrow): the
// project builds without exceptions (ADR-004), so a std::vector that cannot
// get its memory would abort instead of reporting it (M41-18).

#include <cstddef>
#include <cstdint>
#include <cstring>
#include <memory>
#include <mutex>
#include <new>
#include <utility>

#include "rb/core/assert.hpp"
#include "rb/core/result.hpp"
#include "rb/core/span.hpp"
#include "rb/domain/image_byte_source.hpp"

namespace rb::host {

// The bytes of one whole image: move-only, never copied by accident.
class ImageBuffer {
public:
    ImageBuffer() = default;
    // A moved-from buffer is empty in size too (review L6).
    ImageBuffer(ImageBuffer&& other) noexcept : bytes_(std::move(other.bytes_)), size_(other.size_)
    {
        other.size_ = 0;
    }
    ImageBuffer& operator=(ImageBuffer&& other) noexcept
    {
        if (this != &other) {
            bytes_ = std::move(other.bytes_);
            size_ = other.size_;
            other.size_ = 0;
        }
        return *this;
    }
    ImageBuffer(const ImageBuffer&) = delete;
    ImageBuffer& operator=(const ImageBuffer&) = delete;

    // `size` bytes, zeroed; not valid() when the memory cannot be had.
    static ImageBuffer allocate(uint32_t size) noexcept
    {
        ImageBuffer buffer;
        buffer.bytes_.reset(new (std::nothrow) uint8_t[size == 0 ? 1 : size]());
        buffer.size_ = buffer.bytes_ ? size : 0;
        return buffer;
    }
    static ImageBuffer copyOf(rb::core::Span<const uint8_t> bytes) noexcept
    {
        RB_ASSERT(bytes.size() <= UINT32_MAX);
        ImageBuffer buffer = allocate(static_cast<uint32_t>(bytes.size()));
        if (buffer.valid() && bytes.size() != 0) {
            std::memcpy(buffer.data(), bytes.data(), bytes.size());
        }
        return buffer;
    }

    bool valid() const noexcept { return bytes_ != nullptr; }
    uint8_t* data() noexcept { return bytes_.get(); }
    const uint8_t* data() const noexcept { return bytes_.get(); }
    uint32_t size() const noexcept { return size_; }

private:
    std::unique_ptr<uint8_t[]> bytes_;
    uint32_t size_ = 0;
};

class MemoryImageStore {
public:
    static constexpr std::size_t kSlots = 8 + 1;

    MemoryImageStore() = default;
    MemoryImageStore(const MemoryImageStore&) = delete;
    MemoryImageStore& operator=(const MemoryImageStore&) = delete;

    // Takes the bytes of a whole image into a free slot. NoFreeSlot when the nine
    // are in use; InvalidArgument for a buffer that was never allocated.
    rb::core::Result<std::size_t> adopt(ImageBuffer&& bytes)
    {
        if (!bytes.valid()) {
            return rb::core::Failure{rb::core::ErrorCode::InvalidArgument};
        }
        std::lock_guard<std::mutex> lock(mutex_);
        for (std::size_t i = 0; i < kSlots; ++i) {
            Slot& slot = slots_[i];
            if (!slot.used) {
                slot.source.bytes = std::move(bytes);
                slot.used = true;
                slot.open = false;
                return i;
            }
        }
        return rb::core::Failure{rb::core::ErrorCode::NoFreeSlot};
    }

    // The source of a slot in use.
    rb::domain::ImageByteSource& source(std::size_t slot)
    {
        RB_ASSERT(slot < kSlots);
        std::lock_guard<std::mutex> lock(mutex_);
        RB_ASSERT(slots_[slot].used);
        return slots_[slot].source;
    }

    // Frees a slot. False, and the slot stays, when it is free, open or attached.
    bool release(std::size_t slot)
    {
        if (slot >= kSlots) {
            return false;
        }
        ImageBuffer freed;
        {
            std::lock_guard<std::mutex> lock(mutex_);
            Slot& s = slots_[slot];
            if (!s.used || s.open || s.source.isAttached()) {
                return false;
            }
            freed = std::move(s.source.bytes);
            s.used = false;
        }
        return true;  // `freed` releases its memory here, outside the lock
    }

    bool inUse(std::size_t slot) const
    {
        if (slot >= kSlots) {
            return false;
        }
        std::lock_guard<std::mutex> lock(mutex_);
        return slots_[slot].used;
    }

    // For HostImageRepository: NotFound when the slot is free, NoFreeSlot when it is
    // already open (one holder at a time, like the menu disk).
    rb::core::Result<rb::domain::ImageByteSource*> open(std::size_t slot)
    {
        if (slot >= kSlots) {
            return rb::core::Failure{rb::core::ErrorCode::NotFound};
        }
        std::lock_guard<std::mutex> lock(mutex_);
        Slot& s = slots_[slot];
        if (!s.used) {
            return rb::core::Failure{rb::core::ErrorCode::NotFound};
        }
        if (s.open) {
            return rb::core::Failure{rb::core::ErrorCode::NoFreeSlot};
        }
        s.open = true;
        return static_cast<rb::domain::ImageByteSource*>(&s.source);
    }

    bool owns(const rb::domain::ImageByteSource& source) const noexcept { return indexOf(source) < kSlots; }

    // Idempotent, as ImageRepository::doClose must be.
    //
    // Limit (review L5): a slot keeps the same Source across adopt/release, so
    // close() cannot tell one holder from the next. A holder that kept its
    // pointer after closing it and closed it again once the slot was released,
    // adopted again and reopened would clear the new holder's open flag. No
    // valid flow does that: ADR-014 makes every source come back to be closed
    // exactly once (ScopedImage, MountDiskImage, CollectDriveReply,
    // MenuController::assignD1), and the TNFS loader only adopts and releases
    // slots, never closes a source. The menu disk and the file slots share
    // the same limit.
    void close(rb::domain::ImageByteSource& source)
    {
        const std::size_t slot = indexOf(source);
        if (slot >= kSlots) {
            return;
        }
        std::lock_guard<std::mutex> lock(mutex_);
        slots_[slot].open = false;
    }

private:
    struct Source final : rb::domain::ImageByteSource {
        ImageBuffer bytes;
        uint32_t size() const override { return bytes.size(); }
        rb::core::Result<void> readAt(uint32_t offset, rb::core::Span<uint8_t> out) override
        {
            const uint64_t end = uint64_t{offset} + out.size();
            if (end > bytes.size()) {
                return rb::core::Failure{rb::core::ErrorCode::OutOfBounds};
            }
            if (out.size() != 0) {
                std::memcpy(out.data(), bytes.data() + offset, out.size());
            }
            return {};
        }
    };
    struct Slot {
        Source source;
        bool used = false;
        bool open = false;
    };

    std::size_t indexOf(const rb::domain::ImageByteSource& source) const noexcept
    {
        for (std::size_t i = 0; i < kSlots; ++i) {
            if (&slots_[i].source == &source) {
                return i;
            }
        }
        return kSlots;
    }

    mutable std::mutex mutex_;
    Slot slots_[kSlots];
};

// "*tnfs*<n>" with n a single digit from 0 to kSlots - 1. '*' cannot appear in a
// Windows file name and the catalog only holds names ending in ".atr", so these
// locations never name a file of the images folder.
constexpr const char* kTnfsLocationPrefix = "*tnfs*";

inline bool isTnfsLocation(const char* location) noexcept
{
    return std::strncmp(location, kTnfsLocationPrefix, std::strlen(kTnfsLocationPrefix)) == 0;
}

// The slot a "*tnfs*<n>" location names; false for any other text.
inline bool parseTnfsLocation(const char* location, std::size_t& slot) noexcept
{
    if (!isTnfsLocation(location)) {
        return false;
    }
    const char* digit = location + std::strlen(kTnfsLocationPrefix);
    if (digit[0] < '0' || digit[0] >= static_cast<char>('0' + MemoryImageStore::kSlots) || digit[1] != '\0') {
        return false;
    }
    slot = static_cast<std::size_t>(digit[0] - '0');
    return true;
}

}  // namespace rb::host
