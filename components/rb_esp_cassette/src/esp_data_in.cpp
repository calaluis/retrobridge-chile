#include "rb/esp/esp_data_in.hpp"

#include "driver/gpio.h"
#include "driver/rmt_encoder.h"
#include "driver/rmt_tx.h"
#include "driver/uart.h"
#include "esp_log.h"
#include "hal/uart_ll.h"
#include "rb/esp/gpio_route.hpp"

namespace rb::esp {

namespace {

constexpr const char* kTag = "data_in";
constexpr uart_port_t kUart = UART_NUM_2;
constexpr int kUartRxBuffer = 256;   // the driver wants one; RX is never routed to a pin
constexpr int kUartTxBuffer = 2048;  // records are paced by the player, at most 512 per send
constexpr uint32_t kRmtResolutionHz = 1000000;  // 1 tick = 1 us, FskPortion's unit
constexpr std::size_t kRmtMemSymbols = 64 * 8;  // as the owner's beta: all of the ESP32's RMT RAM
constexpr std::size_t kChunk = 32;
// What uart_get_tx_buffer_free_size() reads with the TX ring empty: its size minus 28 (ESP-IDF 6.1, uart.c:
// 8 for the ring's item header, and 8 + 12 for the description item, uart_tx_data_t, each write puts first).
// Any item still in the ring caps the bound at the ring's max item size (about half the ring) minus 20, so
// "empty" can be told apart exactly. Another IDF with other sizes reads as never empty: send() then refuses
// and the tape stalls, which is safe; CI pins ESP-IDF 6.1.
constexpr std::size_t kTxRingIdleCost = 8 + 8 + 12;

rmt_channel_handle_t as_channel(void* p) { return static_cast<rmt_channel_handle_t>(p); }
rmt_encoder_handle_t as_encoder(void* p) { return static_cast<rmt_encoder_handle_t>(p); }

// RMT refill interrupt: symbols straight from the feed. No I/O, no allocation, no logging, no
// locks (FskSymbolFeed is lock-free). Adapted from the owner's own cassette_fsk_encode_cb.
size_t encode_from_feed(const void* /*data*/, size_t /*data_size*/, size_t /*symbols_written*/,
                        size_t symbols_free, rmt_symbol_word_t* symbols, bool* done, void* arg)
{
    auto* feed = static_cast<rb::cassette::FskSymbolFeed*>(arg);
    rb::cassette::FskSymbol chunk[kChunk];
    size_t written = 0;
    *done = false;
    while (written < symbols_free) {
        if (written > 0 && feed->size() < 2) {
            break;  // whole symbols already written: leave the end for the next refill
        }
        const size_t room = symbols_free - written < kChunk ? symbols_free - written : kChunk;
        bool end = false;
        const size_t n = feed->fill(rb::core::Span<rb::cassette::FskSymbol>(chunk, room), end);
        for (size_t i = 0; i < n; ++i) {
            symbols[written + i].duration0 = chunk[i].duration0;
            symbols[written + i].level0 = chunk[i].level0;
            symbols[written + i].duration1 = chunk[i].duration1;
            symbols[written + i].level1 = chunk[i].level1;
        }
        written += n;
        if (end) {
            *done = true;
            break;
        }
        if (n < room) {
            break;  // one portion left after whole pairs: it waits for the next refill
        }
    }
    return written;
}

// RMT "transmission done" interrupt: only raises the flag pump() reads. No I/O, no logging.
bool on_tx_done(rmt_channel_handle_t /*channel*/, const rmt_tx_done_event_data_t* /*event*/, void* ctx)
{
    static_cast<std::atomic<bool>*>(ctx)->store(true, std::memory_order_release);
    return false;  // no task woken
}

}  // namespace

bool EspDataIn::configure(const rb::board::CassetteOutputAssignment& pins) noexcept
{
    if (enabled()) {
        return true;
    }
    uart_config_t config{};
    config.baud_rate = static_cast<int>(kDefaultBaud);
    config.data_bits = UART_DATA_8_BITS;
    config.parity = UART_PARITY_DISABLE;
    config.stop_bits = UART_STOP_BITS_1;
    config.flow_ctrl = UART_HW_FLOWCTRL_DISABLE;
    config.source_clk = UART_SCLK_DEFAULT;
    if (traced("uart_driver_install", uart_driver_install(kUart, kUartRxBuffer, kUartTxBuffer, 0, nullptr, 0)) != ESP_OK) {
        ESP_LOGE(kTag, "UART2 driver not installed: DATA IN not driven");
        return false;
    }
    if (traced("uart_param_config", uart_param_config(kUart, &config)) != ESP_OK ||
        traced("uart_set_pin", uart_set_pin(kUart, pins.dataIn(), UART_PIN_NO_CHANGE, UART_PIN_NO_CHANGE, UART_PIN_NO_CHANGE)) != ESP_OK) {
        (void)traced("uart_driver_delete", uart_driver_delete(kUart));
        ESP_LOGE(kTag, "UART2 not set on GPIO%d: DATA IN not driven", pins.dataIn());
        return false;
    }
    pin_ = pins.dataIn();
    baud_ = kDefaultBaud;
    ESP_LOGI(kTag, "DATA IN on GPIO%d: records by UART2, FSK by RMT", pin_);
    return true;
}

bool EspDataIn::attachShared(const rb::board::CassetteOutputAssignment& pins, std::size_t txRing) noexcept
{
    if (enabled()) {
        return shared_;
    }
    // UART2 and its pins are the disk's, set once at boot: nothing is installed, deleted or routed here.
    pin_ = pins.dataIn();
    shared_ = true;
    txRing_ = txRing;
    baud_ = 0;  // unknown (the disk's rate): the first setBaud() sets it
    ESP_LOGI(kTag, "DATA IN on GPIO%d: shared UART2 (TX ring %u), FSK by RMT", pin_, static_cast<unsigned>(txRing));
    return true;
}

std::size_t EspDataIn::inFlight() const noexcept
{
    // Ring first, FIFO second: a byte the interrupt moves in between is counted twice, never missed.
    // uart_get_tx_buffer_free_size() is the driver's bound for the next write, not the free room, so it only
    // says whether the ring is empty. Not empty (or unreadable): counted as full.
    std::size_t ring = 0;
    if (txRing_ > 0) {
        std::size_t free = 0;
        if (uart_get_tx_buffer_free_size(kUart, &free) != ESP_OK || free + kTxRingIdleCost < txRing_) {
            ring = txRing_;
        }
    }
    const uint32_t fifoFree = uart_ll_get_txfifo_len(UART_LL_GET_HW(kUart));
    const std::size_t fifo = fifoFree < kTxFifo ? kTxFifo - fifoFree : 0;
    return ring + fifo + 1;  // + the shift register, counted as busy
}

bool EspDataIn::sharedRoom(std::size_t bytes) const noexcept
{
    if (txRing_ == 0) {
        // No ring (the alternative of decision 3): at most 32 bytes, written only with the FIFO and the shift
        // register empty, so uart_write_bytes() copies them straight into the FIFO and returns.
        return bytes <= kSharedInFlight && txEmpty();
    }
    const std::size_t busy = inFlight();
    return busy < kSharedInFlight && bytes <= kSharedInFlight - busy - 1;
}

bool EspDataIn::hardwareFree() const noexcept
{
    return enabled() && !faulted_ && channel_ == nullptr && !releasePending_ && !sending_ && feed_.empty() &&
           txEmpty();
}

EspDataIn::GiveBack EspDataIn::giveBack() noexcept
{
    if (!enabled()) {
        return GiveBack::Lost;
    }
    stop();  // any FSK run ends; the RMT lets go of the pin (or keeps it: releasePending)
    if (releasePending_) {
        return GiveBack::Busy;  // the RMT still has the pin: never route the UART over it
    }
    // Route DATA IN to UART2 TX; in shared mode pinToUart() also reads the GPIO matrix (ESP_OK alone is
    // not proof). It resets the pin and faults when either fails.
    if (!pinToUart()) {
        return GiveBack::Lost;
    }
    if (faulted_) {
        faulted_ = false;
        ESP_LOGI(kTag, "DATA IN back on UART2 (GPIO%d), checked in the GPIO matrix", pin_);
    }
    return txEmpty() ? GiveBack::Uart : GiveBack::Busy;
}

bool EspDataIn::drained() const
{
    return !enabled() || txEmpty();
}

bool EspDataIn::txEmpty() const noexcept { return uart_wait_tx_done(kUart, 0) == ESP_OK; }

bool EspDataIn::setBaud(uint32_t baud)
{
    if (!enabled() || faulted_ || baud == 0) {
        return false;
    }
    if (baud == baud_) {
        return true;
    }
    if (uart_wait_tx_done(kUart, 0) != ESP_OK) {
        return false;  // bytes still going out at the old rate: try again later
    }
    if (traced("uart_set_baudrate", uart_set_baudrate(kUart, baud)) != ESP_OK) {
        return false;
    }
    baud_ = baud;
    return true;
}

bool EspDataIn::send(rb::core::Span<const uint8_t> bytes)
{
    if (!enabled() || faulted_ || channel_ != nullptr) {
        return false;  // no output, a fault, or the pin belongs to the RMT until its run has played
    }
    if (bytes.size() == 0) {
        return true;
    }
    if (shared_ && !sharedRoom(bytes.size())) {
        return false;  // busy, nothing taken: the player keeps the bytes for a later tick
    }
    const int written = uart_write_bytes(kUart, bytes.data(), bytes.size());
    const bool all = written == static_cast<int>(bytes.size());
    (void)traced("uart_write_bytes", all ? ESP_OK : ESP_FAIL);
    return all;
}

bool EspDataIn::push(const rb::cassette::FskPortion& portion)
{
    return enabled() && !faulted_ && !releasePending_ && feed_.push(portion);
}

bool EspDataIn::idle() const
{
    return feed_.empty() && channel_ == nullptr;
}

void EspDataIn::stop()
{
    discardFeed_ = true;  // cleared by releasePin() once the RMT is disabled, never under it
    (void)releasePin();
    sending_ = false;
    started_ = false;
    lastSize_ = 0;
}

void EspDataIn::pump() noexcept
{
    if (!enabled() || faulted_) {
        return;
    }
    if (releasePending_) {
        (void)releasePin();  // the RMT has to let go first: no run starts, even with portions queued
        lastSize_ = feed_.size();
        return;
    }
    if (channel_ != nullptr && sending_ && !transmitting()) {
        sending_ = false;
    }
    const uint32_t size = feed_.size();
    if (channel_ != nullptr && !sending_) {
        if (size == 0) {
            releasePin();  // the run has played: the pin goes back to the UART, at mark
            started_ = false;
        } else {
            const bool restart = started_;
            if (!startTransmission()) {
                dropRun();
            } else if (restart) {
                ++underruns_;  // the signal stopped mid-run and starts again: the Atari may not load
            }
        }
    } else if (channel_ == nullptr && size > 0 && (size >= kStartPortions || size == lastSize_) &&
               uart_wait_tx_done(kUart, 0) == ESP_OK) {  // last record bytes out first
        if (!takePin() || !startTransmission()) {
            dropRun();
        }
    }
    lastSize_ = feed_.size();
}

// The RMT says when a transmission ends (on_trans_done). Polling rmt_tx_wait_all_done(0) instead
// logged "flush timeout" as an error on every tick of a run (bench test C8, d05bfe2).
bool EspDataIn::transmitting() const noexcept
{
    return channel_ != nullptr && !txDone_.load(std::memory_order_acquire);
}

void EspDataIn::disableChannel() noexcept
{
    // Once per enable: IDF refuses a second rmt_disable() (ESP_ERR_INVALID_STATE, bench test C8).
    if (channel_ == nullptr || !rmtEnabled_) {
        return;
    }
    if (traced("rmt_disable", rmt_disable(as_channel(channel_))) == ESP_OK) {
        rmtEnabled_ = false;
    }
}

void EspDataIn::dropRun() noexcept
{
    // The RMT could not play: the run is dropped (the Atari will not load it) instead of retrying
    // every tick. The player sees idle() and goes on; DATA IN goes back to the idle UART.
    discardFeed_ = true;
    (void)releasePin();
    started_ = false;
    ++failures_;
    ESP_LOGE(kTag, "FSK run dropped: RMT not available (%u so far)", static_cast<unsigned>(failures_));
}

// A channel per run, created and deleted on the cassette loop: the pin is the UART's between runs.
// The RMT driver allocates while doing so (outside ADR-011's start-up rule, as the owner's beta).
bool EspDataIn::takePin() noexcept
{
    rmt_tx_channel_config_t config{};
    config.gpio_num = static_cast<gpio_num_t>(pin_);
    config.clk_src = RMT_CLK_SRC_DEFAULT;
    config.resolution_hz = kRmtResolutionHz;
    config.mem_block_symbols = kRmtMemSymbols;
    config.trans_queue_depth = 1;
    config.flags.init_level = 1;  // the RMT holds mark until the first symbol: DATA IN released
    rmt_channel_handle_t channel = nullptr;
    if (traced("rmt_new_tx_channel", rmt_new_tx_channel(&config, &channel)) != ESP_OK) {
        (void)pinToUart();
        return false;
    }
    rmt_simple_encoder_config_t simple{};
    simple.callback = &encode_from_feed;
    simple.arg = &feed_;
    simple.min_chunk_size = 0;
    rmt_encoder_handle_t encoder = nullptr;
    if (traced("rmt_new_simple_encoder", rmt_new_simple_encoder(&simple, &encoder)) != ESP_OK) {
        (void)traced("rmt_del_channel", rmt_del_channel(channel));
        (void)pinToUart();
        return false;
    }
    rmt_tx_event_callbacks_t callbacks{};
    callbacks.on_trans_done = &on_tx_done;
    txDone_.store(false, std::memory_order_relaxed);
    if (traced("rmt_tx_register_event_callbacks", rmt_tx_register_event_callbacks(channel, &callbacks, &txDone_)) !=
            ESP_OK ||
        traced("rmt_enable", rmt_enable(channel)) != ESP_OK) {
        (void)traced("rmt_del_encoder", rmt_del_encoder(encoder));
        (void)traced("rmt_del_channel", rmt_del_channel(channel));
        (void)pinToUart();
        return false;
    }
    channel_ = channel;
    encoder_ = encoder;
    rmtEnabled_ = true;
    started_ = false;
    return true;
}

bool EspDataIn::startTransmission() noexcept
{
    rmt_transmit_config_t config{};
    config.loop_count = 0;
    config.flags.eot_level = 1;  // mark after every transmission: DATA IN released
    txDone_.store(false, std::memory_order_release);  // before the RMT can notify this transmission
    if (traced("rmt_transmit", rmt_transmit(as_channel(channel_), as_encoder(encoder_), &dummy_, sizeof dummy_, &config)) !=
        ESP_OK) {
        return false;
    }
    sending_ = true;
    started_ = true;
    return true;
}

bool EspDataIn::releasePin() noexcept
{
    if (channel_ == nullptr) {
        discardIfDue();
        releasePending_ = false;
        return true;
    }
    sending_ = false;
    disableChannel();  // aborts a transmission in flight; not again after it succeeded once
    if (!rmtEnabled_) {
        discardIfDue();  // the RMT no longer reads the feed
    }
    // Still enabled, or not deleted: the RMT still has the pin and may still call the encoder. Both are
    // kept and UART2 does not get the pin; pump() or release() tries again.
    if (rmtEnabled_ || traced("rmt_del_channel", rmt_del_channel(as_channel(channel_))) != ESP_OK) {
        if (!releasePending_) {
            ESP_LOGE(kTag, "RMT did not let go of GPIO%d: channel kept, DATA IN not on UART2, retrying", pin_);
        }
        releasePending_ = true;
        return false;
    }
    channel_ = nullptr;
    (void)traced("rmt_del_encoder", rmt_del_encoder(as_encoder(encoder_)));  // no channel calls it any more
    encoder_ = nullptr;
    if (releasePending_) {
        ESP_LOGI(kTag, "RMT let go of GPIO%d on a retry", pin_);
        releasePending_ = false;
    }
    // The UART takes the pin back; it idles high (mark).
    return pinToUart();
}

void EspDataIn::discardIfDue() noexcept
{
    if (discardFeed_) {
        feed_.clear();
        discardFeed_ = false;
    }
}

bool EspDataIn::pinToUart() noexcept
{
    // Shared mode (menu-cinta T9a.2): the disk gets the line back only once the GPIO matrix reads U2TXD with
    // the output on. The own mode keeps its behaviour (bench test C8 checks the matrix from outside).
    if (traced("uart_set_pin", uart_set_pin(kUart, pin_, UART_PIN_NO_CHANGE, UART_PIN_NO_CHANGE, UART_PIN_NO_CHANGE)) == ESP_OK &&
        (!shared_ || drivenByUart2Tx(pin_))) {
        return true;
    }
    // Nobody may drive DATA IN now: gpio_reset_pin() turns its output off with a pull-up, so the 74LS07 lets the line go.
    const bool safe = traced("gpio_reset_pin", gpio_reset_pin(static_cast<gpio_num_t>(pin_))) == ESP_OK;
    feed_.clear();
    sending_ = false;
    started_ = false;
    lastSize_ = 0;
    if (!faulted_) {  // once per fault: giveBack() retries every 100 ms while it lasts
        ++pinFaults_;
        ESP_LOGE(kTag, "UART2 did not take GPIO%d back: DATA IN %s, output off until recover()", pin_,
                 safe ? "released (output off, pull-up)" : "NOT reset");
    }
    faulted_ = true;
    return false;
}

bool EspDataIn::recover() noexcept
{
    if (!enabled()) {
        return false;
    }
    if (!faulted_) {
        return true;
    }
    if (shared_) {
        // Routed and checked in the GPIO matrix, or the pin is reset again and the fault stays. Bytes still
        // in UART2 go out on DATA IN afterwards; hardwareFree() stays false until they have.
        if (!pinToUart()) {
            return false;
        }
        faulted_ = false;
        ESP_LOGI(kTag, "recover: DATA IN back on UART2 (GPIO%d), checked in the GPIO matrix", pin_);
        return true;
    }
    if (traced("uart_set_pin", uart_set_pin(kUart, pin_, UART_PIN_NO_CHANGE, UART_PIN_NO_CHANGE, UART_PIN_NO_CHANGE)) != ESP_OK) {
        ESP_LOGE(kTag, "recover: UART2 still refuses GPIO%d; DATA IN stays reset (output off)", pin_);
        return false;
    }
    faulted_ = false;
    ESP_LOGI(kTag, "recover: DATA IN back on UART2 (GPIO%d), at mark", pin_);
    return true;
}

bool EspDataIn::release() noexcept
{
    if (!enabled()) {
        return true;
    }
    stop();  // an FSK run in flight ends; the pin goes back to the UART (or the driver faults)
    if (releasePending_) {
        ESP_LOGE(kTag, "DATA IN not released: the RMT still has GPIO%d; release() again later", pin_);
        return false;  // UART2 and the pin stay as they are
    }
    if (shared_) {
        // Detach only with DATA IN back on UART2 TX (checked in the GPIO matrix) and nothing left to send.
        // Busy: stays attached, release() again later. Lost: the pin is reset, the driver stays faulted and
        // attached, so nobody takes the line for good while it is not the disk's.
        const GiveBack line = giveBack();
        if (line != GiveBack::Uart) {
            return false;
        }
        ESP_LOGI(kTag, "DATA IN detached from the shared UART2 (GPIO%d)", pin_);
        pin_ = -1;
        shared_ = false;
        return true;
    }
    (void)traced("uart_driver_delete", uart_driver_delete(kUart));
    // No peripheral keeps the pin: output off, pull-up, so the 74LS07 lets DATA IN go.
    (void)traced("gpio_reset_pin", gpio_reset_pin(static_cast<gpio_num_t>(pin_)));
    ESP_LOGI(kTag, "DATA IN released: UART2 deleted, GPIO%d reset (output off)", pin_);
    pin_ = -1;
    faulted_ = false;
    return true;
}

int EspDataIn::traced(const char* call, int err) const noexcept
{
    if (trace_ != nullptr) {
        trace_(call, err);
    }
    return err;
}

}  // namespace rb::esp
