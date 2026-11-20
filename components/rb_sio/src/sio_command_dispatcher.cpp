#include "rb/sio/sio_command_dispatcher.hpp"

#include <algorithm>

#include "rb/core/assert.hpp"
#include "rb/sio/command_frame.hpp"
#include "rb/sio/sio_checksum.hpp"
#include "rb/sio/sio_codes.hpp"
#include "rb/sio/status_frame_encoder.hpp"

namespace rb::sio {

using rb::core::ErrorCode;
using rb::core::Result;
using rb::core::Span;
using rb::domain::DriveNumber;
using rb::domain::SectorNumber;
using rb::domain::VirtualDrive;

namespace {

constexpr uint16_t kStatusLength = 4;
constexpr uint16_t kEmptyDriveReadLength = 128;  // what the computer expects without a geometry

// Appends the checksum after `length` data bytes; returns the frame length.
uint16_t seal(Span<uint8_t> out, uint16_t length)
{
    out[length] = sio_checksum(out.first(length));
    return static_cast<uint16_t>(length + 1);
}

}  // namespace

Acceptance SioCommandDispatcher::accept(Span<const uint8_t> commandBytes)
{
    RB_ASSERT(!pending_);  // every accepted command is executed before the next one

    const Result<CommandFrame> parsed = CommandFrame::parse(commandBytes);
    if (!parsed.ok()) {
        return Acceptance::Ignore;
    }
    const CommandFrame& frame = parsed.value();
    if (frame.deviceId() < device::kFirstDisk || frame.deviceId() > device::kLastDisk) {
        return Acceptance::Ignore;
    }
    const DriveNumber drive = DriveNumber::from(static_cast<uint8_t>(frame.deviceId() - 0x30)).value();
    const VirtualDrive* target = bay_.find(drive);
    if (target == nullptr) {
        return Acceptance::Ignore;  // never NAK on behalf of a drive we do not emulate
    }

    switch (frame.command()) {
    case command::kStatus:
        pending_ = Pending{drive, command::kStatus, std::nullopt, kStatusLength};
        return Acceptance::Accepted;

    case command::kReadSector: {
        const Result<SectorNumber> sector = SectorNumber::from(frame.aux());
        if (!sector.ok()) {
            return reject(drive);
        }
        const Result<uint16_t> size = target->validateRead(sector.value());
        uint16_t length = kEmptyDriveReadLength;
        if (size.ok()) {
            length = size.value();
        } else if (size.error() != ErrorCode::NoDiskMounted) {
            return reject(drive);  // SectorOutOfRange
        }
        pending_ = Pending{drive, command::kReadSector, sector.value(), length};
        lastCommandRejected_[drive.value() - 1] = false;
        writeFlags(drive) = 0;
        return Acceptance::Accepted;
    }

    case command::kWriteSector:
    case command::kPutSector: {
        if (!allowWrites_) {
            return reject(drive);
        }
        const Result<SectorNumber> sector = SectorNumber::from(frame.aux());
        if (!sector.ok()) {
            return reject(drive);
        }
        const Result<uint16_t> size = target->validateWrite(sector.value());
        uint16_t length = kEmptyDriveReadLength;  // an empty drive still takes the frame, then ERROR
        if (size.ok()) {
            length = size.value();
        } else if (size.error() != ErrorCode::NoDiskMounted) {
            return reject(drive);  // SectorOutOfRange
        }
        pending_ = Pending{drive, frame.command(), sector.value(), length};
        lastCommandRejected_[drive.value() - 1] = false;
        writeFlags(drive) = 0;
        return Acceptance::Accepted;
    }

    case command::kFormat:
    case command::kFormatMedium: {
        if (!allowWrites_) {
            return reject(drive);
        }
        // The answer is a sector-sized buffer: 128 bytes on an empty drive.
        const Result<uint16_t> size = target->validateRead(SectorNumber::from(4).value());
        const uint16_t length = size.ok() ? size.value() : kEmptyDriveReadLength;
        pending_ = Pending{drive, frame.command(), std::nullopt, length};
        lastCommandRejected_[drive.value() - 1] = false;
        writeFlags(drive) = 0;
        return Acceptance::Accepted;
    }

    default:
        return reject(drive);
    }
}

Acceptance SioCommandDispatcher::reject(DriveNumber drive) noexcept
{
    lastCommandRejected_[drive.value() - 1] = true;
    return Acceptance::Nak;
}

bool SioCommandDispatcher::isWrite(uint8_t command) noexcept
{
    return command == command::kWriteSector || command == command::kPutSector;
}

bool SioCommandDispatcher::isFormat(uint8_t command) noexcept
{
    return command == command::kFormat || command == command::kFormatMedium;
}

Span<uint8_t> SioCommandDispatcher::incomingFrame() noexcept
{
    if (!pending_.has_value() || !isWrite(pending_->command)) {
        return Span<uint8_t>{};
    }
    return Span<uint8_t>{data_.data(), static_cast<std::size_t>(pending_->dataLength + 1u)};
}

bool SioCommandDispatcher::acceptData() noexcept
{
    RB_ASSERT(pending_.has_value() && isWrite(pending_->command));
    const uint16_t length = pending_->dataLength;
    if (sio_checksum(Span<const uint8_t>{data_.data(), length}) == data_[length]) {
        return true;
    }
    writeFlags(pending_->drive) = StatusFrameEncoder::kFlagDataFrame;
    pending_.reset();
    return false;
}

Completion SioCommandDispatcher::execute(Span<uint8_t> out)
{
    RB_ASSERT(pending_.has_value());
    RB_ASSERT(out.size() >= kMaxFrameSize);
    const Pending pending = *pending_;
    pending_.reset();

    if (pending.command == command::kStatus) {
        return executeStatus(pending, out);
    }
    if (isWrite(pending.command)) {
        return executeWrite(pending);
    }
    if (isFormat(pending.command)) {
        return executeFormat(pending, out);
    }
    return executeRead(pending, out);
}

Completion SioCommandDispatcher::executeStatus(const Pending& pending, Span<uint8_t> out)
{
    bool& rejected = lastCommandRejected_[pending.drive.value() - 1];
    const uint8_t flags = writeFlags(pending.drive);  // kept: they describe the last read/write
    const auto frame = StatusFrameEncoder::encode(status_.execute(pending.drive).value(),
                                                  bay_.find(pending.drive)->geometry(), rejected, flags, allowWrites_);
    rejected = false;
    std::copy(frame.begin(), frame.end(), out.begin());
    return Completion{code::kComplete, seal(out, kStatusLength)};
}

Completion SioCommandDispatcher::executeRead(const Pending& pending, Span<uint8_t> out)
{
    Span<uint8_t> data = out.first(pending.dataLength);
    const Result<uint16_t> read = read_.execute(pending.drive, *pending.sector, data);
    if (read.ok() && read.value() == pending.dataLength) {
        return Completion{code::kComplete, seal(out, pending.dataLength)};
    }
    // Read command: ERROR is still followed by a data frame of the expected size.
    std::fill(data.begin(), data.end(), uint8_t{0});
    return Completion{code::kError, seal(out, pending.dataLength)};
}

Completion SioCommandDispatcher::executeWrite(const Pending& pending)
{
    const Result<uint16_t> written =
        write_.execute(pending.drive, *pending.sector, Span<const uint8_t>{data_.data(), pending.dataLength});
    if (written.ok()) {
        return Completion{code::kComplete, 0};
    }
    writeFlags(pending.drive) = written.error() == ErrorCode::WriteProtected ? StatusFrameEncoder::kFlagProtected
                                                                             : StatusFrameEncoder::kFlagPut;
    return Completion{code::kError, 0};
}

Completion SioCommandDispatcher::executeFormat(const Pending& pending, Span<uint8_t> out)
{
    const rb::domain::Density density =
        pending.command == command::kFormat ? rb::domain::Density::Single : rb::domain::Density::Enhanced;
    const Result<void> formatted = format_.execute(pending.drive, density);
    Span<uint8_t> list = out.first(pending.dataLength);
    std::fill(list.begin(), list.end(), uint8_t{0xFF});  // no bad sectors: $FFFF ends the list
    if (formatted.ok()) {
        return Completion{code::kComplete, seal(out, pending.dataLength)};
    }
    writeFlags(pending.drive) = formatted.error() == ErrorCode::WriteProtected ? StatusFrameEncoder::kFlagProtected
                                                                               : StatusFrameEncoder::kFlagPut;
    return Completion{code::kError, seal(out, pending.dataLength)};
}

}  // namespace rb::sio
