#pragma once

// CassettePlayer of retrobridge_host (spec cassette-cas, C002): plays an indexed
// .cas tape like a real cassette. The tape moves only while MOTOR is on: it waits
// each block's gap, plays silence blocks as time, sends the rate before a record
// when it changes, and sends the record's bytes paced at 10 bits per byte (start
// bit, 8 data bits, stop bit) by the host clock. MOTOR off freezes the tape where
// it is (AC6, AC7). The host sets the pace, not the emulator (plan §2).
//
// tick() runs on the SIO loop: it never allocates (ADR-011).

#include <cstddef>
#include <cstdint>

#include "cas_image.hpp"
#include "rb/core/result.hpp"
#include "rb/core/span.hpp"

namespace rb::host {

// What the cassette needs from the bus (NetSIO in the host; a fake in tests).
class TapeLink {
public:
    virtual bool motorOn() const noexcept = 0;
    virtual rb::core::Result<void> setBaud(uint32_t baud) = 0;
    virtual rb::core::Result<void> sendTapeBytes(rb::core::Span<const uint8_t> bytes) = 0;

protected:
    ~TapeLink() = default;
};

class CassettePlayer {
public:
    static constexpr std::size_t kMaxSend = 512;          // one NetSIO data block
    static constexpr uint64_t kBitsPerByte = 10;
    static constexpr uint64_t kUsPerSecond = 1000000;

    // `tape` and `bytes` stay owned by the caller and must outlive the player's use of them.
    void load(const CasTape* tape, rb::core::Span<const uint8_t> bytes) noexcept;
    void unload() noexcept;
    void rewind() noexcept;
    void tick(uint64_t nowUs, TapeLink& link);

    bool loaded() const noexcept { return tape_ != nullptr; }
    bool atEnd() const noexcept;
    std::size_t block() const noexcept { return block_; }

private:
    enum class Phase : uint8_t { Gap, Silence, Bytes };

    void nextBlock() noexcept;

    const CasTape* tape_ = nullptr;
    rb::core::Span<const uint8_t> bytes_{};
    std::size_t block_ = 0;
    Phase phase_ = Phase::Gap;
    uint64_t phaseUs_ = 0;   // time already spent in a gap or a silence
    uint64_t budgetUs_ = 0;  // tape time not yet used
    uint64_t scaled_ = 0;    // in Bytes: unused time times the rate (us * baud)
    uint32_t sentInBlock_ = 0;
    uint32_t sentBaud_ = 0;  // 0: no rate sent since load or rewind
    uint64_t lastNowUs_ = 0;
    bool haveNow_ = false;
    bool motorWasOn_ = false;
};

}  // namespace rb::host
