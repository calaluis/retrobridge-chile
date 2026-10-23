#pragma once

#include "rb/app/image_location.hpp"
#include "rb/core/assert.hpp"
#include "rb/core/result.hpp"
#include "rb/domain/image_byte_source.hpp"

namespace rb::app {

// Outbound port: opens and closes image byte sources held in fixed slots.
// Used only from the storage context (ADR-014).
//
// Non-virtual interface (H2-11): the public methods enforce the H1 ownership
// obligations once, for every adapter (FATFS in H3, TNFS later):
//   * open() never hands out an attached source, so an attached slot is never reused;
//   * close() on an attached source is a programming error and aborts;
//   * close(nullptr) and closing an already closed source do nothing.
// Adapters implement doOpen()/doClose(); doClose() must be idempotent.
class ImageRepository {
public:
    ImageRepository(const ImageRepository&) = delete;
    ImageRepository& operator=(const ImageRepository&) = delete;

    // NotFound, NoFreeSlot or IoError on failure.
    rb::core::Result<rb::domain::ImageByteSource*> open(const ImageLocation& location)
    {
        rb::core::Result<rb::domain::ImageByteSource*> result = doOpen(location);
        if (result.ok()) {
            RB_ASSERT(result.value() != nullptr && !result.value()->isAttached());
        }
        return result;
    }

    void close(rb::domain::ImageByteSource* source)
    {
        if (source == nullptr) {
            return;
        }
        RB_ASSERT(!source->isAttached());
        doClose(*source);
    }

protected:
    ImageRepository() = default;
    ~ImageRepository() = default;  // never deleted through the interface

    virtual rb::core::Result<rb::domain::ImageByteSource*> doOpen(const ImageLocation& location) = 0;
    virtual void doClose(rb::domain::ImageByteSource& source) = 0;
};

}  // namespace rb::app
