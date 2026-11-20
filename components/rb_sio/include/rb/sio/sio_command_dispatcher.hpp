#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <optional>

#include "rb/app/drive_bay.hpp"
#include "rb/app/usecases/get_drive_status.hpp"
#include "rb/app/usecases/read_sector.hpp"
#include "rb/app/usecases/write_sector.hpp"
#include "rb/core/span.hpp"
#include "rb/domain/drive_number.hpp"
#include "rb/domain/sector_number.hpp"

namespace rb::sio {

enum class Acceptance : uint8_t {
    Ignore,    // not for us or unreadable: stay silent, keep the data line released
    Nak,       // for us, but unsupported command or invalid parameters
    Accepted,  // send ACK, then execute()
};

// Result of execute(): the completion byte (COMPLETE or ERROR) and the length
// of the data frame written at the start of the output buffer, including its
// trailing checksum (0 if no data frame follows).
struct Completion {
    uint8_t code;
    uint16_t frameLength;
};

// Anticorruption layer between the SIO bus and the drive context (ADR-005).
// Runs in the drive context (ADR-014), like the use cases it calls.
//
// accept(): parses the command frame and decides, from in-memory state only:
//   bad length/checksum or another device ID -> Ignore
//   unsupported command                       -> Nak
//   READ with sector 0 or beyond the geometry -> Nak   (Altirra §10.9, H2-4)
//   READ on an empty drive, other READ, STATUS -> Accepted
//   WRITE/PUT ('W'/'P') without allowWrites   -> Nak    (as the read-only v1)
//   WRITE/PUT with sector 0 or beyond          -> Nak
//   WRITE/PUT, also on an empty or protected drive -> Accepted, with a data frame
//             of the sector size + checksum to receive (incomingFrame())
// acceptData(): after the data frame arrived; bad checksum -> false (send NAK,
//   nothing written, D1 in STATUS); otherwise true (send ACK, execute).
// D1-D3 of STATUS describe the last read or write, like byte 1: STATUS does not
// clear them; the next accepted READ/WRITE/PUT does.
// execute(): performs the accepted command:
//   STATUS -> COMPLETE + 4 bytes (StatusFrameEncoder) + checksum
//   READ   -> COMPLETE + sector + checksum; on failure ERROR + a zeroed frame
//             of the expected size + checksum (read command, ADR-005)
//   WRITE/PUT -> COMPLETE when written (fflush'ed by the source); ERROR when
//             protected (D3) or on any other failure (D2); no data frame follows
//
// D0 of STATUS reports whether the previous command addressed to the drive was
// NAKed [HIP]; it is cleared by any accepted command once reported.
class SioCommandDispatcher {
public:
    static constexpr std::size_t kMaxFrameSize = 256 + 1;  // largest sector + checksum

    explicit SioCommandDispatcher(rb::app::DriveBay& bay, bool allowWrites = false) noexcept
        : bay_(bay), status_(bay), read_(bay), write_(bay), allowWrites_(allowWrites)
    {
    }

    SioCommandDispatcher(const SioCommandDispatcher&) = delete;
    SioCommandDispatcher& operator=(const SioCommandDispatcher&) = delete;

    Acceptance accept(rb::core::Span<const uint8_t> commandBytes);

    // Buffer for the data frame (data + checksum) of the accepted command; empty
    // when the command carries no data from the computer.
    rb::core::Span<uint8_t> incomingFrame() noexcept;

    // Precondition: incomingFrame() was not empty and has been filled.
    // False on a bad checksum: the command is dropped (send NAK, no execute()).
    bool acceptData() noexcept;

    // Precondition: the last accept() returned Accepted; out.size() >= kMaxFrameSize.
    Completion execute(rb::core::Span<uint8_t> out);

    // Discards an accepted command without executing it, e.g. when the ACK could
    // not be transmitted (H2c). Recoverable transport errors must not reach the
    // accept() precondition, which is reserved for programming errors. No effect
    // if nothing is pending.
    void abandon() noexcept { pending_.reset(); }

    bool hasPendingCommand() const noexcept { return pending_.has_value(); }

private:
    struct Pending {
        rb::domain::DriveNumber drive;
        uint8_t command;
        std::optional<rb::domain::SectorNumber> sector;
        uint16_t dataLength;
    };

    Acceptance reject(rb::domain::DriveNumber drive) noexcept;
    Completion executeStatus(const Pending& pending, rb::core::Span<uint8_t> out);
    Completion executeRead(const Pending& pending, rb::core::Span<uint8_t> out);
    Completion executeWrite(const Pending& pending);
    static bool isWrite(uint8_t command) noexcept;
    uint8_t& writeFlags(rb::domain::DriveNumber drive) noexcept { return lastWriteFlags_[drive.value() - 1]; }

    rb::app::DriveBay& bay_;
    rb::app::GetDriveStatus status_;
    rb::app::ReadSector read_;
    rb::app::WriteSector write_;
    bool allowWrites_;
    std::optional<Pending> pending_;
    std::array<bool, rb::domain::DriveNumber::kMax> lastCommandRejected_{};
    std::array<uint8_t, rb::domain::DriveNumber::kMax> lastWriteFlags_{};  // D1-D3 for STATUS
    std::array<uint8_t, kMaxFrameSize> data_{};                             // incoming data frame
};

}  // namespace rb::sio
