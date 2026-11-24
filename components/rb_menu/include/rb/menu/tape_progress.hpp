#pragma once

// TapeProgress (spec menu-cinta, plan §2.4 and requirement 2, T9a.4): "the tape played to its end" crosses
// from the cassette task to the SIO task. The cassette publishes it on every tick from
// CassettePlayer::ended() (last part done AND its outputs empty); the SIO task reads it into
// DataInReadings.tapeEnded for rule 3 ("end of tape and MOTOR off -> disk"). One atomic: no lock, no
// allocation, never blocks either task.

#include <atomic>

namespace rb::host {

class TapeProgress {
public:
    // Cassette task, every tick.
    void publish(bool ended) noexcept { ended_.store(ended, std::memory_order_release); }

    // SIO task.
    bool ended() const noexcept { return ended_.load(std::memory_order_acquire); }

private:
    std::atomic<bool> ended_{false};
};

#ifndef ESP_PLATFORM
static_assert(std::atomic<bool>::is_always_lock_free, "TapeProgress must not take a lock");
#endif

}  // namespace rb::host
