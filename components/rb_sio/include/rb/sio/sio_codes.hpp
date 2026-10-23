#pragma once

#include <cstdint>

namespace rb::sio {

// Response bytes sent by a peripheral (Atari C016555 §9).
namespace code {
constexpr uint8_t kAck = 0x41;       // 'A'
constexpr uint8_t kNak = 0x4E;       // 'N'
constexpr uint8_t kComplete = 0x43;  // 'C'
constexpr uint8_t kError = 0x45;     // 'E'
}  // namespace code

// Command bytes handled by the v1 disk drive (Atari C016555 appendix K).
namespace command {
constexpr uint8_t kReadSector = 0x52;  // 'R'
constexpr uint8_t kStatus = 0x53;      // 'S'
}  // namespace command

// Disk drive device IDs: D1 = $31 [DOC for $31-$34]; D5-D8 follow the same
// $30 + n pattern, only reachable if configured in the DriveBay.
namespace device {
constexpr uint8_t kFirstDisk = 0x31;
constexpr uint8_t kLastDisk = 0x38;
}  // namespace device

}  // namespace rb::sio
