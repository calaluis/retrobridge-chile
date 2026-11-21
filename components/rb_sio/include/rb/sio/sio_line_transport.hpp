#pragma once

#include <cstddef>
#include <cstdint>

#include "rb/app/ports/clock.hpp"
#include "rb/app/ports/sio_transport.hpp"
#include "rb/core/result.hpp"
#include "rb/core/span.hpp"

namespace rb::sio {

// The physical SIO line as the transport sees it (menu-sio-fisico): COMMAND as
// a level, DATA OUT of the Atari as received bytes, DATA IN as transmitted
// bytes. The ESP32 adapter (UART2 + GPIO39) implements it; host tests script it.
//
// Contract:
//   * waitCommandLevel(asserted, timeoutUs): true as soon as COMMAND is at that
//     level (asserted = low), false if it is not within timeoutUs.
//   * read(out, timeoutUs): up to out.size() bytes received within timeoutUs;
//     the count (0..size) or TransportError on a framing or overrun error.
//   * write(bytes): transmits all of them and returns after the last stop bit.
//   * flushInput(): discards everything received and not yet read.
//   * idleLine(): DATA IN back to idle (mark): the Atari's line is released.
class SioUartPort {
public:
    SioUartPort(const SioUartPort&) = delete;
    SioUartPort& operator=(const SioUartPort&) = delete;

    virtual bool waitCommandLevel(bool asserted, uint32_t timeoutUs) = 0;
    virtual rb::core::Result<std::size_t> read(rb::core::Span<uint8_t> out, uint32_t timeoutUs) = 0;
    virtual rb::core::Result<void> write(rb::core::Span<const uint8_t> bytes) = 0;
    virtual void flushInput() = 0;
    virtual void idleLine() = 0;

protected:
    SioUartPort() = default;
    ~SioUartPort() = default;  // never deleted through the interface
};

// What the console shows (`sio status`). Saturating is not needed at these
// rates within a session; plain counters.
struct SioLineStats {
    uint32_t frames = 0;          // 5 bytes inside a COMMAND window
    uint32_t shortFrames = 0;     // fewer than 5 bytes: passed on, the session ignores them
    uint32_t stuckLows = 0;       // COMMAND still low after the frame
    uint32_t receiveErrors = 0;   // framing or overrun
    uint32_t sendErrors = 0;
    uint32_t releases = 0;        // DATA IN released (every transaction end and every error)
    uint32_t lastAckLatencyUs = 0;  // COMMAND rise -> start of the first reply byte
    uint32_t maxAckLatencyUs = 0;
};

// SioTransport over the physical line (menu-sio-fisico T3). Pure logic: no
// ESP-IDF, no allocation (ADR-011).
//
//   * waitCommandFrame(): discards what is waiting (a byte outside a COMMAND
//     window is never part of a frame), waits for COMMAND low, reads up to 5
//     bytes and waits for COMMAND to rise. A short frame is returned as it is
//     (count < 5) so the session ignores it. A read error or COMMAND still low
//     -> TransportError, with the input discarded and DATA IN released.
//   * send(): the first send after a frame records the COMMAND -> reply
//     latency; a failed send releases DATA IN.
//   * receiveAfterAck(): ACK, then the data frame within its byte time plus a
//     margin; short or failed -> TransportError, DATA IN released.
//   * releaseDataLine(): DATA IN back to idle, always.
class SioLineTransport final : public rb::app::SioTransport {
public:
    static constexpr uint32_t kBaud = 19200;
    static constexpr uint32_t kFrameReadUs = 6000;     // 5 bytes take 2604 us at 19200
    static constexpr uint32_t kReleaseWaitUs = 2000;   // t1 is at most 950 us after the last byte
    static constexpr uint32_t kDataMarginUs = 10000;   // after the ACK, before the data frame ends

    SioLineTransport(SioUartPort& port, rb::app::Clock& clock) noexcept : port_(port), clock_(clock) {}

    rb::core::Result<rb::app::CommandBytes> waitCommandFrame(uint32_t timeoutUs) override;
    rb::core::Result<void> send(rb::core::Span<const uint8_t> bytes) override;
    rb::core::Result<void> receiveAfterAck(rb::core::Span<uint8_t> frame) override;
    void releaseDataLine() override;

    const SioLineStats& stats() const noexcept { return stats_; }

    static constexpr uint32_t byteTimeUs(std::size_t bytes) noexcept
    {
        return static_cast<uint32_t>((bytes * 10u * 1000000u + kBaud - 1u) / kBaud);
    }

private:
    void recover() noexcept;

    SioUartPort& port_;
    rb::app::Clock& clock_;
    SioLineStats stats_{};
    bool replyPending_ = false;  // the next send() is the first after a frame
    uint64_t releasedAtUs_ = 0;
};

}  // namespace rb::sio
