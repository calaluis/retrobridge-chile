#pragma once

#include <cstddef>
#include <cstdint>

#include "rb/core/result.hpp"
#include "rb/core/span.hpp"
#include "rb/sio/sio_checksum.hpp"

namespace rb::sio {

// The five bytes the computer sends while COMMAND is low: device ID, command,
// AUX1, AUX2 and checksum (Atari C016555 §9, figure 9-5).
// Invariant: the checksum matched. Whether the frame is addressed to us is
// decided by the dispatcher.
class CommandFrame {
public:
    static constexpr std::size_t kSize = 5;

    // InvalidCommandFrame if not exactly 5 bytes; ChecksumMismatch otherwise.
    static rb::core::Result<CommandFrame> parse(rb::core::Span<const uint8_t> bytes)
    {
        if (bytes.size() != kSize) {
            return rb::core::Failure{rb::core::ErrorCode::InvalidCommandFrame};
        }
        if (sio_checksum(bytes.first(4)) != bytes[4]) {
            return rb::core::Failure{rb::core::ErrorCode::ChecksumMismatch};
        }
        return CommandFrame{bytes[0], bytes[1], bytes[2], bytes[3]};
    }

    uint8_t deviceId() const noexcept { return deviceId_; }
    uint8_t command() const noexcept { return command_; }
    uint8_t aux1() const noexcept { return aux1_; }
    uint8_t aux2() const noexcept { return aux2_; }

    // AUX1 is the least significant byte (e.g. the sector number).
    uint16_t aux() const noexcept { return static_cast<uint16_t>(aux1_ | (aux2_ << 8)); }

private:
    CommandFrame(uint8_t deviceId, uint8_t command, uint8_t aux1, uint8_t aux2) noexcept
        : deviceId_(deviceId), command_(command), aux1_(aux1), aux2_(aux2)
    {
    }

    uint8_t deviceId_;
    uint8_t command_;
    uint8_t aux1_;
    uint8_t aux2_;
};

}  // namespace rb::sio
