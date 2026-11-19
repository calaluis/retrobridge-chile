#pragma once

#include <cstdint>
#include <cstdio>
#include <mutex>

#include "rb/core/span.hpp"
#include "rb/netsio/netsio_sio_transport.hpp"
#include "rb/sio/sio_session.hpp"

namespace rb::host {

// Evidence log, one JSON object per line (JSONL), flushed after each line so a
// killed process still leaves a usable log. Every datagram in both directions,
// every non-idle session step and a final summary of NetSioStats.
// "utc" is the PC wall clock and "mono_us" the HostClock: neither is SIO time.
// Thread-safe (menu-tnfs T013): the TNFS loader thread writes events too, so
// every line is written whole under a lock, never mixed with another.
class JsonlLog final : public rb::netsio::NetSioObserver {
public:
    explicit JsonlLog(std::FILE* file, const rb::app::Clock& clock) noexcept : file_(file), clock_(clock) {}

    void onDatagram(bool outbound, rb::core::Span<const uint8_t> datagram) override;
    void step(rb::sio::StepOutcome outcome);
    void event(const char* name, const char* detail);
    void summary(const rb::netsio::NetSioStats& stats);

private:
    void begin(const char* kind);
    void end();

    std::FILE* file_;
    const rb::app::Clock& clock_;
    std::mutex mutex_;  // one line at a time
};

const char* messageName(uint8_t id) noexcept;
const char* outcomeName(rb::sio::StepOutcome outcome) noexcept;

}  // namespace rb::host
