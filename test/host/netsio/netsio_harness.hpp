#pragma once

// Wiring shared by the NetSIO transport tests: the real drive context (bay,
// dispatcher, ApplyDriveRequest, SioSession) over NetSioSioTransport, a
// ScriptedDatagramPort playing the hub and a FakeClock. Helpers queue what the
// hub forwards from the emulated Atari, with or without the trailing byte.

#include <array>
#include <cstdint>
#include <initializer_list>

#include "fakes/fake_clock.hpp"
#include "fakes/fake_drive_mailbox.hpp"
#include "fakes/scripted_datagram_port.hpp"
#include "rb/app/drive_bay.hpp"
#include "rb/app/image_location.hpp"
#include "rb/app/usecases/apply_drive_request.hpp"
#include "rb/domain/disk_image.hpp"
#include "rb/netsio/netsio_sio_transport.hpp"
#include "rb/sio/sio_checksum.hpp"
#include "rb/sio/sio_command_dispatcher.hpp"
#include "rb/sio/sio_session.hpp"
#include "unity.h"

namespace rb::test {

constexpr uint8_t kD1 = 0x31;
constexpr uint8_t kD2 = 0x32;

inline std::array<uint8_t, 5> command_frame(uint8_t device, uint8_t command, uint8_t aux1 = 0, uint8_t aux2 = 0)
{
    std::array<uint8_t, 5> bytes{device, command, aux1, aux2, 0};
    bytes[4] = rb::sio::sio_checksum(rb::core::Span<const uint8_t>{bytes.data(), 4});
    return bytes;
}

inline rb::netsio::NetSioConfig harness_config(rb::netsio::PeerProfile profile)
{
    rb::netsio::NetSioConfig config;
    config.profile = profile;
    config.pingAttempts = 3;
    return config;
}

struct NetSioHarness {
    explicit NetSioHarness(rb::netsio::PeerProfile profile = rb::netsio::PeerProfile::Fep003Strict)
        : NetSioHarness(harness_config(profile))
    {
    }
    explicit NetSioHarness(const rb::netsio::NetSioConfig& config) : profile(config.profile), transport{port, clock, config}
    {
    }

    rb::netsio::PeerProfile profile;
    FakeClock clock;
    ScriptedDatagramPort port{clock};
    rb::netsio::NetSioSioTransport transport;
    rb::app::DriveBay bay{1};
    FakeDriveMailbox mailbox;
    rb::sio::SioCommandDispatcher dispatcher{bay};
    rb::app::ApplyDriveRequest apply{bay, mailbox};
    rb::sio::SioSession session{transport, clock, dispatcher, apply};
    uint8_t trailer = 0x20;  // next trailing byte under NetsiohubTrailingByte (as in E0)

    // Hub answers the ping and grants `credit` (C7), as observed in E0; then the
    // transport registers (C1). The record of sent datagrams is cleared.
    void connect(uint8_t credit = 3)
    {
        port.push({0xC3});
        if (credit > 0) {
            port.push({0xC7, credit});
        }
        TEST_ASSERT_TRUE(transport.connect().ok());
        TEST_ASSERT_TRUE(transport.connected());
        port.clear_sent();
    }

    void mount(rb::domain::ImageByteSource& source)
    {
        auto result = bay.mount(rb::domain::DriveNumber::from(1).value(),
                                rb::domain::DiskImage::open(source).value(),
                                rb::app::ImageLocation::from("test.atr").value());
        TEST_ASSERT_TRUE(result.ok());
    }

    // One message from the Atari, with the trailing byte when the profile expects it.
    void from_atari(std::initializer_list<uint8_t> bytes)
    {
        std::array<uint8_t, ScriptedDatagramPort::kMaxBytes> datagram{};
        std::size_t length = 0;
        for (uint8_t byte : bytes) {
            datagram[length++] = byte;
        }
        if (profile == rb::netsio::PeerProfile::NetsiohubTrailingByte) {
            datagram[length++] = trailer++;
        }
        port.push_bytes(datagram.data(), length);
    }

    // COMMAND ON, DATA BLOCK with the 5 frame bytes, COMMAND OFF + SYNC.
    void push_frame(const std::array<uint8_t, 5>& frame, uint8_t sync)
    {
        from_atari({0x11});
        from_atari({0x02, frame[0], frame[1], frame[2], frame[3], frame[4]});
        from_atari({0x18, sync});
    }

    void assert_sent(std::size_t index, std::initializer_list<uint8_t> expected) const
    {
        TEST_ASSERT_TRUE_MESSAGE(index < port.sent_count(), "datagram not sent");
        const ScriptedDatagramPort::Datagram& sent = port.sent(index);
        TEST_ASSERT_EQUAL_size_t(expected.size(), sent.length);
        std::size_t i = 0;
        for (uint8_t byte : expected) {
            TEST_ASSERT_EQUAL_HEX8(byte, sent.bytes[i++]);
        }
    }
};

}  // namespace rb::test
