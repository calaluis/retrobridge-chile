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
// - Bus safety (H4, risk R2; H4-C C8): DATA IN rests at mark (high: the 74LS07 lets go and the
//   Atari's pull-up holds the line). The UART idles high, the RMT ends every transmission at high,
//   and stop() and every failure leave the pin to the idle UART.
// - If the UART cannot take the pin back (uart_set_pin fails), the driver faults: it logs the
//   error, resets GPIO21 to its default state (GPIO function, output off, pull-up: the 74LS07 lets the line
//   go), and refuses send(), setBaud(), push() and pump() until recover() gives the pin back to
//   the UART. Nothing retries on its own: recovery is an explicit call.
// - Bytes already handed to the UART are not flushed by stop() or by a MOTOR drop: they go out,
//   then the line idles at mark. The player hands them over paced by the tape clock (at 600 baud
//   and a 2 ms loop, one byte at a time as a rule; at most CassettePlayer::kMaxSend after a long
//   stall of the loop).
//
// Shared mode (menu-cinta T9a.2, plan §2.4): attachShared() uses UART2 as the disk installed it, once
// at boot. It never installs nor deletes the driver; what passes between the SIO task and this one is
// the permission to transmit on DATA IN. send() never blocks: it writes only while fewer than
// kSharedInFlight bytes are on their way (TX ring + FIFO + shift register, decision 3), so giving the
// line back never waits for more than 32 bytes. hardwareFree() and giveBack() are what the cassette
// task checks before it confirms Tape or Disk to the SIO task.
//
// Only configure() with a CassetteOutputAssignment (H4 and M3a signed) touches the hardware;
// without it every call refuses and nothing is driven. One task calls everything except the
// refill interrupt: the cassette loop, which also calls pump() every tick.
// The refill callback is not in IRAM: during a flash write (NVS, OTA) the RMT ISR waits and a run
// may underrun. Nothing writes the flash while a tape plays today.

#include <atomic>
#include <cstddef>
#include <cstdint>

#include "rb/board/board_profile.hpp"
#include "rb/cassette/cassette_player.hpp"
#include "rb/cassette/cassette_tick.hpp"
#include "rb/cassette/fsk_symbol_feed.hpp"

namespace rb::esp {

class EspDataIn final : public rb::cassette::ByteOut, public rb::cassette::WaveOut, public rb::cassette::LiveLine {
public:
    static constexpr std::size_t kFeedPortions = 4096;  // >= 0.4 s of signal (100 us minimum), 16 KB
    static constexpr uint32_t kStartPortions = 1024;    // a run starts with this much queued, or with
                                                        // all the player had (no growth in a tick)
    static constexpr uint32_t kDefaultBaud = 600;
    static constexpr std::size_t kSharedInFlight = 32;  // decision 3: 32 bytes ~ 0.53 s at 600 baud
    static constexpr std::size_t kTxFifo = 128;         // the ESP32's UART TX FIFO

    EspDataIn() noexcept : feed_(rb::core::Span<uint32_t>(slots_, kFeedPortions)) {}

    // Installs UART2 TX on the granted DATA IN pin. False leaves everything as it was.
    bool configure(const rb::board::CassetteOutputAssignment& pins) noexcept;
    bool enabled() const noexcept { return pin_ >= 0; }
    int8_t pin() const noexcept { return pin_; }  // the DATA IN pin; -1 when not configured

    // Shared mode: UART2 is already installed (by the disk, with a TX ring of `txRing` bytes; 0: none)
    // and DATA IN is already its TX. Touches no hardware; the rate is set by setBaud() when the cassette
    // takes the line. False if already enabled in the other mode.
    bool attachShared(const rb::board::CassetteOutputAssignment& pins, std::size_t txRing) noexcept;
    bool shared() const noexcept { return shared_; }

    // Shared mode: bytes on their way out, never under the truth. The ring counts as empty (0) or full (its
    // size): uart_get_tx_buffer_free_size() is the driver's bound for the next write, not the free room, and
    // tells only that. Then the FIFO (128 minus its free room) and 1 for the shift register. Read in that
    // order: a byte the interrupt moves between the two reads is counted twice, never zero times. So send()
    // writes only into an empty ring, and the interrupt moves each write to the FIFO at once.
    std::size_t inFlight() const noexcept;

    // §2.4 "hardware libre": the UART has nothing left to send, no FSK run is active or queued, the RMT
    // has let go of the pin and the driver is not faulted. The cassette confirms only when true.
    bool hardwareFree() const noexcept override;

    // Gives DATA IN back to UART2 for the disk (§2.4 step 6 and the Fault recovery). Ends any FSK run,
    // routes the pin to UART2 TX and reads the GPIO matrix to check it. Busy: the RMT still has the pin
    // or bytes are still going out (ask again next tick). Uart: the pin is UART2 TX and the line is empty,
    // confirm Disk. Lost: UART2 did not take the pin (uart_set_pin failed or the matrix reads otherwise);
    // the pin is reset (output off, the 74LS07 lets go) and the driver is faulted: confirm Released.
    using GiveBack = rb::cassette::LiveLine::GiveBack;  // Busy, Uart, Lost (menu-cinta T9a.7: CassetteTick)
    GiveBack giveBack() noexcept override;

    // ByteOut
    bool setBaud(uint32_t baud) override;
    bool send(rb::core::Span<const uint8_t> bytes) override;
    // UART2 has nothing left to send (ring, FIFO, shift register). True when not configured: nothing
    // was sent.
    bool drained() const override;

    // WaveOut
    bool push(const rb::cassette::FskPortion& portion) override;
    bool idle() const override;
    void stop() override;
    bool realTime() const override { return true; }  // the RMT plays at the signal's pace
    uint32_t cuts() const override { return underruns_ + failures_; }  // runs cut or dropped

    // Cassette loop, every tick: starts a queued run, gives the pin back to the UART once a run has
    // played. After an underrun the run is cut, never resumed from the middle (cassette-cas-paridad
    // D1): portions that arrive after the RMT ran dry are dropped and the pin goes back to the UART;
    // the player sees idle() mid-run and plays the run again from its gap.
    void pump() noexcept override;

    uint32_t underruns() const noexcept { return underruns_; }
    uint32_t failures() const noexcept { return failures_; }  // runs dropped: the RMT could not play
    bool fskActive() const noexcept { return channel_ != nullptr; }  // the RMT has the pin
    // The RMT did not let go of the pin (rmt_disable or rmt_del_channel failed): the channel and its
    // encoder are kept, the pin is still the RMT's, UART2 does not get it and send() and push()
    // refuse. The feed is not cleared while the RMT may still read it. pump() retries only the
    // release, even with portions queued, until it succeeds (release() retries it too).
    bool releasePending() const noexcept { return releasePending_; }

    // The UART could not take DATA IN back: the pin is reset (no output) and everything is refused.
    bool faulted() const noexcept { return faulted_; }
    uint32_t pinFaults() const noexcept { return pinFaults_; }  // times the UART refused the pin

    // Diagnostics (fix-cinta-data-in-65xe): what the last hardwareFree() found busy, as bits
    // (kBusy*), and how many times it said no. Written by the cassette task, read by any task.
    static constexpr uint8_t kBusyDisabled = 1, kBusyFaulted = 2, kBusyChannel = 4, kBusyReleasePending = 8,
                             kBusySending = 16, kBusyFeed = 32, kBusyTx = 64;
    uint8_t lastBusy() const noexcept { return lastBusy_.load(std::memory_order_relaxed); }
    uint32_t busyRefusals() const noexcept { return busyRefusals_.load(std::memory_order_relaxed); }
    // Explicit recovery: gives the pin back to the UART. True when not (or no longer) faulted. Shared mode:
    // true only once the GPIO matrix reads UART2 TX with the output on; otherwise the pin is reset again.
    bool recover() noexcept;

    // Undoes configure(): stops any run, deletes the UART2 driver and resets the pin (output off,
    // pull-up). enabled() is false afterwards. Used by the bench test C8. False: the RMT did not let
    // go of the pin (releasePending()); nothing else was undone and a later release() retries. Also
    // false when gpio_reset_pin() fails: UART2 is already deleted, the driver stays enabled and faulted, and
    // only a later release() ends that state (it does not delete UART2 again). Until then configure() returns
    // false, recover() keeps the fault and giveBack() returns Lost.
    // Shared mode: deletes and resets nothing; detaches only when giveBack() returns Uart (pin on UART2
    // TX, checked in the matrix, nothing left to send). Otherwise false and still attached: Busy, ask
    // again; Lost, the pin is reset and the driver stays faulted.
    bool release() noexcept;

    // Optional trace of every ESP-IDF call that changes the pin or its peripherals, with its
    // esp_err_t (0 = ESP_OK). Not called for uart_wait_tx_done (a poll) nor from the RMT
    // interrupts (refill, on_trans_done). Set before configure(); nullptr: none.
    using CallTrace = void (*)(const char* call, int err);
    void setTrace(CallTrace trace) noexcept { trace_ = trace; }

private:
    bool takePin() noexcept;      // UART -> RMT; false: the UART still has the pin
    void dropRun() noexcept;      // RMT failure: drop the queued run, pin back to the UART
    bool releasePin() noexcept;   // RMT -> UART, line at mark; false: the RMT kept the pin
    bool pinToUart() noexcept;    // false: the UART refused it (shared mode: or the matrix disagrees), faulted
    int traced(const char* call, int err) const noexcept;  // reports err to the trace, returns it
    bool startTransmission() noexcept;
    bool transmitting() const noexcept;
    void disableChannel() noexcept;  // rmt_disable() once per enable
    void discardIfDue() noexcept;    // clears the feed of a cancelled run (the RMT not reading it)

    bool sharedRoom(std::size_t bytes) const noexcept;  // shared mode: may `bytes` be written now?
    bool txEmpty() const noexcept;  // UART2 has nothing left to send (ring, FIFO, shift register)

    int8_t pin_ = -1;
    bool shared_ = false;
    std::size_t txRing_ = 0;    // shared mode: the TX ring the disk installed
    uint32_t baud_ = kDefaultBaud;
    void* channel_ = nullptr;   // rmt_channel_handle_t while the pin belongs to the RMT
    void* encoder_ = nullptr;   // rmt_encoder_handle_t
    bool sending_ = false;      // a transmission was queued and has not been seen done
    bool rmtEnabled_ = false;   // the channel is enabled: rmt_disable() is due exactly once
    // Set by the RMT's on_trans_done interrupt, read by pump(): the transmission has ended.
    std::atomic<bool> txDone_{false};
    bool started_ = false;      // this run has started once (another start would resume it: an underrun)
    uint32_t underruns_ = 0;
    uint32_t failures_ = 0;
    uint32_t pinFaults_ = 0;
    bool faulted_ = false;
    bool releasePending_ = false;
    bool uartDeleted_ = false;   // own mode: release() deleted UART2 but did not reset the pin yet
    bool discardFeed_ = false;   // a cancelled or dropped run: clear the feed once the RMT is disabled
    CallTrace trace_ = nullptr;
    uint32_t lastSize_ = 0;     // feed size at the previous pump()
    mutable std::atomic<uint8_t> lastBusy_{0};        // diagnostics only: see lastBusy()
    mutable std::atomic<uint32_t> busyRefusals_{0};
    uint8_t dummy_ = 0;         // rmt_transmit() wants a payload; the encoder reads the feed
    uint32_t slots_[kFeedPortions] = {};
    rb::cassette::FskSymbolFeed feed_;
};

}  // namespace rb::esp
