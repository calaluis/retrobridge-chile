#include "rb/menu/net_restore_queue.hpp"

namespace rb::host {

namespace {

// Time from `since` to `now`; 0 if the clock reads earlier (never negative).
uint64_t elapsed(uint64_t since, uint64_t now) noexcept { return now > since ? now - since : 0; }

}  // namespace

void NetRestoreQueue::begin(const DriveMemory& memory, uint64_t nowMs) noexcept
{
    drives_.fill(Entry{});
    beginMs_ = nowMs;
    networkSeen_ = false;
    for (unsigned drive = 1; drive <= kDrives; ++drive) {
        const DriveMemory::Origin origin = memory.origin(drive);
        if (origin == DriveMemory::Origin::NetAtr || origin == DriveMemory::Origin::NetXex) {
            drives_[drive - 1].state = State::Pending;
        }
    }
}

unsigned NetRestoreQueue::next(bool networkReady, uint64_t nowMs) noexcept
{
    if (!networkReady) {
        return 0;
    }
    // Ready after its 30 s is too late: the waiting drives are expired()'s (SIN RED), even if
    // next() is asked first (review of af5974c).
    if (!networkSeen_ && wifiLate(nowMs)) {
        return 0;
    }
    networkSeen_ = true;
    unsigned first = 0;
    for (unsigned drive = kDrives; drive >= 1; --drive) {
        const State state = drives_[drive - 1].state;
        if (state == State::Loading) {
            return 0;  // one at a time
        }
        if (state == State::Pending) {
            first = drive;
        }
    }
    return first;
}

bool NetRestoreQueue::started(unsigned drive, uint32_t job, uint64_t nowMs) noexcept
{
    if (!valid(drive) || job == 0 || drives_[drive - 1].state != State::Pending) {
        return false;
    }
    Entry& entry = drives_[drive - 1];
    entry.state = State::Loading;
    entry.job = job;
    entry.bytes = 0;
    entry.lastMs = nowMs;
    return true;
}

void NetRestoreQueue::progressed(unsigned drive, uint32_t bytes, uint64_t nowMs) noexcept
{
    if (!valid(drive) || drives_[drive - 1].state != State::Loading) {
        return;
    }
    Entry& entry = drives_[drive - 1];
    if (bytes > entry.bytes) {
        entry.bytes = bytes;
        entry.lastMs = nowMs;
    }
}

bool NetRestoreQueue::done(unsigned drive) noexcept
{
    if (!valid(drive) || drives_[drive - 1].state != State::Loading) {
        return false;
    }
    drives_[drive - 1] = Entry{};
    drives_[drive - 1].state = State::Mounted;
    return true;
}

bool NetRestoreQueue::failed(unsigned drive, Reason reason) noexcept
{
    if (!valid(drive) || drives_[drive - 1].state != State::Loading || reason == Reason::None) {
        return false;
    }
    drives_[drive - 1] = Entry{};
    drives_[drive - 1].state = State::Failed;
    drives_[drive - 1].reason = reason;
    return true;
}

void NetRestoreQueue::cancel(unsigned drive) noexcept
{
    if (valid(drive)) {
        drives_[drive - 1] = Entry{};
    }
}

bool NetRestoreQueue::requeue(unsigned drive) noexcept
{
    if (!valid(drive) || drives_[drive - 1].state != State::Loading) {
        return false;
    }
    drives_[drive - 1] = Entry{};
    drives_[drive - 1].state = State::Pending;
    return true;
}

uint8_t NetRestoreQueue::expired(uint64_t nowMs) noexcept
{
    const bool late = !networkSeen_ && wifiLate(nowMs);
    uint8_t mask = 0;
    for (unsigned drive = 1; drive <= kDrives; ++drive) {
        Entry& entry = drives_[drive - 1];
        if ((entry.state == State::Pending && late) ||
            (entry.state == State::Loading && elapsed(entry.lastMs, nowMs) >= kSilenceMs)) {
            entry.state = State::Failed;
            entry.reason = Reason::SinRed;  // the job stays: the caller cancels it with jobOf()
            mask = static_cast<uint8_t>(mask | (1u << (drive - 1)));
        }
    }
    return mask;
}

bool NetRestoreQueue::wifiLate(uint64_t nowMs) const noexcept { return elapsed(beginMs_, nowMs) >= kWifiMs; }

NetRestoreQueue::State NetRestoreQueue::state(unsigned drive) const noexcept
{
    return valid(drive) ? drives_[drive - 1].state : State::None;
}

NetRestoreQueue::Reason NetRestoreQueue::reason(unsigned drive) const noexcept
{
    return valid(drive) ? drives_[drive - 1].reason : Reason::None;
}

uint32_t NetRestoreQueue::jobOf(unsigned drive) const noexcept { return valid(drive) ? drives_[drive - 1].job : 0; }

unsigned NetRestoreQueue::driveOf(uint32_t job) const noexcept
{
    if (job == 0) {
        return 0;
    }
    for (unsigned drive = 1; drive <= kDrives; ++drive) {
        if (drives_[drive - 1].job == job && drives_[drive - 1].state == State::Loading) {
            return drive;
        }
    }
    return 0;
}

bool NetRestoreQueue::busy() const noexcept
{
    for (const Entry& entry : drives_) {
        if (entry.state == State::Pending || entry.state == State::Loading) {
            return true;
        }
    }
    return false;
}

}  // namespace rb::host
