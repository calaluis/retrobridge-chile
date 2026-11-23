#pragma once

// Who owns the Atari's DATA IN line (spec menu-cinta, plan §2.3): the disk (SIO replies) or the
// tape (cassette playback). Both drive UART2 on the same pin, so never both at once: the disk ends
// before the tape takes, the tape releases before the disk begins. Pure logic: the two lines are
// ports, the caller feeds the inputs and the time (µs) once per SIO-loop step. No allocation.

#include <cstdint>

namespace rb::host {

// What the arbiter sees in one step. All false by default.
struct DataInInputs {
    bool gates = false;        // M3a and H4 approved for this package
    bool motorOn = false;      // MOTOR CONTROL from the Atari, read as a state (not an edge)
    bool commandLow = false;   // SIO COMMAND asserted: the Atari talks to a disk
    bool tapeReady = false;    // a tape is mounted on C: and ready to play
    bool tapeEnded = false;    // the tape played to its end
    bool poweredOff = false;   // the Atari was seen powered off
    bool tapeStopped = false;  // eject or rewind from the menu
};

// What the SIO task reads each step (T7), before it becomes DataInInputs.
struct DataInReadings {
    bool gatesOpen = false;    // H4 and M3a approved (TapeDeck::gatesOpen)
    bool tapeOutput = false;   // this build can hand DATA IN to the cassette while it runs (T9)
    bool motorOn = false;      // MOTOR as read: always off without M3a (EspMotorIn not granted)
    bool commandHigh = true;   // COMMAND level on its input-only pin: high = idle
    bool tapeOnC = false;      // the deck holds a tape
    bool tapeEnded = false;    // the tape played to its end
    bool atariOff = false;     // SioPowerWatch saw the 65XE powered off
};

// The arbiter's inputs from one step's readings. Without both gates AND a cassette that can drive
// DATA IN, `gates` is false: the arbiter never moves the line (AC8, plan §2.3 rule 4), so the SIO
// UART is never ended. A deck with no tape counts as stopped (eject, plan §2.3 rule 3).
DataInInputs dataInInputs(const DataInReadings& readings) noexcept;

// The SIO side of UART2.
class DiskLine {
public:
    virtual ~DiskLine() = default;
    virtual bool begin() noexcept = 0;  // false: the line did not come up
    virtual void end() noexcept = 0;
};

// The cassette side of UART2.
class TapeLine {
public:
    virtual ~TapeLine() = default;
    virtual bool take() noexcept = 0;     // false: the tape could not install the line
    virtual bool release() noexcept = 0;  // false: the line may still be held
};

enum class DataInOwner { Disk, Tape, None };

enum class DataInChange { None, ToTape, ToDisk, TakeFailed, BeginFailed, ReleaseFailed };

class DataInArbiter {
public:
    static constexpr uint64_t kRetryUs = 100000;  // a failed take, begin or release, again after 100 ms

    DataInArbiter(DiskLine& disk, TapeLine& tape) noexcept : disk_(disk), tape_(tape) {}

    DataInOwner owner() const noexcept { return owner_; }

    // One step of the SIO loop. Returns what changed in this step.
    DataInChange step(const DataInInputs& in, uint64_t nowUs) noexcept;

private:
    DataInChange toTape(uint64_t nowUs) noexcept;
    DataInChange toDisk(uint64_t nowUs) noexcept;
    DataInChange beginDisk(uint64_t nowUs) noexcept;
    bool due(uint64_t nowUs) const noexcept { return nowUs - lastTryUs_ >= kRetryUs; }

    DiskLine& disk_;
    TapeLine& tape_;
    DataInOwner owner_ = DataInOwner::Disk;
    bool releasePending_ = false;  // the tape must give the line back; its release failed
    bool takeFailed_ = false;      // the last take failed: the disk keeps UART2 until the retry
    uint64_t lastTryUs_ = 0;
};

}  // namespace rb::host
