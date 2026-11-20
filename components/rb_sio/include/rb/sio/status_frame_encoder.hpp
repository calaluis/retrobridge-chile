#pragma once

#include <array>
#include <cstdint>

#include "rb/domain/disk_geometry.hpp"
#include "rb/domain/drive_status.hpp"

namespace rb::sio {

// Encodes a drive's state as the 4-byte STATUS ($53) data (architecture §4.2,
// H2-6/H2-7). Sources: Atari C016555 fig. 5-19; Altirra Hardware Reference
// Manual table 44 and §10.9. Tags: [DOC] documented, [DIS] design decision,
// [HIP] provisional until validated on hardware (H6/H7).
//
// Byte 0, drive status:
//   D0 last command frame unsuccessful   [HIP] set after a NAK to this drive
//   D1 last data frame unsuccessful      after a data frame with a bad checksum
//   D2 unsuccessful PUT                  after a write that failed (not protection)
//   D3 failed due to write protection    after a write refused by protection [DOC ALT]
//                                        (Atari 1982 words it as "write protected";
//                                        not used as a permanent flag)
//   D1-D3 come from `lastWriteFlags` (kFlag*), set by the dispatcher.
//   D4 motor running                     0: no motor [DIS]
//   D5 256-byte sectors                  from the geometry [DOC ALT]
//   D7 enhanced density (1040 x 128)     from the geometry [DOC ALT]
// Byte 1, inverted FDC status: $FF normal [DOC 810], $EF record not found after
//   a failed operation [DIS], $7F not ready when empty [DIS, 1050-style].
//   With `reportProtection` (writes enabled), a protected disk clears bit 6
//   (FD1771 type I status, "write protect", inverted): $BF. A write refused by
//   protection is not "record not found": it keeps $BF (menu-escritura-disco).
// Byte 2, format timeout in 64/60 s units: $E0 [DOC 810]
// Byte 3, unused: $00 [DOC]
class StatusFrameEncoder {
public:
    static constexpr uint8_t kFdcNormal = 0xFF;
    static constexpr uint8_t kFdcRecordNotFound = 0xEF;
    static constexpr uint8_t kFdcNotReady = 0x7F;
    static constexpr uint8_t kFormatTimeout = 0xE0;
    static constexpr uint8_t kFdcWriteProtect = 0x40;  // cleared when protected
    static constexpr uint8_t kFlagDataFrame = 0x02;    // D1
    static constexpr uint8_t kFlagPut = 0x04;          // D2
    static constexpr uint8_t kFlagProtected = 0x08;    // D3
    static constexpr uint8_t kWriteFlags = kFlagDataFrame | kFlagPut | kFlagProtected;

    // `geometry` is the mounted disk's geometry, or nullptr when the drive is empty.
    static std::array<uint8_t, 4> encode(const rb::domain::DriveStatus& status, const rb::domain::DiskGeometry* geometry,
                                         bool lastCommandRejected, uint8_t lastWriteFlags = 0,
                                         bool reportProtection = false) noexcept
    {
        uint8_t drive = static_cast<uint8_t>(lastWriteFlags & kWriteFlags);
        if (lastCommandRejected) {
            drive |= 0x01;  // D0
        }
        if (geometry != nullptr) {
            if (geometry->sectorSize() == rb::domain::SectorSize::Bytes256) {
                drive |= 0x20;  // D5
            }
            if (geometry->density() == rb::domain::Density::Enhanced) {
                drive |= 0x80;  // D7
            }
        }

        uint8_t fdc = kFdcNormal;
        if (!status.diskPresent()) {
            fdc = kFdcNotReady;
        } else if (status.lastOperationFailed() && (lastWriteFlags & kFlagProtected) == 0) {
            fdc = kFdcRecordNotFound;
        }
        if (reportProtection && status.diskPresent() && status.writeProtected()) {
            fdc = static_cast<uint8_t>(fdc & ~kFdcWriteProtect);
        }

        return {drive, fdc, kFormatTimeout, 0x00};
    }
};

}  // namespace rb::sio
