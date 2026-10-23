#pragma once

#include <cstdint>

#include "rb/app/diagnostics/scoped_image.hpp"
#include "rb/app/image_location.hpp"
#include "rb/app/ports/image_repository.hpp"
#include "rb/core/result.hpp"
#include "rb/domain/disk_geometry.hpp"

namespace rb::app {

struct ImageInfo {
    uint32_t fileSize;
    rb::domain::DiskGeometry geometry;
    bool headerSizeMismatch;        // D-8
    uint32_t ignoredTrailingBytes;  // D-8
};

// Storage-context diagnostic (H3-6): what the domain sees in an image file.
class InspectImage {
public:
    explicit InspectImage(ImageRepository& repository) noexcept : repository_(repository) {}

    rb::core::Result<ImageInfo> execute(const ImageLocation& location)
    {
        ScopedImage scoped{repository_};
        const rb::core::Result<void> opened = scoped.open(location);
        if (!opened.ok()) {
            return rb::core::Failure{opened.error()};
        }
        const rb::domain::AtrLayout& layout = scoped.image().layout();
        return ImageInfo{scoped.source().size(), layout.geometry(), layout.headerSizeMismatch(),
                         layout.ignoredTrailingBytes()};
    }

private:
    ImageRepository& repository_;
};

}  // namespace rb::app
