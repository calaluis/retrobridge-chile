#pragma once

// Test double for the ImageByteSource port with real, writable content
// (menu-escritura-disco, W002). Holds the whole image in a vector, so tests can
// write a sector and read it back. writable() and failing writes are set by the
// test; every rejected write leaves the content as it was.

#include <array>
#include <cstdint>
#include <vector>

#include "rb/domain/image_byte_source.hpp"

namespace rb::test {

class MemoryByteSource final : public rb::domain::ImageByteSource {
public:
    MemoryByteSource(const std::array<uint8_t, 16>& header, uint32_t size, bool writable)
        : bytes_(size), writable_(writable)
    {
        for (uint32_t i = 0; i < size; ++i) {
            bytes_[i] = i < header.size() ? header[i] : static_cast<uint8_t>(i * 3u);
        }
    }

    uint32_t size() const override { return static_cast<uint32_t>(bytes_.size()); }
    bool writable() const override { return writable_; }

    rb::core::Result<void> readAt(uint32_t offset, rb::core::Span<uint8_t> out) override
    {
        if (offset > size() || out.size() > size() - offset) {
            return rb::core::Failure{rb::core::ErrorCode::OutOfBounds};
        }
        for (uint32_t i = 0; i < out.size(); ++i) {
            out[i] = bytes_[offset + i];
        }
        return {};
    }

    rb::core::Result<void> writeAt(uint32_t offset, rb::core::Span<const uint8_t> in) override
    {
        ++writes_;
        if (offset > size() || in.size() > size() - offset) {
            return rb::core::Failure{rb::core::ErrorCode::OutOfBounds};
        }
        if (!writable_) {
            return rb::core::Failure{rb::core::ErrorCode::WriteProtected};
        }
        if (fail_writes_) {
            return rb::core::Failure{rb::core::ErrorCode::IoError};
        }
        for (uint32_t i = 0; i < in.size(); ++i) {
            bytes_[offset + i] = in[i];
        }
        return {};
    }

    void fail_writes(bool fail) { fail_writes_ = fail; }
    unsigned writes() const { return writes_; }
    const std::vector<uint8_t>& bytes() const { return bytes_; }

private:
    std::vector<uint8_t> bytes_;
    bool writable_;
    bool fail_writes_ = false;
    unsigned writes_ = 0;
};

}  // namespace rb::test
