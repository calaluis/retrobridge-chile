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
//   D1 last data frame unsuccessful      0: v1 receives no data frames
//   D2 unsuccessful PUT                  0: v1 never writes
//   D3 failed due to write protection    0: v1 never attempts a write [DOC ALT]
//                                        (Atari 1982 words it as "write protected";
//                                        not used as a permanent flag)
//   D4 motor running                     0: no motor [DIS]
//   D5 256-byte sectors                  from the geometry [DOC ALT]
//   D7 enhanced density (1040 x 128)     from the geometry [DOC ALT]
// Byte 1, inverted FDC status: $FF normal [DOC 810], $EF record not found after
//   a failed operation [DIS], $7F not ready when empty [DIS, 1050-style]
// Byte 2, format timeout in 64/60 s units: $E0 [DOC 810]
// Byte 3, unused: $00 [DOC]
class StatusFrameEncoder {
public:
    static constexpr uint8_t kFdcNormal = 0xFF;
    static constexpr uint8_t kFdcRecordNotFound = 0xEF;
    static constexpr uint8_t kFdcNotReady = 0x7F;
    static constexpr uint8_t kFormatTimeout = 0xE0;

    // `geometry` is the mounted disk's geometry, or nullptr when the drive is empty.
    static std::array<uint8_t, 4> encode(const rb::domain::DriveStatus& status, const rb::domain::DiskGeometry* geometry,
                                         bool lastCommandRejected) noexcept
    {
        uint8_t drive = 0;
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
        } else if (status.lastOperationFailed()) {
            fdc = kFdcRecordNotFound;
        }

        return {drive, fdc, kFormatTimeout, 0x00};
    }
};

}  // namespace rb::sio
