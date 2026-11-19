#pragma once

// A DatagramPort in front of FakeTnfsServer that holds every request of one
// TNFS command until the test lets it go (menu-tnfs T011, T012). It makes the
// in-progress states of the loading thread observable without timing guesses:
// the server runs on a FakeClock, so its waits cost no real time.

#include <atomic>
#include <chrono>
#include <condition_variable>
#include <cstdint>
#include <mutex>

#include "fakes/fake_tnfs_server.hpp"
#include "rb/netsio/datagram_port.hpp"
#include "unity.h"

namespace rb::test {

class GatedPort final : public rb::netsio::DatagramPort {
public:
    explicit GatedPort(FakeTnfsServer& server) : server_(server) {}

    void gate(uint8_t command) { gated_ = command; gating_ = true; }

    rb::core::Result<void> send(rb::core::Span<const uint8_t> datagram) override
    {
        if (gating_ && datagram.size() > 3 && datagram[3] == gated_) {
            std::unique_lock<std::mutex> lock(mutex_);
            ++arrivals_;
            changed_.notify_all();
            changed_.wait(lock, [this] { return permits_ > 0; });
            --permits_;
        }
        return server_.send(datagram);
    }
    rb::core::Result<std::size_t> receive(rb::core::Span<uint8_t> out, uint32_t timeoutUs) override
    {
        return server_.receive(out, timeoutUs);
    }

    // Waits (real time) until the next gated request arrives and is held. It
    // counts arrivals, not "someone is held", so it never runs ahead.
    void awaitHeld()
    {
        std::unique_lock<std::mutex> lock(mutex_);
        const int target = ++awaited_;
        TEST_ASSERT_TRUE_MESSAGE(
            changed_.wait_for(lock, std::chrono::seconds(10), [this, target] { return arrivals_ >= target; }),
            "no held request");
    }
    void permit(int count = 1)
    {
        std::lock_guard<std::mutex> lock(mutex_);
        permits_ += count;
        changed_.notify_all();
    }
    void open()
    {
        gating_ = false;
        permit(1000000);
    }

private:
    FakeTnfsServer& server_;
    std::atomic<bool> gating_{false};
    uint8_t gated_ = 0;
    std::mutex mutex_;
    std::condition_variable changed_;
    int arrivals_ = 0;  // gated requests that reached send()
    int awaited_ = 0;   // arrivals the test has waited for
    int permits_ = 0;
};

}  // namespace rb::test
