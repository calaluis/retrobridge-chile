#pragma once

// EspFskEngine — RetroBridge adapter, written for RetroBridge: plays one FSK run with the owner's
// FujiNet code as it is (cassette_fsk_play_run and CassetteFSKLoader, beta v1.6.2-atari-fsk-beta.1,
// commit cf3382f). It only turns RetroBridge's TapeSource and MotorIn into what that code asks for.
// ESP-IDF builds (and the host tests, over fake_idf) only.

#include <cstdint>

#include "rb/cassette/tape_source.hpp"
#include "rb/fujinet/fuji_tape_loop.hpp"

namespace rb::fujinet {

// The DATA IN pin (the board's UART2 TX) and the call that waits until UART2 has sent its bytes; the
// owner's code takes the pin from the UART after that call and gives it back when the run ends.
void setDataInPin(int pin) noexcept;
void setFlushOutput(void (*flush)()) noexcept;

class EspFskEngine final : public FskRunPlayer {
public:
    using MotorDroppedFn = bool (*)(void* ctx);

    EspFskEngine() noexcept;
    ~EspFskEngine();
    EspFskEngine(const EspFskEngine&) = delete;
    EspFskEngine& operator=(const EspFskEngine&) = delete;

    bool ready() const noexcept { return impl_ != nullptr; }

    // A tape is mounted (or none, nullptr): the loader stops, then the file starts over on `source`.
    void attach(rb::cassette::TapeSource* source, MotorDroppedFn motorDropped, void* ctx) noexcept;
    // The tape leaves: the loader stops (the beta's stop_fsk_loader()).
    void stop() noexcept;

    FskRunOutcome play(uint32_t headerOffset, uint16_t chunkLength) override;

    // The status of the last play(), as the owner's CassetteFSKStatus value (0 = ok).
    uint8_t lastStatus() const noexcept { return lastStatus_; }

private:
    struct Impl;
    Impl* impl_ = nullptr;
    uint8_t lastStatus_ = 0;
};

}  // namespace rb::fujinet
