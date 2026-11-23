#include "rb/menu/data_in_arbiter.hpp"

namespace rb::host {

DataInChange DataInArbiter::step(const DataInInputs& in, uint64_t nowUs) noexcept
{
    switch (owner_) {
    case DataInOwner::None:
        // Rule 5: a failed begin. Only the disk may come back, every 100 ms; never the tape.
        return due(nowUs) ? beginDisk(nowUs) : DataInChange::None;

    case DataInOwner::Tape: {
        if (releasePending_) {
            return due(nowUs) ? toDisk(nowUs) : DataInChange::None;
        }
        // Rule 2: MOTOR pauses keep the tape. Rule 3: these give the line back to the disk.
        const bool giveBack = !in.gates || in.poweredOff || in.tapeStopped
                              || (!in.motorOn && (in.commandLow || in.tapeEnded));
        return giveBack ? toDisk(nowUs) : DataInChange::None;
    }

    case DataInOwner::Disk:
    default: {
        // Rule 1, by state: MOTOR on (already, or now), a ready tape and both gates.
        const bool wantTape = in.gates && in.motorOn && in.tapeReady && !in.tapeEnded
                              && !in.poweredOff && !in.tapeStopped;
        return wantTape ? toTape(nowUs) : DataInChange::None;
    }
    }
}

DataInChange DataInArbiter::toTape(uint64_t nowUs) noexcept
{
    disk_.end();
    if (tape_.take()) {
        owner_ = DataInOwner::Tape;
        return DataInChange::ToTape;
    }
    // Rule 5: the tape could not install the line: the disk begins again at once.
    const DataInChange begun = beginDisk(nowUs);
    return begun == DataInChange::ToDisk ? DataInChange::TakeFailed : begun;
}

DataInChange DataInArbiter::toDisk(uint64_t nowUs) noexcept
{
    if (!tape_.release()) {
        // The tape may still hold UART2: the disk must not begin. Retry the release.
        releasePending_ = true;
        lastTryUs_ = nowUs;
        return DataInChange::ReleaseFailed;
    }
    releasePending_ = false;
    return beginDisk(nowUs);
}

DataInChange DataInArbiter::beginDisk(uint64_t nowUs) noexcept
{
    if (disk_.begin()) {
        owner_ = DataInOwner::Disk;
        return DataInChange::ToDisk;
    }
    owner_ = DataInOwner::None;
    lastTryUs_ = nowUs;
    return DataInChange::BeginFailed;
}

}  // namespace rb::host
