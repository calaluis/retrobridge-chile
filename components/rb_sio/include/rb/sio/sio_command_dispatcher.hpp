#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <optional>

#include "rb/app/drive_bay.hpp"
#include "rb/app/usecases/get_drive_status.hpp"
#include "rb/app/usecases/read_sector.hpp"
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
// execute(): performs the accepted command:
//   STATUS -> COMPLETE + 4 bytes (StatusFrameEncoder) + checksum
//   READ   -> COMPLETE + sector + checksum; on failure ERROR + a zeroed frame
//             of the expected size + checksum (read command, ADR-005)
//
// D0 of STATUS reports whether the previous command addressed to the drive was
// NAKed [HIP]; it is cleared by any accepted command once reported.
class SioCommandDispatcher {
public:
    static constexpr std::size_t kMaxFrameSize = 256 + 1;  // largest sector + checksum

    explicit SioCommandDispatcher(rb::app::DriveBay& bay) noexcept : bay_(bay), status_(bay), read_(bay) {}

    SioCommandDispatcher(const SioCommandDispatcher&) = delete;
    SioCommandDispatcher& operator=(const SioCommandDispatcher&) = delete;

    Acceptance accept(rb::core::Span<const uint8_t> commandBytes);

    // Precondition: the last accept() returned Accepted; out.size() >= kMaxFrameSize.
    Completion execute(rb::core::Span<uint8_t> out);

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

    rb::app::DriveBay& bay_;
    rb::app::GetDriveStatus status_;
    rb::app::ReadSector read_;
    std::optional<Pending> pending_;
    std::array<bool, rb::domain::DriveNumber::kMax> lastCommandRejected_{};
};

}  // namespace rb::sio
