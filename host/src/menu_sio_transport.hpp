#pragma once

// SioTransport decorator of retrobridge_host with --images (Issues #38 and #40, ADR-017).
// Sits between SioSession and NetSioSioTransport and takes two decisions on each
// command frame, without changing rb_sio:
//   * a cold reset (FF) was seen while waiting: the frame is answered empty and
//     dropped, so it is never served from the image D1 had before the reset (the
//     Atari retries it once the menu is back on D1);
//   * the frame is for the menu device (kMenuDevice): served here, with the same
//     sequence SioSession uses (ACK, t5 >= 250 us, COMPLETE + data + checksum),
//     and the session sees a timeout. kMenuCommand ('K') carries a key in AUX1
//     and is answered with the screen (kMenuReplySize bytes), sent in blocks of
//     kSendBlock bytes. AUX2 numbers each key: a repeated AUX1 + AUX2 is SIOV
//     retrying, answered with the same reply and not applied again. A bad
//     checksum is ignored; any other command is NAKed.
// Every other frame goes to SioSession unchanged.

#include <array>
#include <cstddef>
#include <cstdint>

#include "menu_controller.hpp"
#include "rb/app/ports/clock.hpp"
#include "rb/app/ports/sio_transport.hpp"

namespace rb::host {

struct MenuTransportStats {
    uint32_t menuCommands = 0;
    uint32_t menuNaks = 0;
    uint32_t droppedAfterReset = 0;
    uint32_t menuRetries = 0;  // a key repeated by SIOV, answered with the same reply
};

class MenuSioTransport final : public rb::app::SioTransport {
public:
    MenuSioTransport(rb::app::SioTransport& inner, rb::app::Clock& clock, MenuController& controller) noexcept
        : inner_(inner), clock_(clock), controller_(controller)
    {
    }

    rb::core::Result<rb::app::CommandBytes> waitCommandFrame(uint32_t timeoutUs) override;
    rb::core::Result<void> send(rb::core::Span<const uint8_t> bytes) override { return inner_.send(bytes); }
    void releaseDataLine() override { inner_.releaseDataLine(); }

    const MenuTransportStats& stats() const noexcept { return stats_; }

private:
    void serveMenu(const rb::app::CommandBytes& command);

    rb::app::SioTransport& inner_;
    rb::app::Clock& clock_;
    MenuController& controller_;
    MenuTransportStats stats_{};
    // NetSIO takes at most 512 bytes per data block; 256 keeps a margin and
    // fits the test transport.
    static constexpr std::size_t kSendBlock = 256;

    std::array<uint8_t, 1 + MenuReply::kSize + 1> response_{};  // COMPLETE, data, checksum
    bool haveLast_ = false;  // response_ answers the key below
    uint8_t lastKey_ = 0;
    uint8_t lastSequence_ = 0;
};

}  // namespace rb::host
