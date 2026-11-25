#include "fake_idf.hpp"

#include <algorithm>

#include "driver/gpio.h"
#include "driver/rmt_encoder.h"
#include "driver/rmt_tx.h"
#include "driver/uart.h"
#include "esp_log.h"
#include "esp_rom_sys.h"
#include "esp_timer.h"
#include "freertos/task.h"

namespace fake_idf {

namespace {
State g_state;
}  // namespace

int g_channel = 0;  // its address is the channel handle
int g_encoder = 0;  // its address is the encoder handle

State& state() { return g_state; }

void reset() { g_state = State{}; }

std::size_t play_all()
{
    State& s = g_state;
    std::size_t total = 0;
    if (s.encoderCallback == nullptr) {
        return 0;
    }
    auto cb = reinterpret_cast<rmt_encode_simple_cb_t>(s.encoderCallback);
    rmt_symbol_word_t symbols[64];
    bool done = false;
    for (int guard = 0; guard < 100000 && !done; ++guard) {
        const std::size_t n = cb(nullptr, 0, total, 64, symbols, &done, s.encoderArg);
        total += n;
        if (n == 0 && !done) {
            break;  // nothing more queued: the RMT would end the transmission (eot)
        }
    }
    const bool wasBusy = s.rmtBusy;
    s.rmtBusy = false;
    if (wasBusy && s.doneCallback != nullptr) {
        const rmt_tx_done_event_data_t event{total};
        reinterpret_cast<rmt_tx_done_callback_t>(s.doneCallback)(reinterpret_cast<rmt_channel_handle_t>(&g_channel),
                                                                 &event, s.doneContext);
    }
    return total;
}

bool line_released()
{
    return !(g_state.owner == Owner::Rmt && g_state.rmtBusy);
}

namespace {

int64_t byteUs(const State& s)
{
    const int64_t baud = s.uartBaud > 0 ? s.uartBaud : 600;
    return (10 * 1000000 + baud - 1) / baud;  // 8N1: 10 bits a byte
}

int align4(int n) { return (n + 3) & ~3; }

// NOSPLIT ring geometry (ESP-IDF ringbuf): the largest item and the size uart.c infers from it.
int maxItem(const State& s) { return align4(s.txRing / 2) - kRingItemHeader; }

int offFree(const State& s) { return s.txItems.empty() ? s.txWp : s.txItems.front().off; }

// Where an item of `len` bytes (header included) goes now, or -1: it does not fit. NOSPLIT: an item never
// wraps; if the tail is too short it goes to the start and the tail is left unused.
int place(const State& s, int len)
{
    if (len - kRingItemHeader > maxItem(s)) {
        return -1;
    }
    const int acq = s.txWp;
    const int free = offFree(s);
    if (s.txItems.empty() || acq > free) {
        if (acq + len <= s.txRing) {
            return acq;
        }
        return (s.txItems.empty() ? len <= s.txRing : len < free) ? 0 : -1;
    }
    return acq + len < free ? acq : -1;  // the free room is between the write point and the oldest item
}

// Puts an item in the ring, waiting (line time passes) while it does not fit.
void put(State& s, int payload, int data)
{
    const int len = kRingItemHeader + align4(payload);
    int at = place(s, len);
    while (at < 0) {
        tx_advance(s.txShiftLeftUs > 0 ? s.txShiftLeftUs : 1);
        at = place(s, len);
    }
    s.txItems.push_back(State::RingItem{at, len, data, 0});
    s.txWp = at + len == s.txRing ? 0 : at + len;
}

// The driver's interrupt (ring -> FIFO, unless held) and the hardware (FIFO -> shift register).
void tx_hardware(State& s)
{
    while (!s.txIsrHeld && !s.txItems.empty() && s.txFifo < kTxFifo) {
        State::RingItem& item = s.txItems.front();
        const int move = std::min(item.data - item.copied, kTxFifo - s.txFifo);
        item.copied += move;
        s.txFifo += move;
        if (item.copied == item.data) {
            s.txItems.erase(s.txItems.begin());  // returned to the ring
        }
    }
    if (s.txShiftLeftUs == 0 && s.txFifo > 0) {
        --s.txFifo;
        s.txShiftLeftUs = byteUs(s);
    }
}

}  // namespace

void tx_advance(int64_t us)
{
    State& s = g_state;
    while (us > 0) {
        tx_hardware(s);
        if (s.txShiftLeftUs == 0) {
            s.nowUs += us;
            return;
        }
        const int64_t step = std::min(us, s.txShiftLeftUs);
        s.nowUs += step;
        us -= step;
        s.txShiftLeftUs -= step;
        if (s.txShiftLeftUs == 0) {
            ++s.txBytesOut;
        }
    }
    tx_hardware(s);
}

int tx_in_flight()
{
    const State& s = g_state;
    int ring = 0;
    for (const State::RingItem& item : s.txItems) {
        ring += item.data - item.copied;
    }
    return ring + s.txFifo + (s.txShiftLeftUs > 0 ? 1 : 0);
}

bool tx_empty() { return tx_in_flight() == 0; }

void tx_hardware_now() { tx_hardware(g_state); }

void put_item(int payload, int data) { put(g_state, payload, data); }

int tx_free_size()
{
    const State& s = g_state;
    if (s.txRing == 0 || place(s, kRingItemHeader + 4) < 0) {
        return 0;  // no ring, or not even the smallest item fits
    }
    const int maxItemSize = maxItem(s);
    const int acq = s.txWp;
    const int free = offFree(s);
    const int bufSizeMin = (2 * (maxItemSize + kRingItemHeader - 3)) & ~3;
    const auto usable = [&](int seg) {
        if (seg <= kRingItemHeader) {
            return 0;
        }
        return std::min((seg - kRingItemHeader) & ~3, maxItemSize);
    };
    int total = 0;
    if (acq == free && s.txItems.empty()) {
        total = s.txRing - kRingItemHeader;  // empty: one large segment (uart.c)
    } else if (acq <= free) {
        total = usable(free - acq);
    } else {
        total = usable(bufSizeMin - acq) + usable(free);
    }
    const int desc = kRingItemHeader + align4(12);
    return total > desc ? total - desc : 0;
}

}  // namespace fake_idf

using fake_idf::Owner;

void fake_idf_log(char level, const char* /*tag*/, const char* /*format*/, ...)
{
    if (level == 'E') {
        ++fake_idf::state().errorLogs;
    }
}

esp_err_t gpio_reset_pin(gpio_num_t gpio_num)
{
    auto& s = fake_idf::state();
    ++s.gpioResets;
    if (s.failGpioReset) {
        return ESP_FAIL;
    }
    if (gpio_num == s.pin || s.pin < 0) {
        s.pin = gpio_num;
        s.owner = Owner::Gpio;  // default state: output off, pull-up: the 74LS07 lets the line go
        s.outSel = 256;
        s.outEnabled = false;
    }
    return ESP_OK;
}

esp_err_t uart_driver_install(uart_port_t, int, int tx, int, QueueHandle_t*, int)
{
    auto& s = fake_idf::state();
    ++s.uartInstalls;
    if (s.failUartInstall) {
        return ESP_FAIL;
    }
    s.uartInstalled = true;
    s.txRing = tx;
    return ESP_OK;
}

esp_err_t uart_driver_delete(uart_port_t)
{
    ++fake_idf::state().uartDeletes;
    fake_idf::state().uartInstalled = false;
    return ESP_OK;
}

esp_err_t uart_get_tx_buffer_free_size(uart_port_t, size_t* size)
{
    auto& s = fake_idf::state();
    *size = static_cast<size_t>(fake_idf::tx_free_size());
    if (s.txIsrAfterRingRead) {
        // The interrupt fires between this read and the FIFO read: bytes move from the ring to the FIFO.
        const bool held = s.txIsrHeld;
        s.txIsrHeld = false;
        fake_idf::tx_hardware_now();
        s.txIsrHeld = held;
    }
    return ESP_OK;
}

esp_err_t uart_param_config(uart_port_t, const uart_config_t* config)
{
    auto& s = fake_idf::state();
    s.uartBaud = config->baud_rate;
    s.uartWordLength = static_cast<int>(config->data_bits);
    s.uartParity = static_cast<int>(config->parity);
    s.uartStopBits = static_cast<int>(config->stop_bits);
    return ESP_OK;
}

esp_err_t uart_set_rx_full_threshold(uart_port_t, int threshold)
{
    fake_idf::state().uartRxFullThreshold = threshold;
    return ESP_OK;
}

esp_err_t uart_set_rx_timeout(uart_port_t, const uint8_t tout_thresh)
{
    fake_idf::state().uartRxTimeout = tout_thresh;
    return ESP_OK;
}

esp_err_t uart_set_pin(uart_port_t, int tx, int rx, int, int)
{
    auto& s = fake_idf::state();
    if (tx >= 0) {
        s.uartTxPin = tx;
    }
    if (rx >= 0) {
        s.uartRxPin = rx;
    }
    ++s.uartSetPinCalls;
    if (s.failUartSetPin) {
        return ESP_FAIL;  // the matrix is left as it was
    }
    s.pin = tx;
    s.owner = Owner::Uart;
    s.outSel = fake_idf::kU2TxdOutIdx;
    s.outEnabled = true;
    return ESP_OK;
}

esp_err_t uart_wait_tx_done(uart_port_t, uint32_t ticks)
{
    auto& s = fake_idf::state();
    ++s.uartWaitTxDone;
    if (!s.txModel) {
        return ESP_OK;
    }
    // 1 tick = 1 ms (CONFIG_FREERTOS_HZ=1000): waits for the line to empty, at most `ticks`.
    const int64_t until = s.nowUs + static_cast<int64_t>(ticks) * 1000;
    fake_idf::tx_hardware_now();
    while (!fake_idf::tx_empty() && s.nowUs < until) {
        fake_idf::tx_advance(std::min<int64_t>(until - s.nowUs, s.txShiftLeftUs > 0 ? s.txShiftLeftUs : 1));
    }
    return fake_idf::tx_empty() ? ESP_OK : ESP_ERR_TIMEOUT;
}

esp_err_t uart_set_baudrate(uart_port_t, uint32_t baud)
{
    auto& s = fake_idf::state();
    ++s.uartSetBaudrates;
    if (s.failUartSetBaudrate) {
        return ESP_FAIL;
    }
    s.uartBaud = static_cast<int>(baud);
    return ESP_OK;
}

esp_err_t uart_get_baudrate(uart_port_t, uint32_t* baudrate)
{
    const auto& s = fake_idf::state();
    *baudrate = static_cast<uint32_t>(s.uartBaudReadback > 0 ? s.uartBaudReadback : s.uartBaud);
    return ESP_OK;
}

esp_err_t uart_get_word_length(uart_port_t, uart_word_length_t* data_bit)
{
    *data_bit = static_cast<uart_word_length_t>(fake_idf::state().uartWordLength);
    return ESP_OK;
}

esp_err_t uart_get_parity(uart_port_t, uart_parity_t* parity_mode)
{
    *parity_mode = static_cast<uart_parity_t>(fake_idf::state().uartParity);
    return ESP_OK;
}

esp_err_t uart_get_stop_bits(uart_port_t, uart_stop_bits_t* stop_bit)
{
    *stop_bit = static_cast<uart_stop_bits_t>(fake_idf::state().uartStopBits);
    return ESP_OK;
}

int uart_write_bytes(uart_port_t, const void* src, size_t size)
{
    auto& s = fake_idf::state();
    if (s.failUartWrite) {
        return -1;
    }
    const auto* bytes = static_cast<const uint8_t*>(src);
    s.written.insert(s.written.end(), bytes, bytes + size);
    if (!s.txModel) {
        s.nowUs += static_cast<int64_t>((size * 10u * 1000000u + 19199u) / 19200u);
        return static_cast<int>(size);
    }
    const int64_t start = s.nowUs;
    const int n = static_cast<int>(size);
    if (s.txRing > 0) {
        // uart_tx_all(): the description item, then the data (one item: our writes are far below max_item).
        fake_idf::put_item(12, 0);
        fake_idf::put_item(n, n);
    } else {
        // No ring: the caller copies into the FIFO and blocks until all of it is in.
        int left = n;
        while (left > 0) {
            fake_idf::tx_hardware_now();
            const int put = std::min(left, fake_idf::kTxFifo - s.txFifo);
            s.txFifo += put;
            left -= put;
            if (left > 0) {
                fake_idf::tx_hardware_now();
                fake_idf::tx_advance(s.txShiftLeftUs > 0 ? s.txShiftLeftUs : 1);
            }
        }
    }
    fake_idf::tx_hardware_now();
    s.txWriteBlockedUs = std::max(s.txWriteBlockedUs, s.nowUs - start);
    s.txMaxInFlight = std::max(s.txMaxInFlight, fake_idf::tx_in_flight());
    return n;
}

esp_err_t rmt_new_tx_channel(const rmt_tx_channel_config_t* config, rmt_channel_handle_t* ret)
{
    auto& s = fake_idf::state();
    ++s.rmtNewChannels;
    if (s.failRmtNewChannel) {
        return ESP_FAIL;
    }
    s.rmtChannel = true;
    s.pin = config->gpio_num;
    s.owner = Owner::Rmt;
    s.outSel = fake_idf::kRmtOut0Idx;
    s.outEnabled = true;
    s.initLevel = config->flags.init_level;
    *ret = reinterpret_cast<rmt_channel_handle_t>(&fake_idf::g_channel);
    return ESP_OK;
}

esp_err_t rmt_del_channel(rmt_channel_handle_t)
{
    auto& s = fake_idf::state();
    ++s.rmtDelChannelCalls;
    if (s.failRmtDelChannel) {
        ++s.errorLogs;
        return ESP_FAIL;  // nothing changes: the channel still exists and still has the pin
    }
    if (s.rmtEnabled) {
        ++s.errorLogs;
        return ESP_ERR_INVALID_STATE;  // IDF: a channel is deleted only once disabled
    }
    s.rmtChannel = false;
    s.rmtEnabled = false;
    s.rmtBusy = false;
    s.doneCallback = nullptr;
    s.doneContext = nullptr;
    if (s.owner == Owner::Rmt) {
        s.owner = Owner::None;  // the RMT lets the pin go; nobody routes it until uart_set_pin
    }
    return ESP_OK;
}

esp_err_t rmt_new_simple_encoder(const rmt_simple_encoder_config_t* config, rmt_encoder_handle_t* ret)
{
    auto& s = fake_idf::state();
    s.encoderCallback = reinterpret_cast<void*>(config->callback);
    s.encoderArg = config->arg;
    *ret = reinterpret_cast<rmt_encoder_handle_t>(&fake_idf::g_encoder);
    return ESP_OK;
}

esp_err_t rmt_del_encoder(rmt_encoder_handle_t)
{
    auto& s = fake_idf::state();
    ++s.rmtDelEncoderCalls;
    s.encoderCallback = nullptr;
    s.encoderArg = nullptr;
    return s.failRmtDelEncoder ? ESP_FAIL : ESP_OK;
}

esp_err_t rmt_enable(rmt_channel_handle_t)
{
    auto& s = fake_idf::state();
    if (s.failRmtEnable) {
        return ESP_FAIL;
    }
    s.rmtEnabled = true;
    return ESP_OK;
}

esp_err_t rmt_disable(rmt_channel_handle_t)
{
    auto& s = fake_idf::state();
    if (!s.rmtEnabled) {
        ++s.rmtDisableRefused;  // IDF: "channel can't be disabled in state 0"
        ++s.errorLogs;
        return ESP_ERR_INVALID_STATE;
    }
    if (s.failRmtDisable) {
        ++s.errorLogs;
        return ESP_FAIL;  // nothing changes: still enabled, a run in flight goes on
    }
    s.rmtEnabled = false;
    s.rmtBusy = false;  // aborts the transmission in flight
    return ESP_OK;
}

esp_err_t rmt_transmit(rmt_channel_handle_t, rmt_encoder_handle_t, const void*, size_t,
                       const rmt_transmit_config_t* config)
{
    auto& s = fake_idf::state();
    if (!s.rmtEnabled) {
        return ESP_FAIL;
    }
    ++s.rmtTransmits;
    s.eotLevel = config->flags.eot_level;
    s.rmtBusy = true;
    return ESP_OK;
}

esp_err_t rmt_tx_wait_all_done(rmt_channel_handle_t, int)
{
    auto& s = fake_idf::state();
    if (!s.rmtBusy) {
        return ESP_OK;
    }
    ++s.rmtWaitTimeouts;  // IDF: "rmt_tx_wait_all_done(588): flush timeout", logged as an error
    ++s.errorLogs;
    return ESP_ERR_TIMEOUT;
}

esp_err_t rmt_tx_register_event_callbacks(rmt_channel_handle_t, const rmt_tx_event_callbacks_t* cbs, void* user_data)
{
    auto& s = fake_idf::state();
    if (!s.rmtChannel || s.rmtEnabled) {
        ++s.errorLogs;
        return ESP_ERR_INVALID_STATE;  // IDF: only while the channel is in the init state
    }
    s.doneCallback = reinterpret_cast<void*>(cbs->on_trans_done);
    s.doneContext = user_data;
    return ESP_OK;
}

// --- SIO line (menu-sio-fisico T4) ----------------------------------------------------------

esp_err_t gpio_set_direction(gpio_num_t gpio_num, gpio_mode_t mode)
{
    if (mode == GPIO_MODE_INPUT) {
        fake_idf::state().inputs.push_back(gpio_num);
    }
    return ESP_OK;
}

esp_err_t gpio_pullup_en(gpio_num_t gpio_num)
{
    fake_idf::state().pullups.push_back(gpio_num);
    return ESP_OK;
}

int gpio_get_level(gpio_num_t gpio_num)
{
    const auto& s = fake_idf::state();
    if (gpio_num == s.commandPin) {
        return s.nowUs >= s.commandLowFromUs && s.nowUs < s.commandLowUntilUs ? 0 : 1;
    }
    return 1;  // DATA OUT and the rest idle at mark
}

namespace {

// Moves the scheduled bytes that have arrived by now into the receive buffer.
void deliver(fake_idf::State& s)
{
    std::size_t i = 0;
    while (i < s.rxAt.size() && s.rxAt[i].first <= s.nowUs) {
        s.rx.push_back(s.rxAt[i].second);
        ++i;
    }
    s.rxAt.erase(s.rxAt.begin(), s.rxAt.begin() + static_cast<std::ptrdiff_t>(i));
}

}  // namespace

int uart_read_bytes(uart_port_t, void* buf, uint32_t length, uint32_t ticks)
{
    auto& s = fake_idf::state();
    if (s.failUartRead) {
        return -1;
    }
    if (!s.rxAt.empty()) {  // bytes on a timeline: wait for them until the deadline
        const int64_t deadline = s.nowUs + static_cast<int64_t>(ticks) * 1000;
        deliver(s);
        while (s.rx.size() < length && !s.rxAt.empty() && s.rxAt.front().first <= deadline) {
            s.nowUs = s.rxAt.front().first;
            deliver(s);
        }
        if (s.rx.size() < length) {
            s.nowUs = deadline;
        }
        const std::size_t n = s.rx.size() < length ? s.rx.size() : length;
        auto* out = static_cast<uint8_t*>(buf);
        for (std::size_t i = 0; i < n; ++i) {
            out[i] = s.rx[i];
        }
        s.rx.erase(s.rx.begin(), s.rx.begin() + static_cast<std::ptrdiff_t>(n));
        return static_cast<int>(n);
    }
    const std::size_t n = s.rx.size() < length ? s.rx.size() : length;
    auto* out = static_cast<uint8_t*>(buf);
    for (std::size_t i = 0; i < n; ++i) {
        out[i] = s.rx[i];
    }
    s.rx.erase(s.rx.begin(), s.rx.begin() + static_cast<std::ptrdiff_t>(n));
    // Fewer bytes than asked: the read waited its whole timeout.
    s.nowUs += n == length ? static_cast<int64_t>((n * 10u * 1000000u + 19199u) / 19200u)
                           : static_cast<int64_t>(ticks) * 1000;
    return static_cast<int>(n);
}

esp_err_t uart_flush_input(uart_port_t)
{
    auto& s = fake_idf::state();
    deliver(s);
    ++s.uartFlushes;
    if (s.failUartFlush) {
        return ESP_FAIL;
    }
    s.rx.clear();
    return ESP_OK;
}

int64_t esp_timer_get_time() { return fake_idf::state().nowUs; }

void esp_rom_delay_us(uint32_t us) { fake_idf::state().nowUs += us; }

void vTaskDelay(TickType_t ticks)
{
    auto& s = fake_idf::state();
    s.nowUs += static_cast<int64_t>(ticks) * 1000;
    ++s.taskDelays;
    if (s.onDelay != nullptr) {
        s.onDelay();
    }
}
