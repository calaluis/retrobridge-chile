#pragma once

#include <cstddef>

#include "rb/app/image_location.hpp"
#include "rb/core/result.hpp"
#include "rb/core/span.hpp"

namespace rb::storage {

// Longest storage base the adapter accepts: Windows MAX_PATH (260) minus the
// NUL, so a deep host folder or build directory fits; "/sd" on the ESP32.
constexpr std::size_t kMaxImageBase = 259;

// Longest full path the adapter builds: base + '/' + location.
constexpr std::size_t kMaxImagePath = kMaxImageBase + 1 + rb::app::ImageLocation::kMaxLength;

// Joins `base` and `location` into `out` (NUL-terminated) and returns the path
// length. Path policy (H3-2, ADR-015): the location is relative to the base, so
// InvalidLocation is returned for a leading '/' or '\', any '\' or ':', empty
// segments, "." or ".." segments, or a path that does not fit in `out`.
rb::core::Result<std::size_t> build_image_path(const char* base, const rb::app::ImageLocation& location,
                                               rb::core::Span<char> out);

}  // namespace rb::storage
