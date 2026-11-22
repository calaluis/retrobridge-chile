#include "rb/menu/drive_memory_link.hpp"

#include <cerrno>
#include <cstdio>

#include "rb/menu/menu_controller.hpp"
#include "rb/menu/storage_reason.hpp"

namespace rb::host {

void planDriveMemory(const char* cardFailure, int cardError, const SlotFileStore::LoadResult* loaded,
                     bool saverReady, DriveMemoryPlan& out) noexcept
{
    out = DriveMemoryPlan{};
    if (cardFailure != nullptr) {
        std::snprintf(out.message, sizeof out.message, " MICROSD: %s", cardFailure);
        out.refusal = cardError != 0 ? cardError : EIO;
        return;
    }
    if (loaded == nullptr) {
        std::snprintf(out.message, sizeof out.message, " MEMORIA: %s", "SIN RAM");
        out.refusal = ENOMEM;
        return;
    }
    if (loaded->status == SlotFileStore::LoadStatus::Error) {
        std::snprintf(out.message, sizeof out.message, " MEMORIA: %s", storageReason(loaded->error));
        out.refusal = loaded->error != 0 ? loaded->error : EIO;
        return;
    }
    out.restore = true;
    out.memory = loaded->memory;  // Empty: nothing remembered, every drive stays empty
    out.saveAllowed = saverReady;
    out.refusal = saverReady ? 0 : ENOMEM;
}

bool DriveMemoryLink::apply(MenuController& controller, const DriveMemoryPlan& plan, bool catalogApplied)
{
    if (applied_) {
        return false;
    }
    if (!plan.restore) {
        controller.restoreFailed(plan.message);
    } else if (catalogApplied) {
        controller.restore(plan.memory);
    } else {
        return false;
    }
    applied_ = true;
    return true;
}

void DriveMemoryLink::saves(MenuController& controller, const DriveMemoryPlan& plan, Saver& saver) noexcept
{
    if (!applied_) {
        return;
    }
    if (controller.memoryChanged()) {
        const uint32_t version = controller.snapshot(scratch_);
        if (plan.saveAllowed) {
            saver.submit(version, scratch_);
            ++submitted_;
        } else {
            controller.saveResult(version, false, plan.refusal);  // NO SE GUARDO now, never GUARDANDO forever
        }
    }
    if (!plan.saveAllowed) {
        return;  // no worker this session: nothing to poll
    }
    uint32_t version = 0;
    bool ok = false;
    int error = 0;
    while (saver.poll(version, ok, error)) {
        controller.saveResult(version, ok, error);
    }
}

}  // namespace rb::host
