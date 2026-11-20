#pragma once

#include <cstdint>

namespace rb::core {

// Single error vocabulary for all pure layers (ADR-003). Values are grouped by
// origin; the SIO dispatcher (H2) is the only place that maps them to bus responses.
enum class ErrorCode : uint8_t {
    // Generic
    InvalidArgument = 1,

    // Value objects
    InvalidSectorNumber,
    InvalidDriveNumber,
    InvalidGeometry,

    // ATR format
    InvalidAtrHeader,
    UnsupportedSectorSize,
    UnsupportedAtrLayout,
    ImageTooSmall,
    ImageTooLarge,

    // Drive and disk image
    NoDiskMounted,
    SectorOutOfRange,
    BufferTooSmall,
    InvalidDiskImage,
    SourceInUse,

    // Image byte sources
    OutOfBounds,
    IoError,

    // Application: image repository, drive bay and drive mailbox (H2a)
    InvalidLocation,
    NotFound,
    NoFreeSlot,
    DriveNotConfigured,
    MailboxBusy,

    // SIO protocol (H2b)
    InvalidCommandFrame,
    ChecksumMismatch,

    // SIO transport (H2c)
    Timeout,
    TransportError,

    // Board and storage platform (H3b)
    BoardNotConfirmed,
    InvalidPinAssignment,
    StorageNotMounted,

    // Disk writes (menu-escritura-disco)
    WriteProtected,

    // Formatting (menu-formatear-disco): the requested density is not the disk's
    DensityMismatch,
};

}  // namespace rb::core
