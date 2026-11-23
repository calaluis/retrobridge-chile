#pragma once

// NetRestoreQueue (menu-tnfs-persistente T2): at power-up, the drives remembered from a TNFS
// server come back one at a time, after the local ones and without holding up D1. Per drive:
// Pending -> Loading -> Mounted | Failed(reason). Pure: no I/O, no allocation, no thread; the
// caller (MenuController) starts and cancels the loader jobs and injects a monotonic clock in
// milliseconds (esp_timer on the FujiNet, a fake one in the host tests).
//
// Deadlines (plan §0), all measured with that clock and never with the loader's integer percent:
//   - Wi-Fi: 30 s from begin() for the network to be Ready at least once;
//   - first response: 30 s from started() to the first byte;
//   - data: 30 s since the last time the bytes received grew.
// There is no deadline on a whole download: a slow transfer that keeps sending lives on.

#include <array>
#include <cstdint>

#include "rb/menu/drive_memory.hpp"

namespace rb::host {

class NetRestoreQueue {
public:
    static constexpr unsigned kDrives = DriveMemory::kDrives;
    static constexpr uint64_t kWifiMs = 30000;
    static constexpr uint64_t kSilenceMs = 30000;  // first response, and since the last data

    enum class State : uint8_t { None, Pending, Loading, Mounted, Failed };
    // Why a drive failed. Only SinRed comes from the queue itself (a deadline); the controller
    // reports the others from the loader's result. Which ones keep the drive remembered is the
    // controller's rule (plan §3), not the queue's.
    enum class Reason : uint8_t { None, SinRed, ServidorNoEncontrado, Falta, NoSePudoAbrir, Invalida, NoCabe };

    // Starts over: every Net* drive of `memory` is Pending, every other one None. `nowMs` starts
    // the Wi-Fi deadline.
    void begin(const DriveMemory& memory, uint64_t nowMs) noexcept;

    // The Pending drive to ask the loader for now (the lowest one), or 0: none Pending, one
    // already Loading, the network is not Ready, or it was never Ready in the 30 s after begin()
    // (then expired() fails the waiting drives, whichever of the two is called first).
    unsigned next(bool networkReady, uint64_t nowMs) noexcept;

    // Pending -> Loading with loader job `job` (never 0). False, and nothing changes, otherwise.
    bool started(unsigned drive, uint32_t job, uint64_t nowMs) noexcept;
    // The job of a Loading drive now has `bytes` received. If they grew, `nowMs` is its last data.
    void progressed(unsigned drive, uint32_t bytes, uint64_t nowMs) noexcept;
    // Loading -> Mounted / Failed(reason). False, and nothing changes, if it was not Loading.
    bool done(unsigned drive) noexcept;
    bool failed(unsigned drive, Reason reason) noexcept;

    // The user put something else on the drive (or emptied it): it leaves the queue, and a late
    // result of its job is no longer anyone's (driveOf() answers 0).
    void cancel(unsigned drive) noexcept;
    // A user's job took the loader: Loading -> Pending, to be asked for again later.
    bool requeue(unsigned drive) noexcept;

    // Every drive whose deadline passed at `nowMs`, as a mask (bit drive-1); each one is now
    // Failed(SinRed). A Loading one keeps its job until the caller cancels it: jobOf() tells it.
    uint8_t expired(uint64_t nowMs) noexcept;

    State state(unsigned drive) const noexcept;
    Reason reason(unsigned drive) const noexcept;
    // The loader job of drive 1..8 (0 if it has none), and the drive a job belongs to (0 if none).
    uint32_t jobOf(unsigned drive) const noexcept;
    unsigned driveOf(uint32_t job) const noexcept;
    // True while some drive is Pending or Loading.
    bool busy() const noexcept;

private:
    struct Entry {
        State state = State::None;
        Reason reason = Reason::None;
        uint32_t job = 0;
        uint32_t bytes = 0;
        uint64_t lastMs = 0;  // started(), or the last time the bytes grew
    };
    static bool valid(unsigned drive) noexcept { return drive >= 1 && drive <= kDrives; }
    bool wifiLate(uint64_t nowMs) const noexcept;
    std::array<Entry, kDrives> drives_{};
    uint64_t beginMs_ = 0;
    bool networkSeen_ = false;
};

}  // namespace rb::host
