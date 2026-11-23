#include "rb/menu/cas_tape_deck.hpp"

#include <cstdio>
#include <cstring>

namespace rb::host {

namespace {

constexpr std::size_t kLineMax = 160;    // CassetteService::kLineMax
constexpr std::size_t kReplyMax = 160;   // CassetteService::kReplyMax

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
        return CasMountReply::Invalid;  // no reply in time: the menu says the tape is not valid
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

bool CasTapeDeck::mount(const char* name) noexcept
{
    pump();
    if (inFlight_ != Op::None || ejectWaiting_ || report_ != TapeState::Idle) {
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
    cancelled_ = false;
    busyLeft_ = kBusyRetries;
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
    return inFlight_ == Op::Mount && !cancelled_ ? TapeState::Pending : TapeState::Idle;
}

void CasTapeDeck::cancel() noexcept
{
    if (inFlight_ == Op::Mount) {
        cancelled_ = true;
    }
    report_ = TapeState::Idle;
}

void CasTapeDeck::eject() noexcept
{
    loaded_[0] = '\0';
    ejectWaiting_ = true;
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
    if (done == Op::Eject) {
        return;
    }
    const CasMountReply kind = classifyMountReply(reply);
    if (done == Op::Restore) {
        if (kind != CasMountReply::Mounted) {
            // The old tape is gone: the cassette still holds the one the menu dropped. Take it out.
            loaded_[0] = '\0';
            ejectWaiting_ = true;
        }
        return;
    }
    // done == Op::Mount
    if (kind == CasMountReply::Busy && !cancelled_) {
        if (busyLeft_ > 0) {
            --busyLeft_;
            if (post(Op::Mount, "mount", asked_)) {
                return;
            }
        }
        report_ = TapeState::Invalid;
        return;
    }
    if (cancelled_) {
        cancelled_ = false;
        if (kind == CasMountReply::Mounted) {
            // The cassette took the tape the menu no longer wants: put C: back as it was (AC6).
            if (loaded_[0] != '\0') {
                (void)post(Op::Restore, "mount", loaded_);
            } else {
                (void)post(Op::Eject, "eject", nullptr);
            }
        }
        return;
    }
    switch (kind) {
    case CasMountReply::Mounted:
        copyName(loaded_, asked_);
        report_ = TapeState::Ready;
        break;
    case CasMountReply::TooBig:
        report_ = TapeState::NoMemory;
        break;
    default:
        report_ = TapeState::Invalid;
        break;
    }
}

void CasTapeDeck::sendWaiting() noexcept
{
    if (ejectWaiting_ && inFlight_ == Op::None && post(Op::Eject, "eject", nullptr)) {
        ejectWaiting_ = false;
    }
}

}  // namespace rb::host
