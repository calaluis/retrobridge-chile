#pragma once

// What the firmware does with the drive memory between finish() and the SIO task
// (menu-unidades-persistentes T6), without FreeRTOS, so the host tests run it.
//
//   planDriveMemory()        finish(): what was read becomes a plan, published once
//   DriveMemoryLink::apply() SIO task, between frames: restore or restoreFailed, once
//   DriveMemoryLink::saves() SIO task, between frames: a changed memory goes to the save worker,
//                            or is refused at once when saving is off for this session
//
// Saving is on only when the worker runs and the first read of the slots gave Loaded or Empty.
// After a card failure or a slot read error it stays off for the whole session: a save then
// could replace the remembered drives with this session's (the store retries the read before
// a save, so a card that recovers would otherwise let it through).

#include <cstdint>

#include "rb/menu/drive_memory.hpp"
#include "rb/menu/drive_slot_store.hpp"

namespace rb::host {

class MenuController;

struct DriveMemoryPlan {
    bool restore = false;   // true: restore(memory); false: restoreFailed(message)
    DriveMemory memory;
    char message[48] = "";  // " MICROSD: <reason>" or " MEMORIA: <reason>"
    bool saveAllowed = false;
    int refusal = 0;        // errno that saveResult() reports when !saveAllowed
};

// `cardFailure`: why the microSD or its catalog could not be used (nullptr: it could), with
// `cardError` its errno. `loaded`: the first load() of the slots, nullptr when not read.
// `saverReady`: the save worker runs.
void planDriveMemory(const char* cardFailure, int cardError, const SlotFileStore::LoadResult* loaded,
                     bool saverReady, DriveMemoryPlan& out) noexcept;

class DriveMemoryLink {
public:
    // The save worker, seen from the SIO task. Neither call may block.
    class Saver {
    public:
        virtual void submit(uint32_t version, const DriveMemory& memory) noexcept = 0;
        // One result, oldest first; false when there is none.
        virtual bool poll(uint32_t& version, bool& ok, int& error) noexcept = 0;

    protected:
        ~Saver() = default;
    };

    // Restores once `catalogApplied` (names are looked up in the catalog) or fails at once.
    // True only on the call that applied it.
    bool apply(MenuController& controller, const DriveMemoryPlan& plan, bool catalogApplied);
    bool applied() const noexcept { return applied_; }

    // After apply(): hands a changed memory to `saver`, or refuses it, and gives results back.
    // Counts what it submitted.
    void saves(MenuController& controller, const DriveMemoryPlan& plan, Saver& saver) noexcept;
    uint32_t submitted() const noexcept { return submitted_; }

private:
    bool applied_ = false;
    uint32_t submitted_ = 0;
    DriveMemory scratch_;  // ~1 KB: the object lives outside the SIO task's stack
};

}  // namespace rb::host
