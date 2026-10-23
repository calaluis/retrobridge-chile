#pragma once

#include <array>
#include <cstddef>
#include <cstdint>

#include "rb/core/result.hpp"

namespace rb::app {

// Where a disk image lives (an SD path today, a TNFS URL later). Value object of
// the image storage context, held in place without allocation (H2-9).
// Invariant: 1..127 printable ASCII characters (0x20-0x7E), NUL-terminated.
class ImageLocation {
public:
    static constexpr std::size_t kMaxLength = 127;

    // Reads at most kMaxLength + 1 characters, so an unterminated or oversized
    // input is rejected without scanning past that bound.
    static rb::core::Result<ImageLocation> from(const char* text);

    const char* c_str() const noexcept { return text_.data(); }
    std::size_t length() const noexcept { return length_; }

    friend bool operator==(const ImageLocation& a, const ImageLocation& b) noexcept;
    friend bool operator!=(const ImageLocation& a, const ImageLocation& b) noexcept { return !(a == b); }

private:
    ImageLocation() = default;

    std::array<char, kMaxLength + 1> text_{};
    uint8_t length_ = 0;
};

}  // namespace rb::app
