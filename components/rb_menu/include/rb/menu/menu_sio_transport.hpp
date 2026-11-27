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
// Every other frame goes to SioSession unchanged. A write (W/P) to D1..D8 is
// also followed through receiveAfterAck() and the replies the session sends,
// and reported once to the SioWriteObserver, if any (menu-escritura-disco).

#include <array>
#include <atomic>
#include <cstddef>
#include <cstdint>

#include "rb/menu/menu_controller.hpp"
#include "rb/app/ports/clock.hpp"
#include "rb/app/ports/sio_transport.hpp"
#include "rb/menu/sio_write_log.hpp"

namespace rb::host {

struct MenuTransportStats {
    uint32_t menuCommands = 0;
    uint32_t menuNaks = 0;
    uint32_t droppedAfterReset = 0;
    uint32_t menuRetries = 0;  // a key repeated by SIOV, answered with the same reply
    uint32_t textCommands = 0;  // 'T': characters of an edited server (menu-editar-servidor)
    // Issue #125 (PR #124): the ends of a menu command the Atari sees as "EL HOST NO RESPONDE". Written
    // by the SIO task only, read by the console (`sio status`): atomics, so a read never races a write
    // (review of PR #124, H1). Each field is read on its own; a reading is not one snapshot of all of them.
    std::atomic<uint32_t> menuBadChecksums{0};  // a menu frame with a bad checksum: dropped, the Atari retries
    std::atomic<uint32_t> menuAckFailures{0};   // the ACK not sent: nothing selected, the Atari retries
    std::atomic<uint32_t> menuSendFailures{0};  // a block of the reply not sent: the rest is not sent
    std::atomic<uint32_t> lastReplyUs{0};       // end of the ACK to the last byte of a complete reply
    std::atomic<uint32_t> maxReplyUs{0};        // the longest complete reply
    std::atomic<uint32_t> lastInterruptedUs{0}; // end of the ACK to the failed block (review PR #124, M2)
};
// As in data_in_handover.hpp: only aligned 32-bit loads and stores (no read-modify-write). Always
// lock-free on the host; the ESP32 GCC (Xtensa) does not report it, so the check is host only.
#ifndef ESP_PLATFORM
static_assert(std::atomic<uint32_t>::is_always_lock_free, "the console reads these without a lock");
#endif

class MenuSioTransport final : public rb::app::SioTransport {
public:
    MenuSioTransport(rb::app::SioTransport& inner, rb::app::Clock& clock, MenuController& controller,
                     SioWriteObserver* writes = nullptr) noexcept
        : inner_(inner), clock_(clock), controller_(controller), writes_(writes)
    {
    }

    rb::core::Result<rb::app::CommandBytes> waitCommandFrame(uint32_t timeoutUs) override;
    rb::core::Result<void> send(rb::core::Span<const uint8_t> bytes) override;
    rb::core::Result<void> receiveAfterAck(rb::core::Span<uint8_t> frame) override;
    void releaseDataLine() override { inner_.releaseDataLine(); }

    const MenuTransportStats& stats() const noexcept { return stats_; }

private:
    void serveMenu(const rb::app::CommandBytes& command);
    void serveText(const rb::app::CommandBytes& command);
    void trackWrite(const rb::app::CommandBytes& command) noexcept;
    void reportWrite(char result) noexcept;

    rb::app::SioTransport& inner_;
    rb::app::Clock& clock_;
    MenuController& controller_;
    SioWriteObserver* writes_;
    // The write being followed: Command (waiting for the data or a NAK), Data
    // (data received, next byte answers it), Completion (data ACKed).
    enum class WriteStage : uint8_t { None, Command, Data, Completion };
    WriteStage writeStage_ = WriteStage::None;
    SioWriteRecord write_{};
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
