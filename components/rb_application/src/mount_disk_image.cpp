#include "rb/app/usecases/mount_disk_image.hpp"

#include <utility>

#include "rb/domain/disk_image.hpp"

namespace rb::app {

using rb::core::Failure;
using rb::core::Result;
using rb::domain::DiskImage;
using rb::domain::DriveNumber;
using rb::domain::ImageByteSource;

Result<MountReport> MountDiskImage::execute(DriveNumber drive, const ImageLocation& location)
{
    Result<ImageByteSource*> opened = repository_.open(location);
    if (!opened.ok()) {
        return Failure{opened.error()};
    }
    ImageByteSource* source = opened.value();

    Result<DiskImage> image = DiskImage::open(*source);
    if (!image.ok()) {
        repository_.close(source);  // DiskImage::open leaves the source detached on failure
        return Failure{image.error()};
    }

    const rb::domain::AtrLayout& layout = image.value().layout();
    const MountReport report{layout.geometry(), layout.headerSizeMismatch(), layout.ignoredTrailingBytes(),
                             image.value().readOnly()};

    DriveRequest request = DriveRequest::mount(drive, std::move(image).value(), location);
    const Result<void> submitted = mailbox_.submit(request);
    if (!submitted.ok()) {
        request.image().reset();    // detach first: the request was not consumed
        repository_.close(source);  // the cancelled mount releases its slot
        return Failure{submitted.error()};
    }
    return report;
}

}  // namespace rb::app
