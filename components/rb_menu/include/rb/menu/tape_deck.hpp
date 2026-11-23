#pragma once

// The cassette as the menu sees it (spec menu-cinta, plan §3). Every call runs on the
// menu (SIO) thread and returns without waiting: reading a tape happens elsewhere, and
// the menu polls. A tape becomes the one on C: only when poll() says Ready; a tape
// that cannot be read (Invalid, NoMemory) or a cancelled load leaves the deck holding
// the tape it had (AC6).

#include <cstdint>

namespace rb::host {

enum class TapeState : uint8_t {
    Idle,      // no load in progress
    Pending,   // the tape asked for is being read
    Ready,     // the tape asked for is now the one on C:
    Invalid,   // not a tape this cassette can play
    NoMemory,  // no room to hold the tape
    // The cassette did not answer: nobody knows whether it took the tape. C: does not change
    // (review 2 of PR #85).
    Unconfirmed,
};

class TapeDeck {
public:
    TapeDeck(const TapeDeck&) = delete;
    TapeDeck& operator=(const TapeDeck&) = delete;

    // Starts reading the tape `name` (a name of the microSD root). False: busy, nothing started.
    virtual bool mount(const char* name) noexcept = 0;
    // The state of the load mount() started; Idle when none is in progress.
    virtual TapeState poll() noexcept = 0;
    // Drops the load in progress; the tape on C: stays.
    virtual void cancel() noexcept = 0;
    // Takes the tape out of C:.
    virtual void eject() noexcept = 0;
    // True if this package may play a tape (M3a and H4 approved): without them a tape can be
    // put on C: but never plays (AC8).
    virtual bool gatesOpen() const noexcept = 0;

protected:
    TapeDeck() = default;
    ~TapeDeck() = default;
};

}  // namespace rb::host
