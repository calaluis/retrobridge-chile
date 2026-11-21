#include "rb/esp/esp_data_in.hpp"

#include "driver/gpio.h"
#include "driver/rmt_encoder.h"
#include "driver/rmt_tx.h"
#include "driver/uart.h"
#include "esp_log.h"

namespace rb::esp {

namespace {

constexpr const char* kTag = "data_in";
constexpr uart_port_t kUart = UART_NUM_2;
constexpr int kUartRxBuffer = 256;   // the driver wants one; RX is never routed to a pin
constexpr int kUartTxBuffer = 2048;  // records are paced by the player, at most 512 per send
constexpr uint32_t kRmtResolutionHz = 1000000;  // 1 tick = 1 us, FskPortion's unit
constexpr std::size_t kRmtMemSymbols = 64 * 8;  // as the owner's beta: all of the ESP32's RMT RAM
constexpr std::size_t kChunk = 32;

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
    if (uart_driver_install(kUart, kUartRxBuffer, kUartTxBuffer, 0, nullptr, 0) != ESP_OK) {
        ESP_LOGE(kTag, "UART2 driver not installed: DATA IN not driven");
        return false;
    }
    if (uart_param_config(kUart, &config) != ESP_OK ||
        uart_set_pin(kUart, pins.dataIn(), UART_PIN_NO_CHANGE, UART_PIN_NO_CHANGE, UART_PIN_NO_CHANGE) != ESP_OK) {
        (void)uart_driver_delete(kUart);
        ESP_LOGE(kTag, "UART2 not set on GPIO%d: DATA IN not driven", pins.dataIn());
        return false;
    }
    pin_ = pins.dataIn();
    baud_ = kDefaultBaud;
    ESP_LOGI(kTag, "DATA IN on GPIO%d: records by UART2, FSK by RMT", pin_);
    return true;
}

bool EspDataIn::setBaud(uint32_t baud)
{
    if (!enabled() || baud == 0) {
        return false;
    }
    if (baud == baud_) {
        return true;
    }
    if (uart_wait_tx_done(kUart, 0) != ESP_OK) {
        return false;  // bytes still going out at the old rate: try again later
    }
    if (uart_set_baudrate(kUart, baud) != ESP_OK) {
        return false;
    }
    baud_ = baud;
    return true;
}

bool EspDataIn::send(rb::core::Span<const uint8_t> bytes)
{
    if (!enabled() || channel_ != nullptr) {
        return false;  // no output, or the pin belongs to the RMT until its run has played
    }
    if (bytes.size() == 0) {
        return true;
    }
    const int written = uart_write_bytes(kUart, bytes.data(), bytes.size());
    return written == static_cast<int>(bytes.size());
}

bool EspDataIn::push(const rb::cassette::FskPortion& portion)
{
    return enabled() && feed_.push(portion);
}

bool EspDataIn::idle() const
{
    return feed_.empty() && channel_ == nullptr;
}

void EspDataIn::stop()
{
    if (channel_ != nullptr) {
        (void)rmt_disable(as_channel(channel_));  // aborts the transmission in flight
    }
    feed_.clear();
    releasePin();
    sending_ = false;
    started_ = false;
    lastSize_ = 0;
}

void EspDataIn::pump() noexcept
{
    if (!enabled()) {
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

bool EspDataIn::transmitting() const noexcept
{
    return channel_ != nullptr && rmt_tx_wait_all_done(as_channel(channel_), 0) == ESP_ERR_TIMEOUT;
}

void EspDataIn::dropRun() noexcept
{
    // The RMT could not play: the run is dropped (the Atari will not load it) instead of retrying
    // every tick. The player sees idle() and goes on; DATA IN goes back to the idle UART.
    feed_.clear();
    releasePin();
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
    if (rmt_new_tx_channel(&config, &channel) != ESP_OK) {
        (void)uart_set_pin(kUart, pin_, UART_PIN_NO_CHANGE, UART_PIN_NO_CHANGE, UART_PIN_NO_CHANGE);
        return false;
    }
    rmt_simple_encoder_config_t simple{};
    simple.callback = &encode_from_feed;
    simple.arg = &feed_;
    simple.min_chunk_size = 0;
    rmt_encoder_handle_t encoder = nullptr;
    if (rmt_new_simple_encoder(&simple, &encoder) != ESP_OK) {
        (void)rmt_del_channel(channel);
        (void)uart_set_pin(kUart, pin_, UART_PIN_NO_CHANGE, UART_PIN_NO_CHANGE, UART_PIN_NO_CHANGE);
        return false;
    }
    if (rmt_enable(channel) != ESP_OK) {
        (void)rmt_del_encoder(encoder);
        (void)rmt_del_channel(channel);
        (void)uart_set_pin(kUart, pin_, UART_PIN_NO_CHANGE, UART_PIN_NO_CHANGE, UART_PIN_NO_CHANGE);
        return false;
    }
    channel_ = channel;
    encoder_ = encoder;
    started_ = false;
    return true;
}

bool EspDataIn::startTransmission() noexcept
{
    rmt_transmit_config_t config{};
    config.loop_count = 0;
    config.flags.eot_level = 1;  // mark after every transmission: DATA IN released
    if (rmt_transmit(as_channel(channel_), as_encoder(encoder_), &dummy_, sizeof dummy_, &config) != ESP_OK) {
        return false;
    }
    sending_ = true;
    started_ = true;
    return true;
}

void EspDataIn::releasePin() noexcept
{
    if (channel_ == nullptr) {
        return;
    }
    (void)rmt_disable(as_channel(channel_));  // already disabled after stop(): harmless
    (void)rmt_del_channel(as_channel(channel_));
    (void)rmt_del_encoder(as_encoder(encoder_));
    channel_ = nullptr;
    encoder_ = nullptr;
    sending_ = false;
    // The UART takes the pin back; it idles high (mark).
    (void)uart_set_pin(kUart, pin_, UART_PIN_NO_CHANGE, UART_PIN_NO_CHANGE, UART_PIN_NO_CHANGE);
}

}  // namespace rb::esp
