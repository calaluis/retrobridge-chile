#pragma once

#include <array>
#include <cstdint>
#include <optional>

#include "rb/app/ports/clock.hpp"
#include "rb/app/ports/sio_transport.hpp"
#include "rb/netsio/datagram_port.hpp"
#include "rb/netsio/netsio_codec.hpp"

namespace rb::netsio {

struct NetSioConfig {
    PeerProfile profile = PeerProfile::Fep003Strict;
    uint32_t aliveIntervalUs = 5000000;  // C4 period; 5 s kept the probe registered in E0
    uint32_t pingTimeoutUs = 500000;     // wait for C3 after each C2
    uint8_t pingAttempts = 10;
    uint32_t creditTimeoutUs = 1000000;  // wait for C7 after C6
    uint32_t dataTimeoutUs = 1000000;    // wait for a write data frame after its ACK
};

// Counters for logs and acceptance evidence. Saturating is not needed: a run
// that overflows 32 bits is far beyond any test session.
struct NetSioStats {
    uint32_t datagramsIn = 0;
    uint32_t datagramsOut = 0;
    uint32_t malformed = 0;          // rejected by decode() under the configured profile
    uint32_t ignoredMessages = 0;    // valid but irrelevant to a disk drive (proceed, interrupt, ...)
    uint32_t motorChanges = 0;       // MOTOR ON/OFF from the Atari (cassette-cas C002)
    uint32_t framesDelivered = 0;
    uint32_t framesDiscarded = 0;    // cut by a reset or by a new COMMAND ON
    uint32_t validSyncResponses = 0; // 81 with ack_type 1 (ACK or NAK)
    uint32_t emptySyncResponses = 0; // 81 with ack_type 0
    uint32_t creditRequests = 0;     // C6 sent
    uint32_t aliveRequests = 0;      // C4 sent
    uint32_t aliveResponses = 0;     // C5 received
    uint32_t resets = 0;             // FE or FF received
    uint32_t trailerGaps = 0;        // NetsiohubTrailingByte: trailing byte not previous + 1
    uint32_t lastBaud = 0;           // last SPEED CHANGE from the Atari, 0 if none
    uint32_t dataFramesReceived = 0; // write data frames collected whole (09 at the right count)
    uint32_t dataFramesDiscarded = 0;// after the ACK: wrong length, cut (11, reset), error or timeout
};

// Optional observer of every datagram, for the host executable's log. A port
// in the ADR-006 sense: implemented outside, never owns data.
class NetSioObserver {
public:
    NetSioObserver(const NetSioObserver&) = delete;
    NetSioObserver& operator=(const NetSioObserver&) = delete;

    virtual void onDatagram(bool outbound, rb::core::Span<const uint8_t> datagram) = 0;

protected:
    NetSioObserver() = default;
    ~NetSioObserver() = default;
};

// SioTransport over NetSIO (FEP-003, ADR-016). Lets SioSession serve an emulated
// Atari through a NetSIO hub or emulator without any change to rb_sio.
//
// Mapping of the port contract:
//   waitCommandFrame(): COMMAND ON (11) starts a frame; DATA BYTE (01) / DATA
//     BLOCK (02) fill it (first 5 bytes kept, all counted); COMMAND OFF + SYNC
//     (18) ends it and leaves the sync number pending; the emulation stays
//     paused until it is answered. Timestamps are the PC clock, not SIO timing.
//   send(), sync pending: must be exactly one byte (ACK or NAK); it goes out as
//     SYNC RESPONSE 81 <sync> 01 <byte> 00 00 (write_size 0: no data follows).
//   receiveAfterAck(frame) (menu-escritura-disco, ADR-019): needs the command
//     sync pending; sends 81 <sync> 01 'A' <frame.size() LE16> (data + checksum,
//     measured in W001), then collects DATA BYTE (01) / DATA BLOCK (02) bytes
//     into `frame` until DATA BYTE + SYNC (09), whose byte is the last one and
//     whose sync stays pending for the ACK/NAK of the data (next send()).
//     Wrong length, a new COMMAND ON, a reset or dataTimeoutUs without 09:
//     TransportError; a 09 sync left pending is released empty.
//   send(), no sync pending: one DATA BLOCK (02 <bytes>), 1..512 bytes, which
//     consumes one credit; without credit, C6 is sent and C7 awaited.
//   releaseDataLine(): a sync still pending (frame ignored, or ACK not sent) is
//     answered with 81 <sync> 00 00 00 00, so the emulator never waits for a
//     timeout. Nothing else to release on a network transport.
// Link upkeep: connect() pings (C2) until C3 and then registers (C1); while
// connected, C4 is sent every aliveIntervalUs from inside the waits.
// A DATA BYTE + SYNC (09) outside receiveAfterAck() is answered empty at once.
// Fixed buffers only; nothing is allocated after construction (ADR-011).
class NetSioSioTransport final : public rb::app::SioTransport {
public:
    NetSioSioTransport(DatagramPort& port, rb::app::Clock& clock, const NetSioConfig& config,
                       NetSioObserver* observer = nullptr) noexcept
        : port_(port), clock_(clock), config_(config), observer_(observer)
    {
    }

    // Timeout if no C3 arrives after config.pingAttempts pings; TransportError
    // if a datagram cannot be sent.
    rb::core::Result<void> connect();
    // Answers any pending sync, then sends C0. Idempotent.
    void disconnect();

    rb::core::Result<rb::app::CommandBytes> waitCommandFrame(uint32_t timeoutUs) override;
    rb::core::Result<void> send(rb::core::Span<const uint8_t> bytes) override;
    void releaseDataLine() override;
    rb::core::Result<void> receiveAfterAck(rb::core::Span<uint8_t> frame) override;

    bool connected() const noexcept { return connected_; }
    bool syncPending() const noexcept { return pendingSync_.has_value(); }
    uint8_t credit() const noexcept { return credit_; }
    // MOTOR line as the last 20/21 from the Atari left it (cassette-cas C002); off after connect.
    bool motorOn() const noexcept { return motor_; }
    const NetSioStats& stats() const noexcept { return stats_; }

private:
    rb::core::Result<void> transmit(std::size_t length);
    rb::core::Result<void> transmitSignal(uint8_t messageId);
    void answerEmpty();
    void serviceAlive();
    rb::core::Result<void> acquireCredit();
    // Receives at most one datagram before `deadlineUs` (or the next C4) and
    // processes it. Returns true when it completed a command frame.
    rb::core::Result<bool> receiveOne(uint64_t deadlineUs);
    bool process(std::size_t length);
    void append(rb::core::Span<const uint8_t> bytes) noexcept;
    void appendData(rb::core::Span<const uint8_t> bytes) noexcept;
    void abandonData() noexcept;

    DatagramPort& port_;
    rb::app::Clock& clock_;
    NetSioConfig config_;
    NetSioObserver* observer_;

    std::array<uint8_t, kMaxDatagram> rx_{};
    std::array<uint8_t, kMaxDatagram> tx_{};
    rb::app::CommandBytes frame_{};
    bool inFrame_ = false;
    // Write data frame being collected by receiveAfterAck() (never owned).
    uint8_t* data_ = nullptr;
    std::size_t dataSize_ = 0;
    std::size_t dataCount_ = 0;  // bytes seen; stops at dataSize_ + 1 (too long)
    bool receiving_ = false;
    bool dataComplete_ = false;
    std::optional<uint8_t> pendingSync_;
    std::optional<uint8_t> lastTrailer_;
    uint8_t credit_ = 0;
    bool motor_ = false;
    bool connected_ = false;
    bool pingAnswered_ = false;
    uint64_t nextAliveUs_ = 0;
    NetSioStats stats_{};
};

}  // namespace rb::netsio
