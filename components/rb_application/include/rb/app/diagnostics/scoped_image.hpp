#pragma once

#include <optional>
#include <utility>

#include "rb/app/image_location.hpp"
#include "rb/app/ports/image_repository.hpp"
#include "rb/core/result.hpp"
#include "rb/domain/disk_image.hpp"

namespace rb::app {

// An image opened by a diagnostic in the storage context (H3-6), closed on
// every path: the DiskImage is destroyed (source detached) before the source
// goes back to the repository. Never mounted in the DriveBay.
class ScopedImage {
public:
    explicit ScopedImage(ImageRepository& repository) noexcept : repository_(repository) {}
    ScopedImage(const ScopedImage&) = delete;
    ScopedImage& operator=(const ScopedImage&) = delete;
    ~ScopedImage()
    {
        image_.reset();
        repository_.close(source_);
    }

    rb::core::Result<void> open(const ImageLocation& location)
    {
        rb::core::Result<rb::domain::ImageByteSource*> opened = repository_.open(location);
        if (!opened.ok()) {
            return rb::core::Failure{opened.error()};
        }
        source_ = opened.value();
        rb::core::Result<rb::domain::DiskImage> image = rb::domain::DiskImage::open(*source_);
        if (!image.ok()) {
            return rb::core::Failure{image.error()};  // the destructor closes the source
        }
        image_.emplace(std::move(image).value());
        return {};
    }

    rb::domain::DiskImage& image() noexcept { return *image_; }
    const rb::domain::ImageByteSource& source() const noexcept { return *source_; }

private:
    ImageRepository& repository_;
    rb::domain::ImageByteSource* source_ = nullptr;
    std::optional<rb::domain::DiskImage> image_;
};

}  // namespace rb::app
