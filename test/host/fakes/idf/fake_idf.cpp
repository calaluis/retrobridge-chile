#include "fake_idf.hpp"

#include "driver/gpio.h"
#include "driver/rmt_encoder.h"
#include "driver/rmt_tx.h"
#include "driver/uart.h"
#include "esp_log.h"

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
    }
    return ESP_OK;
}

esp_err_t uart_driver_install(uart_port_t, int, int, int, QueueHandle_t*, int)
{
    fake_idf::state().uartInstalled = true;
    return ESP_OK;
}

esp_err_t uart_driver_delete(uart_port_t)
{
    fake_idf::state().uartInstalled = false;
    return ESP_OK;
}

esp_err_t uart_param_config(uart_port_t, const uart_config_t*) { return ESP_OK; }

esp_err_t uart_set_pin(uart_port_t, int tx, int, int, int)
{
    auto& s = fake_idf::state();
    ++s.uartSetPinCalls;
    if (s.failUartSetPin) {
        return ESP_FAIL;  // the matrix is left as it was
    }
    s.pin = tx;
    s.owner = Owner::Uart;
    return ESP_OK;
}

esp_err_t uart_wait_tx_done(uart_port_t, uint32_t) { return ESP_OK; }

esp_err_t uart_set_baudrate(uart_port_t, uint32_t) { return ESP_OK; }

int uart_write_bytes(uart_port_t, const void* src, size_t size)
{
    auto& s = fake_idf::state();
    const auto* bytes = static_cast<const uint8_t*>(src);
    s.written.insert(s.written.end(), bytes, bytes + size);
    return static_cast<int>(size);
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
    s.initLevel = config->flags.init_level;
    *ret = reinterpret_cast<rmt_channel_handle_t>(&fake_idf::g_channel);
    return ESP_OK;
}

esp_err_t rmt_del_channel(rmt_channel_handle_t)
{
    auto& s = fake_idf::state();
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
    s.encoderCallback = nullptr;
    s.encoderArg = nullptr;
    return ESP_OK;
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
