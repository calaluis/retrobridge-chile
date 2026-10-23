#pragma once

#include <cstdint>
#include <optional>
#include <utility>

#include "rb/app/image_location.hpp"
#include "rb/core/error_code.hpp"
#include "rb/domain/disk_image.hpp"
#include "rb/domain/drive_number.hpp"
#include "rb/domain/image_byte_source.hpp"

namespace rb::app {

enum class DriveRequestKind : uint8_t { Mount, Unmount };

// Message from the storage context to the drive context (ADR-014). A Mount
// request carries the prepared DiskImage by move: whoever holds the request
// owns the image. Move-only.
class DriveRequest {
public:
    static DriveRequest mount(rb::domain::DriveNumber drive, rb::domain::DiskImage&& image, const ImageLocation& location)
    {
        DriveRequest request{DriveRequestKind::Mount, drive};
        request.image_.emplace(std::move(image));
        request.location_.emplace(location);
        return request;
    }

    static DriveRequest unmount(rb::domain::DriveNumber drive) { return DriveRequest{DriveRequestKind::Unmount, drive}; }

    DriveRequestKind kind() const noexcept { return kind_; }
    rb::domain::DriveNumber drive() const noexcept { return drive_; }
    std::optional<rb::domain::DiskImage>& image() noexcept { return image_; }
    const std::optional<ImageLocation>& location() const noexcept { return location_; }

private:
    DriveRequest(DriveRequestKind kind, rb::domain::DriveNumber drive) : kind_(kind), drive_(drive) {}

    DriveRequestKind kind_;
    rb::domain::DriveNumber drive_;
    std::optional<rb::domain::DiskImage> image_;
    std::optional<ImageLocation> location_;
};

// Answer from the drive context. Every source the storage context must close
// travels back here exactly once:
//   displaced - the source of the disk removed by a successful mount/unmount;
//   rejected  - the source of a mount request that could not be applied.
// Both are already detached when the reply is sent.
struct DriveReply {
    DriveRequestKind kind;
    rb::domain::DriveNumber drive;
    std::optional<rb::core::ErrorCode> error;  // nullopt: applied
    rb::domain::ImageByteSource* displaced;
    rb::domain::ImageByteSource* rejected;
};

}  // namespace rb::app
