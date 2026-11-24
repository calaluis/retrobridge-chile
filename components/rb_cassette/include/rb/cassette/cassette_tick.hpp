#pragma once

// CassetteTick (spec menu-cinta, plan §2.4 and requirements 1 and 2, T9a.7): one tick of the cassette
// task. CassetteService runs it every loop period; the host tests run the same code.
//
// Each tick, in this order:
//   1. the player plays into PlayerOutputs (bytes(), wave()) and sees a GatedMotor, never the MOTOR
//      itself: while the live outputs are not selected the tape does not move (requirement 1);
//   2. the live line pumps its FSK queue (EspDataIn::pump), when there is one;
//   3. "the tape played to its end" is published with the outputs this tick played into (requirement 2);
//   4. the request of whoever lends DATA IN is answered, between two ticks:
//      - Take (§2.4 step 3): once the line's hardware is free, select(true), then confirm Tape;
//      - Release (§2.4 step 6): select(false) first, then give the pin back to UART2 and confirm Disk
//        (or Released when it could not).
//
// Pure: the lender, the live line and the end sink are ports (the firmware adapts DataInHandover,
// EspDataIn and TapeProgress to them). Never allocates.

#include <cstdint>

#include "rb/cassette/cassette_player.hpp"
#include "rb/cassette/player_outputs.hpp"

namespace rb::cassette {

// Who lends DATA IN to the cassette (the SIO task, through DataInHandover).
class DataInLender {
public:
    enum class Kind : uint8_t { Take, Release };
    enum class Ack : uint8_t { Disk, Tape, Released };
    struct Ask {
        Kind kind;
        uint32_t seq;  // 0: the boot state, already answered
    };

    virtual Ask pending() const noexcept = 0;
    virtual void confirm(uint32_t seq, Ack ack) noexcept = 0;

protected:
    ~DataInLender() = default;
};

// The hardware under the live outputs (EspDataIn in shared mode).
class LiveLine {
public:
    enum class GiveBack : uint8_t { Busy, Uart, Lost };

    // §2.4 "hardware libre": nothing left to send, no FSK run active or queued.
    virtual bool hardwareFree() const noexcept = 0;
    // GPIO21 back on UART2 TX (Uart), not yet (Busy), or let go without a driver (Lost).
    virtual GiveBack giveBack() noexcept = 0;
    // Starts the next queued FSK run when the line is ready for it.
    virtual void pump() noexcept = 0;

protected:
    ~LiveLine() = default;
};

// Where "the tape played to its end" goes (TapeProgress).
class EndSink {
public:
    virtual void publish(bool ended) noexcept = 0;

protected:
    ~EndSink() = default;
};

class CassetteTick {
public:
    CassetteTick(CassettePlayer& player, PlayerOutputs& outputs, const MotorIn& motor) noexcept
        : player_(player), outputs_(outputs), motor_(motor, outputs)
    {
    }

    // The live line to pump each tick (exclusive or shared mode).
    void pump(LiveLine& line) noexcept { line_ = &line; }
    // Shared mode: DATA IN is lent and taken back through `lender`; `line` must be the live line.
    void lend(DataInLender& lender, LiveLine& line) noexcept
    {
        lender_ = &lender;
        line_ = &line;
    }
    void report(EndSink& sink) noexcept { sink_ = &sink; }

    void tick(uint64_t nowUs) noexcept;

private:
    void answer() noexcept;

    CassettePlayer& player_;
    PlayerOutputs& outputs_;
    GatedMotor motor_;
    LiveLine* line_ = nullptr;
    DataInLender* lender_ = nullptr;
    EndSink* sink_ = nullptr;
    uint32_t answered_ = 0;  // the last request confirmed
    // From select(false) of live outputs until giveBack() has the line back with TX empty (Uart), or until a
    // new take is confirmed: the tape's bytes may still be going out on the live line, so the end is not
    // published. Lost does not clear it: the line was freed for safety, with no proof the bytes left
    // (reviews of PR #106).
    bool returning_ = false;
};

}  // namespace rb::cassette
