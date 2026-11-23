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
        if (!wantTape) {
            return DataInChange::None;  // takeFailed_ stays: a MOTOR flicker must not skip the wait
        }
        // Review of PR #85: after a failed take the disk keeps UART2; one new attempt per 100 ms,
        // never an end/take/begin every step (each one drops SIO frames).
        if (takeFailed_ && !due(nowUs)) {
            return DataInChange::None;
        }
        return toTape(nowUs);
    }
    }
}

DataInChange DataInArbiter::toTape(uint64_t nowUs) noexcept
{
    disk_.end();
    if (tape_.take()) {
        takeFailed_ = false;
        owner_ = DataInOwner::Tape;
        return DataInChange::ToTape;
    }
    takeFailed_ = true;
    lastTryUs_ = nowUs;
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

namespace rb::host {

DataInInputs dataInInputs(const DataInReadings& readings) noexcept
{
    DataInInputs in;
    in.gates = readings.gatesOpen && readings.tapeOutput;  // rule 4: otherwise the disk never lets go
    in.motorOn = readings.motorOn;
    in.commandLow = !readings.commandHigh;
    in.tapeReady = readings.tapeOnC;
    in.tapeEnded = readings.tapeEnded;
    in.poweredOff = readings.atariOff;
    in.tapeStopped = !readings.tapeOnC;  // ejected: the tape gives the line back (rule 3)
    return in;
}

}  // namespace rb::host
