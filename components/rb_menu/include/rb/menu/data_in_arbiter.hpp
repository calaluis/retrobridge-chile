#pragma once

// Who owns the Atari's DATA IN line (spec menu-cinta, plan §2.3 and §2.4): the disk (SIO replies) or
// the tape (cassette playback). Both drive UART2 TX on GPIO21, so never both at once: the disk parks
// before the tape is asked, and resumes only after the tape confirms it gave the line back. The tape
// confirms from its own task (T9a), so take and release are asynchronous, with deadlines. Pure logic:
// the two lines are ports, the caller feeds the inputs and the time (µs) once per SIO-loop step.

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

// What a line operation returned. Pending: asked, not yet confirmed (T9a: the cassette confirms on
// its own tick). Released: the tape let the line go without giving it back to UART2 (release only).
enum class LineResult { Done, Pending, Failed, Released };

// The SIO side of UART2. The driver is installed once at boot and never deleted (plan §2.4, decision
// 1): park/resume move the permission to transmit, not the driver.
class DiskLine {
public:
    virtual ~DiskLine() = default;
    // Stop transmitting and serving frames. False: the disk's TX is not empty yet; try next step.
    virtual bool park() noexcept = 0;
    // Back to the disk's line settings (19 200 baud, 8N1, verified) and serving frames.
    // Done or Failed; Failed leaves D1 silent (plan §2.4, «Reanudar el disco»).
    virtual LineResult resume() noexcept = 0;
};

// The cassette side of UART2. take() and release() start a request or poll the one in flight.
class TapeLine {
public:
    virtual ~TapeLine() = default;
    virtual LineResult take() noexcept = 0;
    virtual LineResult release() noexcept = 0;
    virtual void abandon() noexcept = 0;  // stop waiting for the request in flight (it expired)
};

// Who holds DATA IN (plan §2.4). Only Disk lets the disk serve frames; only Tape lets the cassette
// transmit. TakePending: disk parked, tape not yet confirmed. ReleasePending: waiting for the tape to
// give the line back. Fault: a release expired or was refused; the disk stays parked and the release
// is asked again every 100 ms. Released: the tape let the line go without UART2; nobody drives it and
// the release is asked again every 100 ms. DiskFault: the tape gave the line back but resume() failed;
// D1 stays silent and resume() is retried every 100 ms.
enum class DataInOwner { Disk, TakePending, Tape, ReleasePending, Fault, Released, DiskFault };

const char* dataInOwnerName(DataInOwner owner) noexcept;

// What changed in one step. Only a change of owner (or a retry that failed again where the console
// must see it) is reported; waiting steps return None.
enum class DataInChange {
    None,
    TakeRequested,     // Disk -> TakePending
    ToTape,            // -> Tape
    TakeFailed,        // the tape refused: back to the disk (rule 5)
    TakeTimedOut,      // no confirmation in kTakeTimeoutUs: the take becomes a release
    ReleaseRequested,  // Tape or TakePending -> ReleasePending
    ToDisk,            // -> Disk, resume() done
    ReleaseFailed,     // refused -> Fault
    ReleaseTimedOut,   // no confirmation in kReleaseTimeoutUs -> Fault
    Released,          // -> Released
    ResumeFailed,      // -> DiskFault
};

class DataInArbiter {
public:
    static constexpr uint64_t kRetryUs = 100000;         // a failed or expired request, again after 100 ms
    static constexpr uint64_t kTakeTimeoutUs = 20000;    // 10 cassette ticks (plan §2.4)
    static constexpr uint64_t kReleaseTimeoutUs = 600000;  // 32 bytes at 600 baud + RMT + margin (decision 3)

    DataInArbiter(DiskLine& disk, TapeLine& tape) noexcept : disk_(disk), tape_(tape) {}

    DataInOwner owner() const noexcept { return owner_; }

    // One step of the SIO loop. Returns what changed in this step. No allocation.
    DataInChange step(const DataInInputs& in, uint64_t nowUs) noexcept;

private:
    static bool wantsTape(const DataInInputs& in) noexcept;
    static bool givesBack(const DataInInputs& in) noexcept;
    DataInChange onTake(LineResult taken, uint64_t nowUs) noexcept;
    DataInChange startRelease(uint64_t nowUs) noexcept;
    DataInChange onRelease(LineResult result, uint64_t nowUs) noexcept;
    DataInChange retryRelease(uint64_t nowUs) noexcept;
    DataInChange resumeDisk(uint64_t nowUs, DataInChange onDone) noexcept;
    bool due(uint64_t nowUs) const noexcept { return nowUs - lastTryUs_ >= kRetryUs; }

    DiskLine& disk_;
    TapeLine& tape_;
    DataInOwner owner_ = DataInOwner::Disk;
    bool takeFailed_ = false;   // the last take failed or expired: the next one waits 100 ms
    bool retrying_ = false;     // Fault/Released: a release retry is in flight
    uint64_t sinceUs_ = 0;      // when the request in flight was published
    uint64_t lastTryUs_ = 0;    // when the last attempt failed
};

}  // namespace rb::host
