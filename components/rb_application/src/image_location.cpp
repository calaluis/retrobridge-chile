#include "rb/app/image_location.hpp"

namespace rb::app {

using rb::core::ErrorCode;
using rb::core::Failure;
using rb::core::Result;

Result<ImageLocation> ImageLocation::from(const char* text)
{
    if (text == nullptr) {
        return Failure{ErrorCode::InvalidLocation};
    }
    ImageLocation location;
    std::size_t length = 0;
    while (text[length] != '\0') {
        if (length == kMaxLength) {
            return Failure{ErrorCode::InvalidLocation};  // too long
        }
        const char c = text[length];
        if (c < 0x20 || c > 0x7E) {
            return Failure{ErrorCode::InvalidLocation};  // not printable ASCII (char may be signed)
        }
        location.text_[length] = c;
        ++length;
    }
    if (length == 0) {
        return Failure{ErrorCode::InvalidLocation};
    }
    location.length_ = static_cast<uint8_t>(length);
    return location;
}

bool operator==(const ImageLocation& a, const ImageLocation& b) noexcept
{
    if (a.length_ != b.length_) {
        return false;
    }
    for (std::size_t i = 0; i < a.length_; ++i) {
        if (a.text_[i] != b.text_[i]) {
            return false;
        }
    }
    return true;
}

}  // namespace rb::app
