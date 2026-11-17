#pragma once

// SioTransport decorator of retrobridge_host with --images (Issue #38, ADR-017).
// Sits between SioSession and NetSioSioTransport and takes two decisions on each
// command frame, without changing rb_sio:
//   * a cold reset (FF) was seen while waiting: the frame is answered empty and
//     dropped, so it is never served from the image D1 had before the reset (the
//     Atari retries it once the menu is back on D1);
//   * the frame is for the menu device (kMenuDevice): served here, with the same
//     sequence SioSession uses (ACK, t5 >= 250 us, COMPLETE + data + checksum),
//     and the session sees a timeout. A bad checksum is ignored; any command
//     other than kMenuCommand is NAKed.
// Every other frame goes to SioSession unchanged.

#include <array>
#include <cstdint>

#include "menu_controller.hpp"
#include "rb/app/ports/clock.hpp"
#include "rb/app/ports/sio_transport.hpp"

namespace rb::host {

struct MenuTransportStats {
    uint32_t menuCommands = 0;
    uint32_t menuNaks = 0;
    uint32_t droppedAfterReset = 0;
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
    std::array<uint8_t, 1 + MenuReply::kSize + 1> response_{};  // COMPLETE, data, checksum
};

}  // namespace rb::host
