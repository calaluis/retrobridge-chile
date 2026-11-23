#pragma once

// CasTapeDeck (spec menu-cinta, plan §3, T5): the menu's TapeDeck over the `cas` commands of the
// cassette service. The service answers a command in up to seconds (it indexes the tape), so the
// commands travel through a CasChannel that never waits: send() posts one line, receive() takes
// the reply once it is there. On the ESP32 a small task does the waiting (main/esp_tape_deck).
//
// Everything here runs on the menu (SIO) thread. Pure, no allocation.
//
//   mount(name)  -> "mount <name>"; "mounted ..." = Ready, "too many blocks" = NoMemory,
//                   "busy..." (the last tape is still closing) = sent again, anything else = Invalid.
//                   No reply in time: the service keeps the command and may still finish it, so
//                   "status" is asked (it runs after the mount, on the same loop) and the tape it
//                   names decides. A service that did not start (no PSRAM) answers NoMemory at once.
//   cancel()     -> the load is dropped, even if its result was already taken but not yet handed
//                   out by poll(); if the cassette took the new tape, the tape that was on C: goes
//                   back ("mount <old>", or "eject" if there was none), so C: stays (AC6).
//   eject()      -> "eject", once nothing else is in flight; sent again on "busy" or no reply.
//   pump()       -> takes replies and sends what waits; poll() pumps too. Call it every SIO step.
//
// Review of PR #85 (T5): the deck never says a tape that the cassette does not hold.

#include <cstddef>

#include "rb/menu/tape_deck.hpp"

namespace rb::host {

class CasChannel {
public:
    CasChannel(const CasChannel&) = delete;
    CasChannel& operator=(const CasChannel&) = delete;

    // The cassette service is running (its PSRAM bank was reserved).
    virtual bool available() const noexcept = 0;
    // Posts `args` (what follows "cas"). False: a command is still in flight, nothing posted.
    virtual bool send(const char* args) noexcept = 0;
    // The reply to the command in flight, once it is there. False: not yet. A command that got
    // no reply in time returns true with an empty reply.
    virtual bool receive(char* reply, std::size_t size) noexcept = 0;

protected:
    CasChannel() = default;
    ~CasChannel() = default;
};

enum class CasMountReply { Mounted, Busy, TooBig, NoReply, Invalid };

// What a `cas mount` reply says.
CasMountReply classifyMountReply(const char* reply) noexcept;

// True if a `cas status` reply names `name` as the tape the cassette holds.
bool statusHolds(const char* reply, const char* name) noexcept;

class CasTapeDeck final : public TapeDeck {
public:
    static constexpr std::size_t kNameMax = 128;  // with the NUL
    static constexpr int kBusyRetries = 20;       // "busy" replies before a command gives up
    static constexpr int kStatusRetries = 5;      // "status" with no reply before the load gives up

    CasTapeDeck(CasChannel& channel, bool gatesOpen) noexcept : channel_(channel), gates_(gatesOpen) {}

    bool mount(const char* name) noexcept override;
    TapeState poll() noexcept override;
    void cancel() noexcept override;
    void eject() noexcept override;
    bool gatesOpen() const noexcept override { return gates_; }

    void pump() noexcept;

    // The tape the cassette holds, as far as this deck knows ("" = none).
    const char* loaded() const noexcept { return loaded_; }

private:
    enum class Op { None, Mount, MountStatus, Restore, RestoreStatus, Eject };

    bool post(Op op, const char* verb, const char* name) noexcept;
    void onReply(const char* reply) noexcept;
    void mountDone(bool taken, CasMountReply kind) noexcept;
    void restoreDone(bool taken) noexcept;
    void undo() noexcept;  // the cassette holds asked_, the menu does not want it
    void sendWaiting() noexcept;

    CasChannel& channel_;
    bool gates_;
    Op inFlight_ = Op::None;
    bool cancelled_ = false;
    bool restoreWaiting_ = false;
    bool ejectWaiting_ = false;
    int busyLeft_ = 0;
    int statusLeft_ = 0;
    TapeState report_ = TapeState::Idle;  // a result poll() has not handed out yet
    char asked_[kNameMax] = {};
    char previous_[kNameMax] = {};  // the tape on C: when mount() was called
    char loaded_[kNameMax] = {};
};

}  // namespace rb::host
