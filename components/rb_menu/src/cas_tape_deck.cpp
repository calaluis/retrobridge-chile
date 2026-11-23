#include "rb/menu/cas_tape_deck.hpp"

#include <cstdio>
#include <cstring>

namespace rb::host {

namespace {

constexpr std::size_t kLineMax = 160;    // CassetteService::kLineMax
constexpr std::size_t kReplyMax = 160;   // CassetteService::kReplyMax
constexpr std::size_t kCasNameMax = 63;  // CassetteCommands::kNameMax without the NUL: longer names are cut

bool startsWith(const char* text, const char* prefix) noexcept
{
    return std::strncmp(text, prefix, std::strlen(prefix)) == 0;
}

void copyName(char (&out)[CasTapeDeck::kNameMax], const char* name) noexcept
{
    std::snprintf(out, sizeof out, "%s", name == nullptr ? "" : name);
}

}  // namespace

CasMountReply classifyMountReply(const char* reply) noexcept
{
    if (reply == nullptr || reply[0] == '\0') {
        return CasMountReply::NoReply;
    }
    if (startsWith(reply, "mounted ")) {
        return CasMountReply::Mounted;
    }
    if (startsWith(reply, "busy")) {
        return CasMountReply::Busy;
    }
    if (std::strstr(reply, ": too many blocks") != nullptr) {
        return CasMountReply::TooBig;
    }
    return CasMountReply::Invalid;
}

bool statusHolds(const char* reply, const char* name) noexcept
{
    // "cas status": "<name>: segment ..." or "<name>: end, ..."; "no tape, ..." when empty.
    if (reply == nullptr || name == nullptr || name[0] == '\0') {
        return false;
    }
    std::size_t length = std::strlen(name);
    if (length > kCasNameMax) {
        length = kCasNameMax;
    }
    return std::strncmp(reply, name, length) == 0 && reply[length] == ':';
}

bool CasTapeDeck::mount(const char* name) noexcept
{
    pump();
    if (inFlight_ != Op::None || restoreWaiting_ || ejectWaiting_ || report_ != TapeState::Idle) {
        return false;
    }
    if (!channel_.available()) {
        report_ = TapeState::NoMemory;
        return true;
    }
    if (name == nullptr || name[0] == '\0' || std::strlen(name) >= kNameMax) {
        report_ = TapeState::Invalid;
        return true;
    }
    copyName(asked_, name);
    copyName(previous_, loaded_);
    cancelled_ = false;
    busyLeft_ = kBusyRetries;
    statusLeft_ = kStatusRetries;
    return post(Op::Mount, "mount", asked_);
}

TapeState CasTapeDeck::poll() noexcept
{
    pump();
    if (report_ != TapeState::Idle) {
        const TapeState state = report_;
        report_ = TapeState::Idle;
        return state;
    }
    const bool loading = inFlight_ == Op::Mount || inFlight_ == Op::MountStatus;
    return loading && !cancelled_ ? TapeState::Pending : TapeState::Idle;
}

void CasTapeDeck::cancel() noexcept
{
    if (inFlight_ == Op::Mount || inFlight_ == Op::MountStatus) {
        cancelled_ = true;
    }
    if (report_ == TapeState::Ready) {
        // pump() already took "mounted", poll() has not handed it out: the cassette holds the new
        // tape the menu no longer wants (review of PR #85, 2).
        undo();
        sendWaiting();
    }
    report_ = TapeState::Idle;
}

void CasTapeDeck::eject() noexcept
{
    loaded_[0] = '\0';
    restoreWaiting_ = false;
    ejectWaiting_ = true;
    busyLeft_ = kBusyRetries;
    sendWaiting();
}

void CasTapeDeck::pump() noexcept
{
    if (inFlight_ != Op::None) {
        char reply[kReplyMax];
        if (channel_.receive(reply, sizeof reply)) {
            onReply(reply);
        }
    }
    sendWaiting();
}

bool CasTapeDeck::post(Op op, const char* verb, const char* name) noexcept
{
    char line[kLineMax];
    if (name == nullptr) {
        std::snprintf(line, sizeof line, "%s", verb);
    } else {
        std::snprintf(line, sizeof line, "%s %s", verb, name);
    }
    if (!channel_.send(line)) {
        return false;
    }
    inFlight_ = op;
    return true;
}

void CasTapeDeck::onReply(const char* reply) noexcept
{
    const Op done = inFlight_;
    inFlight_ = Op::None;
    const CasMountReply kind = classifyMountReply(reply);
    switch (done) {
    case Op::Mount:
        if (kind == CasMountReply::Busy && busyLeft_ > 0) {
            --busyLeft_;
            (void)post(Op::Mount, "mount", asked_);
            return;
        }
        if (kind == CasMountReply::NoReply) {
            // The service may still finish the mount: ask what the cassette holds (review 3).
            (void)post(Op::MountStatus, "status", nullptr);
            return;
        }
        mountDone(kind == CasMountReply::Mounted, kind);
        return;
    case Op::MountStatus:
        if (kind == CasMountReply::NoReply && statusLeft_ > 0) {
            --statusLeft_;
            (void)post(Op::MountStatus, "status", nullptr);
            return;
        }
        mountDone(statusHolds(reply, asked_), CasMountReply::Invalid);
        return;
    case Op::Restore:
        if (kind == CasMountReply::Busy && busyLeft_ > 0) {
            --busyLeft_;
            (void)post(Op::Restore, "mount", previous_);
            return;
        }
        if (kind == CasMountReply::NoReply) {
            (void)post(Op::RestoreStatus, "status", nullptr);
            return;
        }
        restoreDone(kind == CasMountReply::Mounted);
        return;
    case Op::RestoreStatus:
        if (kind == CasMountReply::NoReply && statusLeft_ > 0) {
            --statusLeft_;
            (void)post(Op::RestoreStatus, "status", nullptr);
            return;
        }
        restoreDone(statusHolds(reply, previous_));
        return;
    case Op::Eject:
        if ((kind == CasMountReply::Busy || kind == CasMountReply::NoReply) && busyLeft_ > 0) {
            --busyLeft_;
            ejectWaiting_ = true;  // the tape may still be in: eject again (review 1)
        }
        return;
    case Op::None:
    default:
        return;
    }
}

void CasTapeDeck::mountDone(bool taken, CasMountReply kind) noexcept
{
    if (cancelled_) {
        cancelled_ = false;
        if (taken) {
            undo();
        }
        return;
    }
    if (taken) {
        copyName(loaded_, asked_);
        report_ = TapeState::Ready;
        return;
    }
    report_ = kind == CasMountReply::TooBig ? TapeState::NoMemory : TapeState::Invalid;
}

void CasTapeDeck::restoreDone(bool taken) noexcept
{
    if (taken) {
        copyName(loaded_, previous_);
        return;
    }
    // The old tape is gone: the cassette still holds the one the menu dropped. Take it out.
    loaded_[0] = '\0';
    ejectWaiting_ = true;
    busyLeft_ = kBusyRetries;
}

void CasTapeDeck::undo() noexcept
{
    copyName(loaded_, previous_);
    busyLeft_ = kBusyRetries;
    statusLeft_ = kStatusRetries;
    if (previous_[0] != '\0') {
        restoreWaiting_ = true;
    } else {
        ejectWaiting_ = true;
    }
}

void CasTapeDeck::sendWaiting() noexcept
{
    if (inFlight_ != Op::None) {
        return;
    }
    if (restoreWaiting_) {
        if (post(Op::Restore, "mount", previous_)) {
            restoreWaiting_ = false;
        }
        return;
    }
    if (ejectWaiting_ && post(Op::Eject, "eject", nullptr)) {
        ejectWaiting_ = false;
    }
}

}  // namespace rb::host
