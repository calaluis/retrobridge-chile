#pragma once

// EspDataIn (cassette-cas C107): the cassette's two outputs on the Atari's DATA IN line.
//
// - ByteOut: records as bytes on UART2 TX, 8N1, at the tape's rate (600 baud as a rule).
// - WaveOut: the FSK signal on the RMT at 1 MHz (1 tick = 1 us), fed from an FskSymbolFeed by
//   the RMT refill interrupt.
// - Both use the one DATA IN pin. Between FSK runs the pin belongs to the UART; for a run it is
//   handed to the RMT and given back when the run has played, or at once on stop(). Adapted from
//   the owner's own code (FujiNet PR #1756, cassette_fsk_take_pin / cassette_fsk_release_pin and
//   the RMT channel settings; PR #1804, stop on MOTOR drop), under ADR-012 amendment 1.
// - Bus safety (H4, risk R2): DATA IN rests at mark (high: the 74LS07 lets go and the Atari's
//   pull-up holds the line). The UART idles high, the RMT ends every transmission at high, and
//   stop() and every failure leave the pin to the idle UART.
//
// Only configure() with a CassetteOutputAssignment (H4 and M3a signed) touches the hardware;
// without it every call refuses and nothing is driven. One task calls everything except the
// refill interrupt: the cassette loop, which also calls pump() every tick.
// The refill callback is not in IRAM: during a flash write (NVS, OTA) the RMT ISR waits and a run
// may underrun. Nothing writes the flash while a tape plays today.

#include <cstddef>
#include <cstdint>

#include "rb/board/board_profile.hpp"
#include "rb/cassette/cassette_player.hpp"
#include "rb/cassette/fsk_symbol_feed.hpp"

namespace rb::esp {

class EspDataIn final : public rb::cassette::ByteOut, public rb::cassette::WaveOut {
public:
    static constexpr std::size_t kFeedPortions = 4096;  // >= 0.4 s of signal (100 us minimum), 16 KB
    static constexpr uint32_t kStartPortions = 1024;    // a run starts with this much queued, or with
                                                        // all the player had (no growth in a tick)
    static constexpr uint32_t kDefaultBaud = 600;

    EspDataIn() noexcept : feed_(rb::core::Span<uint32_t>(slots_, kFeedPortions)) {}

    // Installs UART2 TX on the granted DATA IN pin. False leaves everything as it was.
    bool configure(const rb::board::CassetteOutputAssignment& pins) noexcept;
    bool enabled() const noexcept { return pin_ >= 0; }

    // ByteOut
    bool setBaud(uint32_t baud) override;
    bool send(rb::core::Span<const uint8_t> bytes) override;

    // WaveOut
    bool push(const rb::cassette::FskPortion& portion) override;
    bool idle() const override;
    void stop() override;

    // Cassette loop, every tick: starts a queued run, restarts after an underrun, gives the pin
    // back to the UART once a run has played.
    void pump() noexcept;

    uint32_t underruns() const noexcept { return underruns_; }
    uint32_t failures() const noexcept { return failures_; }  // runs dropped: the RMT could not play
    bool fskActive() const noexcept { return channel_ != nullptr; }

private:
    bool takePin() noexcept;      // UART -> RMT; false: the UART still has the pin
    void dropRun() noexcept;      // RMT failure: drop the queued run, pin back to the UART
    void releasePin() noexcept;   // RMT -> UART, line at mark
    bool startTransmission() noexcept;
    bool transmitting() const noexcept;

    int8_t pin_ = -1;
    uint32_t baud_ = kDefaultBaud;
    void* channel_ = nullptr;   // rmt_channel_handle_t while the pin belongs to the RMT
    void* encoder_ = nullptr;   // rmt_encoder_handle_t
    bool sending_ = false;      // a transmission was queued and has not been seen done
    bool started_ = false;      // this run has started once (a restart counts as an underrun)
    uint32_t underruns_ = 0;
    uint32_t failures_ = 0;
    uint32_t lastSize_ = 0;     // feed size at the previous pump()
    uint8_t dummy_ = 0;         // rmt_transmit() wants a payload; the encoder reads the feed
    uint32_t slots_[kFeedPortions] = {};
    rb::cassette::FskSymbolFeed feed_;
};

}  // namespace rb::esp
