#pragma once

// StartupFlags (menu-cinta T6, review of PR #85): how the start-up task hands the SIO task the tape
// deck and the drive-memory plan. finish() publishes the deck first, then the plan, each with a
// release store. The SIO task must read the PLAN FIRST (acquire): if it sees the plan, everything
// published before it, the deck included, is visible too, so the restore of C: never runs without
// the deck. Reading the deck first lets both be published between the two reads: the plan is seen,
// the deck is not, and the remembered tape is restored with no cassette to ask.
//
// Pure, header-only, no allocation.

#include <atomic>

namespace rb::host {

class StartupFlags {
public:
    struct Seen {
        bool tape = false;  // the deck is published
        bool plan = false;  // the drive-memory plan is published
    };

    // Start-up task, in this order: the deck (if there is one), then the plan.
    void publishTape() noexcept { tape_.store(true, std::memory_order_release); }
    void publishPlan() noexcept { plan_.store(true, std::memory_order_release); }

    // SIO task, once per step. `between` runs between the two reads; tests use it to publish there.
    template <typename Between>
    Seen read(Between&& between) const noexcept
    {
        Seen seen;
        seen.plan = plan_.load(std::memory_order_acquire);
        between();
        seen.tape = tape_.load(std::memory_order_acquire);
        return seen;
    }
    Seen read() const noexcept
    {
        return read([] {});
    }

private:
    std::atomic<bool> tape_{false};
    std::atomic<bool> plan_{false};
};

}  // namespace rb::host
