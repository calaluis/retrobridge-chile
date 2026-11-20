#pragma once

// Wiring shared by the SioSession tests: drive context (bay, dispatcher,
// ApplyDriveRequest, session) over a FakeClock and a ScriptedSioTransport, plus
// helpers to build command frames.

#include <array>
#include <cstdint>

#include "fakes/fake_clock.hpp"
#include "fakes/fake_drive_mailbox.hpp"
#include "fakes/scripted_sio_transport.hpp"
#include "rb/app/drive_bay.hpp"
#include "rb/app/usecases/apply_drive_request.hpp"
#include "rb/sio/sio_checksum.hpp"
#include "rb/sio/sio_command_dispatcher.hpp"
#include "rb/sio/sio_session.hpp"

namespace rb::test {

constexpr uint8_t kD1 = 0x31;
constexpr uint8_t kReadCommand = 0x52;
constexpr uint8_t kStatusCommand = 0x53;

inline std::array<uint8_t, 5> sio_frame(uint8_t device, uint8_t command, uint8_t aux1 = 0, uint8_t aux2 = 0)
{
    std::array<uint8_t, 5> bytes{device, command, aux1, aux2, 0};
    bytes[4] = rb::sio::sio_checksum(rb::core::Span<const uint8_t>{bytes.data(), 4});
    return bytes;
}

inline std::array<uint8_t, 5> read_frame(uint16_t sector)
{
    return sio_frame(kD1, kReadCommand, static_cast<uint8_t>(sector & 0xFF), static_cast<uint8_t>(sector >> 8));
}

inline std::array<uint8_t, 5> status_frame()
{
    return sio_frame(kD1, kStatusCommand);
}

// A data frame of `length` bytes (value seed + i) plus its checksum.
inline std::array<uint8_t, 257> data_frame(std::size_t length, uint8_t seed, bool good = true)
{
    std::array<uint8_t, 257> bytes{};
    for (std::size_t i = 0; i < length; ++i) {
        bytes[i] = static_cast<uint8_t>(seed + i);
    }
    bytes[length] = static_cast<uint8_t>(rb::sio::sio_checksum(rb::core::Span<const uint8_t>{bytes.data(), length}) ^
                                         (good ? 0 : 1));
    return bytes;
}

struct SessionHarness {
    FakeClock clock;
    ScriptedSioTransport transport{clock};
    rb::app::DriveBay bay{1};
    FakeDriveMailbox mailbox;
    rb::sio::SioCommandDispatcher dispatcher{bay};
    rb::app::ApplyDriveRequest apply{bay, mailbox};
    rb::sio::SioSession session{transport, clock, dispatcher, apply};
};

}  // namespace rb::test
