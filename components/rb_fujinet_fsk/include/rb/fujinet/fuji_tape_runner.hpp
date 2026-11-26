#pragma once

// FujiTapeRunner — RetroBridge adapter, written for RetroBridge (cassette-motor-fujinet). It runs
// FujiTapeLoop from the cassette loop the way the beta's SIO bus runs sioCassette::sio_handle_cassette():
// one record per turn while the cassette is enabled. When the cassette is enabled is decided by FujiNet's
// SIO bus code, which was not read (ADR-012 amendment 2 allows cassette.cpp only), so these rules are
// RetroBridge's and are recorded as assumptions in docs/diseno/cassette-motor-fujinet.md §3:
//   - MOTOR on enables the cassette (600 baud, FujiTapeLoop::enable()); MOTOR off between records disables
//     it, and the tape stays where it is.
//   - A step that leaves the tape at offset 0 (its end, or a replay of the first record) disables it, as
//     the beta does; it is enabled again only after MOTOR goes off and on.
//   - A new mount, an eject or a rewind (the commands' generation) starts the tape over.

#include <cstdint>

#include "rb/cassette/cassette_player.hpp"
#include "rb/cassette/tape_source.hpp"
#include "rb/fujinet/fuji_tape_loop.hpp"

namespace rb::fujinet {

class Pause {
public:
    virtual void waitOneMs() = 0;  // a gap millisecond
    virtual void yield() = 0;      // while the UART is busy or sending

protected:
    ~Pause() = default;
};

class FujiTapeRunner final : private FujiTapeBus {
public:
    FujiTapeRunner(rb::cassette::ByteOut& out, const rb::cassette::MotorIn& motor, FskRunPlayer& player,
                   Pause& pause) noexcept;

    // The tape the commands have mounted (nullptr: none) and their generation. True when either changed:
    // the runner is back at the start of the tape, disabled; the caller then points the FSK engine at it.
    bool follow(rb::cassette::TapeSource* tape, uint32_t generation) noexcept;

    // One turn of the cassette loop. False: nothing played (no tape, MOTOR off, or waiting for MOTOR to go
    // off after the tape was disabled). True: one step ran; `last()` says which.
    bool tick() noexcept;

    const FujiStep& last() const noexcept { return last_; }
    bool enabled() const noexcept { return enabled_; }
    uint32_t offset() const noexcept { return loop_.offset(); }

private:
    // The tape the loop reads: whatever follow() was given last.
    class Followed final : public rb::cassette::TapeSource {
    public:
        rb::cassette::TapeSource* tape = nullptr;
        uint32_t size() const override { return tape == nullptr ? 0 : tape->size(); }
        bool read(uint32_t offset, rb::core::Span<uint8_t> dst) override
        {
            return tape != nullptr && tape->read(offset, dst);
        }
    };

    // FujiTapeBus
    void setBaud(uint32_t baud) override;
    void write(const uint8_t* bytes, uint32_t count) override;
    void flushOutput() override;
    void waitOneMs() override { pause_.waitOneMs(); }
    bool motorDropped() const override { return !motor_.on(); }

    rb::cassette::ByteOut& out_;
    const rb::cassette::MotorIn& motor_;
    Pause& pause_;
    Followed followed_;
    FujiTapeLoop loop_;
    uint32_t generation_ = 0;
    bool enabled_ = false;
    bool waitMotorOff_ = false;
    FujiStep last_;
};

}  // namespace rb::fujinet
