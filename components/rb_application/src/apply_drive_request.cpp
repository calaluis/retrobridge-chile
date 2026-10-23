#include "rb/app/usecases/apply_drive_request.hpp"

#include <optional>
#include <utility>

#include "rb/core/assert.hpp"

namespace rb::app {

using rb::core::Result;
using rb::domain::DiskImage;
using rb::domain::ImageByteSource;

bool ApplyDriveRequest::execute()
{
    std::optional<DriveRequest> request = mailbox_.takeRequest();
    if (!request) {
        return false;
    }

    DriveReply reply{request->kind(), request->drive(), std::nullopt, nullptr, nullptr};
    if (request->kind() == DriveRequestKind::Mount) {
        std::optional<DiskImage>& image = request->image();
        RB_ASSERT(image.has_value() && request->location().has_value());
        ImageByteSource* source = image->source();

        Result<ImageByteSource*> mounted = bay_.mount(request->drive(), std::move(*image), *request->location());
        if (mounted.ok()) {
            reply.displaced = mounted.value();
        } else {
            reply.error = mounted.error();
            image.reset();            // not consumed on failure: detach it here
            reply.rejected = source;  // and send it back to be closed
        }
    } else {
        Result<ImageByteSource*> unmounted = bay_.unmount(request->drive());
        if (unmounted.ok()) {
            reply.displaced = unmounted.value();
        } else {
            reply.error = unmounted.error();
        }
    }

    mailbox_.putReply(reply);
    return true;
}

}  // namespace rb::app
