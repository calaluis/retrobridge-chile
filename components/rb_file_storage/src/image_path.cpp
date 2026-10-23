#include "rb/storage/image_path.hpp"

#include <cstring>

namespace rb::storage {

using rb::core::ErrorCode;
using rb::core::Failure;
using rb::core::Result;
using rb::core::Span;

namespace {

// True if every segment of `text` is a plain name: not empty, not "." or "..",
// and free of separators or drive markers other than '/'.
bool is_safe_relative_path(const char* text, std::size_t length)
{
    std::size_t segment_start = 0;
    for (std::size_t i = 0; i <= length; ++i) {
        const char c = i < length ? text[i] : '/';
        if (c == '\\' || c == ':') {
            return false;
        }
        if (c != '/') {
            continue;
        }
        const std::size_t segment_length = i - segment_start;
        const char* segment = text + segment_start;
        if (segment_length == 0) {
            return false;  // leading '/', "//" or trailing '/'
        }
        if ((segment_length == 1 && segment[0] == '.') ||
            (segment_length == 2 && segment[0] == '.' && segment[1] == '.')) {
            return false;
        }
        segment_start = i + 1;
    }
    return true;
}

}  // namespace

Result<std::size_t> build_image_path(const char* base, const rb::app::ImageLocation& location, Span<char> out)
{
    if (!is_safe_relative_path(location.c_str(), location.length())) {
        return Failure{ErrorCode::InvalidLocation};
    }

    std::size_t base_length = std::strlen(base);
    while (base_length > 0 && base[base_length - 1] == '/') {
        --base_length;  // "/sd/" and "/sd" mean the same base
    }
    const std::size_t total = base_length + 1 + location.length();
    if (total + 1 > out.size()) {
        return Failure{ErrorCode::InvalidLocation};  // does not fit
    }

    std::memcpy(out.data(), base, base_length);
    out[base_length] = '/';
    std::memcpy(out.data() + base_length + 1, location.c_str(), location.length());
    out[total] = '\0';
    return total;
}

}  // namespace rb::storage
