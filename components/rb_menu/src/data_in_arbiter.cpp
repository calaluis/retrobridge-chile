#include "rb/menu/data_in_arbiter.hpp"

namespace rb::host {

bool DataInArbiter::wantsTape(const DataInInputs& in) noexcept
{
    // Rule 1, by state: MOTOR on (already, or now), a ready tape and both gates.
    return in.gates && in.motorOn && in.tapeReady && !in.tapeEnded && !in.poweredOff && !in.tapeStopped;
}

bool DataInArbiter::givesBack(const DataInInputs& in) noexcept
{
    // Rule 2: MOTOR pauses keep the tape. Rule 3: these give the line back to the disk.
    return !in.gates || in.poweredOff || in.tapeStopped || (!in.motorOn && (in.commandLow || in.tapeEnded));
}

DataInChange DataInArbiter::step(const DataInInputs& in, uint64_t nowUs) noexcept
{
    switch (owner_) {
    case DataInOwner::Disk: {
        if (!wantsTape(in)) {
            return DataInChange::None;  // takeFailed_ stays: a MOTOR flicker must not skip the wait
        }
        // Review of PR #85: after a failed take, one new attempt per 100 ms, never one per step.
        if (takeFailed_ && !due(nowUs)) {
            return DataInChange::None;
        }
        // Plan §2.4 step 1: the disk parks only with its TX empty; otherwise, next step.
        if (!disk_.park()) {
            return DataInChange::None;
        }
        owner_ = DataInOwner::TakePending;
        sinceUs_ = nowUs;
        const LineResult taken = tape_.take();
        return taken == LineResult::Pending ? DataInChange::TakeRequested : onTake(taken, nowUs);
    }

    case DataInOwner::TakePending: {
        // The deadline first: an ack that comes at or after it is ignored (plan §2.4).
        if (nowUs - sinceUs_ >= kTakeTimeoutUs) {
            takeFailed_ = true;
            lastTryUs_ = nowUs;
            const DataInChange released = startRelease(nowUs);  // the take becomes a release
            return released == DataInChange::ReleaseRequested ? DataInChange::TakeTimedOut : released;
        }
        if (givesBack(in)) {
            return startRelease(nowUs);
        }
        const LineResult taken = tape_.take();
        return taken == LineResult::Pending ? DataInChange::None : onTake(taken, nowUs);
    }

    case DataInOwner::Tape:
        return givesBack(in) ? startRelease(nowUs) : DataInChange::None;

    case DataInOwner::ReleasePending: {
        if (nowUs - sinceUs_ >= kReleaseTimeoutUs) {
            // Never resume the disk: the tape may still drive GPIO21 (R1). Ask again every 100 ms.
            tape_.abandon();
            owner_ = DataInOwner::Fault;
            retrying_ = false;
            lastTryUs_ = nowUs;
            return DataInChange::ReleaseTimedOut;
        }
        const LineResult released = tape_.release();
        return released == LineResult::Pending ? DataInChange::None : onRelease(released, nowUs);
    }

    case DataInOwner::Fault:
    case DataInOwner::Released:
        return retryRelease(nowUs);

    case DataInOwner::DiskFault:
    default:
        // The tape gave the line back, but the disk's line is not right yet: D1 silent, retry.
        // MOTOR is not looked at: only Disk may ask for the tape.
        if (!due(nowUs)) {
            return DataInChange::None;
        }
        if (disk_.resume() == LineResult::Done) {
            owner_ = DataInOwner::Disk;
            return DataInChange::ToDisk;
        }
        lastTryUs_ = nowUs;
        return DataInChange::None;
    }
}

DataInChange DataInArbiter::onTake(LineResult taken, uint64_t nowUs) noexcept
{
    if (taken == LineResult::Done) {
        takeFailed_ = false;
        owner_ = DataInOwner::Tape;
        return DataInChange::ToTape;
    }
    // Rule 5: refused (the tape never took the line): the disk resumes at once.
    takeFailed_ = true;
    lastTryUs_ = nowUs;
    return resumeDisk(nowUs, DataInChange::TakeFailed);
}

DataInChange DataInArbiter::startRelease(uint64_t nowUs) noexcept
{
    owner_ = DataInOwner::ReleasePending;
    sinceUs_ = nowUs;
    const LineResult released = tape_.release();
    return released == LineResult::Pending ? DataInChange::ReleaseRequested : onRelease(released, nowUs);
}

DataInChange DataInArbiter::onRelease(LineResult released, uint64_t nowUs) noexcept
{
    retrying_ = false;
    switch (released) {
    case LineResult::Done:
        return resumeDisk(nowUs, DataInChange::ToDisk);
    case LineResult::Released:
        lastTryUs_ = nowUs;
        if (owner_ == DataInOwner::Released) {
            return DataInChange::None;  // one event per change of state, not per retry
        }
        owner_ = DataInOwner::Released;
        return DataInChange::Released;
    case LineResult::Failed:
    case LineResult::Pending:
    default:
        lastTryUs_ = nowUs;
        if (owner_ == DataInOwner::Fault) {
            return DataInChange::None;
        }
        owner_ = DataInOwner::Fault;
        return DataInChange::ReleaseFailed;
    }
}

DataInChange DataInArbiter::retryRelease(uint64_t nowUs) noexcept
{
    if (!retrying_) {
        if (!due(nowUs)) {
            return DataInChange::None;
        }
        retrying_ = true;  // release() publishes a new request: the expired one is ignored
        sinceUs_ = nowUs;
    } else if (nowUs - sinceUs_ >= kReleaseTimeoutUs) {
        tape_.abandon();
        retrying_ = false;
        lastTryUs_ = nowUs;
        return DataInChange::None;  // still Fault or Released: the console already knows
    }
    const LineResult released = tape_.release();
    return released == LineResult::Pending ? DataInChange::None : onRelease(released, nowUs);
}

DataInChange DataInArbiter::resumeDisk(uint64_t nowUs, DataInChange onDone) noexcept
{
    if (disk_.resume() == LineResult::Done) {
        owner_ = DataInOwner::Disk;
        return onDone;
    }
    owner_ = DataInOwner::DiskFault;
    lastTryUs_ = nowUs;
    return DataInChange::ResumeFailed;
}

const char* dataInOwnerName(DataInOwner owner) noexcept
{
    switch (owner) {
    case DataInOwner::Disk:
        return "disco";
    case DataInOwner::TakePending:
        return "esperando a la cinta";
    case DataInOwner::Tape:
        return "cinta";
    case DataInOwner::ReleasePending:
        return "esperando que la cinta suelte";
    case DataInOwner::Fault:
        return "FALLO: la cinta no suelta (reintento cada 100 ms)";
    case DataInOwner::Released:
        return "SUELTA: nadie transmite (reintento cada 100 ms)";
    case DataInOwner::DiskFault:
    default:
        return "FALLO del disco: D1 en silencio (reintento cada 100 ms)";
    }
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
