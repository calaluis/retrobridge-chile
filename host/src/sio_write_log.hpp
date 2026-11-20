#pragma once

// The sio_write event of retrobridge_host (menu-escritura-disco, M45): one line
// per write command (W/P) that reached a drive, written to the JSONL log:
//   drive=n cmd=W|P sector=s result=complete|error|nak [crc32=xxxxxxxx] [cause=...]
//   [reply=lost]
// crc32 is the CRC-32 (IEEE) of the sector data received (absent when the
// command itself was NAKed). cause, on error: no_disk, protected or io (the
// file could not be written: disk full, file locked by another program).
// reply=lost: COMPLETE or ERROR could not be sent; the result still happened.

#include <cstddef>
#include <cstdint>
#include <cstdio>

#include "rb/domain/virtual_drive.hpp"

namespace rb::host {

struct SioWriteRecord {
    uint8_t drive = 0;    // 1..8
    uint8_t command = 0;  // 'W' or 'P'
    uint16_t sector = 0;
    char result = 0;      // 'C' complete, 'E' error, 'N' nak
    bool haveData = false;
    uint32_t crc32 = 0;
    bool replyLost = false;  // COMPLETE/ERROR not sent (the write did happen)
};

class SioWriteObserver {
public:
    SioWriteObserver(const SioWriteObserver&) = delete;
    SioWriteObserver& operator=(const SioWriteObserver&) = delete;

    virtual void onSioWrite(const SioWriteRecord& record) = 0;

protected:
    SioWriteObserver() = default;
    ~SioWriteObserver() = default;
};

// CRC-32 (IEEE 802.3, reflected, as zlib's crc32): bit by bit, no table.
inline uint32_t crc32(const uint8_t* data, std::size_t length) noexcept
{
    uint32_t crc = 0xFFFFFFFFu;
    for (std::size_t i = 0; i < length; ++i) {
        crc ^= data[i];
        for (int bit = 0; bit < 8; ++bit) {
            crc = (crc >> 1) ^ (0xEDB88320u & (0u - (crc & 1u)));
        }
    }
    return ~crc;
}

// Cause of an ERROR, from the drive as it is right after the write.
inline const char* sioWriteCause(const rb::domain::VirtualDrive* drive) noexcept
{
    if (drive == nullptr) {
        return "no_drive";
    }
    const rb::domain::DriveStatus status = drive->status();
    if (!status.diskPresent()) {
        return "no_disk";
    }
    return status.writeProtected() ? "protected" : "io";
}

// Writes the event detail into `out`; `drive` is the drive written (for cause).
inline void formatSioWrite(const SioWriteRecord& record, const rb::domain::VirtualDrive* drive, char* out,
                           std::size_t size) noexcept
{
    const char* result = record.result == 'C' ? "complete" : record.result == 'E' ? "error" : "nak";
    int used = std::snprintf(out, size, "drive=%u cmd=%c sector=%u result=%s", static_cast<unsigned>(record.drive),
                             static_cast<char>(record.command), static_cast<unsigned>(record.sector), result);
    if (used < 0 || static_cast<std::size_t>(used) >= size) {
        return;
    }
    if (record.haveData) {
        const int more = std::snprintf(out + used, size - static_cast<std::size_t>(used), " crc32=%08lx",
                                       static_cast<unsigned long>(record.crc32));
        if (more < 0 || static_cast<std::size_t>(used + more) >= size) {
            return;
        }
        used += more;
    }
    if (record.result == 'E') {
        const int more =
            std::snprintf(out + used, size - static_cast<std::size_t>(used), " cause=%s", sioWriteCause(drive));
        if (more < 0 || static_cast<std::size_t>(used + more) >= size) {
            return;
        }
        used += more;
    }
    if (record.replyLost) {
        std::snprintf(out + used, size - static_cast<std::size_t>(used), " reply=lost");
    }
}

}  // namespace rb::host
