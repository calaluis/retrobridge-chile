#pragma once

// Test double for the ImageRepository port: a fixed catalog of named images and a
// fixed number of slots, each holding a reusable in-memory byte source. Image
// content is synthesised from the offset (like FakeByteSource), so any size can
// be modelled without allocation. Counts opens and real closes so tests can prove
// that no source is leaked, closed twice or reused while attached.

#include <array>
#include <cstdint>
#include <cstring>

#include "rb/app/ports/image_repository.hpp"
#include "rb/core/assert.hpp"

namespace rb::test {

struct MemoryImage {
    const char* name;
    std::array<uint8_t, 16> header;
    uint32_t size;
};

class SlotByteSource final : public rb::domain::ImageByteSource {
public:
    static uint8_t data_byte(uint32_t offset) { return static_cast<uint8_t>((offset * 13u) ^ (offset >> 7)); }

    void reset(const MemoryImage& image)
    {
        header_ = image.header;
        size_ = image.size;
        fail_reads_ = false;
    }

    uint32_t size() const override { return size_; }

    rb::core::Result<void> readAt(uint32_t offset, rb::core::Span<uint8_t> out) override
    {
        if (offset > size_ || out.size() > size_ - offset) {
            return rb::core::Failure{rb::core::ErrorCode::OutOfBounds};
        }
        if (fail_reads_) {
            return rb::core::Failure{rb::core::ErrorCode::IoError};
        }
        for (uint32_t i = 0; i < out.size(); ++i) {
            const uint32_t at = offset + i;
            out[i] = at < header_.size() ? header_[at] : data_byte(at);
        }
        return {};
    }

    void fail_reads(bool fail) { fail_reads_ = fail; }

private:
    std::array<uint8_t, 16> header_{};
    uint32_t size_ = 0;
    bool fail_reads_ = false;
};

template <std::size_t Slots>
class MemoryImageRepository final : public rb::app::ImageRepository {
public:
    static constexpr std::size_t kMaxCatalog = 8;

    void add(const MemoryImage& image)
    {
        RB_ASSERT(catalog_size_ < kMaxCatalog);
        catalog_[catalog_size_++] = image;
    }

    unsigned open_count() const
    {
        unsigned count = 0;
        for (const Slot& slot : slots_) {
            count += slot.open ? 1u : 0u;
        }
        return count;
    }
    unsigned opens() const { return opens_; }
    unsigned closes() const { return closes_; }
    // Every open ever performed, plus closes; changes whenever the repository is used.
    unsigned operations() const { return opens_ + closes_ + failed_opens_; }

    bool is_open(const rb::domain::ImageByteSource* source) const
    {
        const Slot* slot = find(source);
        return slot != nullptr && slot->open;
    }

    SlotByteSource& slot_source(std::size_t index) { return slots_[index].source; }

protected:
    rb::core::Result<rb::domain::ImageByteSource*> doOpen(const rb::app::ImageLocation& location) override
    {
        const MemoryImage* image = lookup(location);
        if (image == nullptr) {
            ++failed_opens_;
            return rb::core::Failure{rb::core::ErrorCode::NotFound};
        }
        for (Slot& slot : slots_) {
            if (!slot.open) {
                RB_ASSERT(!slot.source.isAttached());  // a closed slot is never attached
                slot.source.reset(*image);
                slot.open = true;
                ++opens_;
                return static_cast<rb::domain::ImageByteSource*>(&slot.source);
            }
        }
        ++failed_opens_;
        return rb::core::Failure{rb::core::ErrorCode::NoFreeSlot};
    }

    void doClose(rb::domain::ImageByteSource& source) override
    {
        Slot* slot = find(&source);
        RB_ASSERT(slot != nullptr);  // only sources from this repository
        if (slot->open) {            // idempotent: closing a closed slot does nothing
            slot->open = false;
            ++closes_;
        }
    }

private:
    struct Slot {
        SlotByteSource source;
        bool open = false;
    };

    const MemoryImage* lookup(const rb::app::ImageLocation& location) const
    {
        for (std::size_t i = 0; i < catalog_size_; ++i) {
            if (std::strcmp(catalog_[i].name, location.c_str()) == 0) {
                return &catalog_[i];
            }
        }
        return nullptr;
    }

    const Slot* find(const rb::domain::ImageByteSource* source) const
    {
        for (const Slot& slot : slots_) {
            if (&slot.source == source) {
                return &slot;
            }
        }
        return nullptr;
    }
    Slot* find(const rb::domain::ImageByteSource* source)
    {
        return const_cast<Slot*>(static_cast<const MemoryImageRepository*>(this)->find(source));
    }

    std::array<Slot, Slots> slots_{};
    std::array<MemoryImage, kMaxCatalog> catalog_{};
    std::size_t catalog_size_ = 0;
    unsigned opens_ = 0;
    unsigned closes_ = 0;
    unsigned failed_opens_ = 0;
};

}  // namespace rb::test
