#pragma once

// Test double for the ImageByteSource port. Not an adapter: it holds a 16-byte
// ATR header and synthesises the rest of the image from its offset, so tests can
// model images of any size (up to ~16.8 MB) without allocating them.
// It also records every read and flags any read outside the image.

#include <array>
#include <cstdint>

#include "rb/domain/image_byte_source.hpp"

namespace rb::test {

class FakeByteSource final : public rb::domain::ImageByteSource {
public:
    FakeByteSource(const std::array<uint8_t, 16>& header, uint32_t size) : header_(header), size_(size) {}

    // Deterministic content of every byte after the header.
    static uint8_t data_byte(uint32_t offset)
    {
        return static_cast<uint8_t>((offset * 7u) ^ (offset >> 8));
    }

    uint32_t size() const override { return size_; }

    rb::core::Result<void> readAt(uint32_t offset, rb::core::Span<uint8_t> out) override
    {
        ++reads_;
        if (offset > size_ || out.size() > size_ - offset) {
            ++out_of_bounds_reads_;
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
    unsigned reads() const { return reads_; }
    unsigned out_of_bounds_reads() const { return out_of_bounds_reads_; }

private:
    std::array<uint8_t, 16> header_;
    uint32_t size_;
    bool fail_reads_ = false;
    unsigned reads_ = 0;
    unsigned out_of_bounds_reads_ = 0;
};

}  // namespace rb::test
