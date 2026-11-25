#include "rb/esp/esp_sio_uart.hpp"

#include "driver/gpio.h"
#include "driver/uart.h"
#include "esp_log.h"
#include "esp_rom_sys.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

namespace rb::esp {

namespace {

constexpr const char* kTag = "rb_sio_line";
constexpr uart_port_t kUart = UART_NUM_2;
constexpr uint32_t kFinePollUs = 50;
constexpr uint32_t kBaudTolerancePct = 2;  // the ESP32's divisor does not give 19200 exactly

uint32_t ticksFor(uint32_t timeoutUs) noexcept { return pdMS_TO_TICKS((timeoutUs + 999u) / 1000u); }

}  // namespace

bool EspSioUart::begin() noexcept
{
    if (ready_) {
        return true;
    }
    uart_config_t config{};
    config.baud_rate = static_cast<int>(rb::sio::SioLineTransport::kBaud);
    config.data_bits = UART_DATA_8_BITS;
    config.parity = UART_PARITY_DISABLE;
    config.stop_bits = UART_STOP_BITS_1;
    config.flow_ctrl = UART_HW_FLOWCTRL_DISABLE;
    config.source_clk = UART_SCLK_DEFAULT;
    if (gpio_set_direction(static_cast<gpio_num_t>(pins_.command), GPIO_MODE_INPUT) != ESP_OK) {
        ESP_LOGE(kTag, "COMMAND (GPIO%d) not set as input", pins_.command);
        return false;
    }
    if (uart_driver_install(kUart, kUartRxBuffer, txRing_, 0, nullptr, 0) != ESP_OK) {
        ESP_LOGE(kTag, "UART2 driver not installed: SIO line off");
        return false;
    }
    // Each received byte goes to the driver at once. With the defaults (120 bytes, or 10 idle characters,
    // ~5.2 ms at 19200) a 5-byte command frame reached uart_read_bytes after kFrameReadUs: every frame was
    // short and the 65XE never got an answer (menu-sio bench on the 65XE, 2026-11-21: 168 short, 0 frames).
    if (uart_param_config(kUart, &config) != ESP_OK || uart_set_rx_full_threshold(kUart, 1) != ESP_OK ||
        uart_set_rx_timeout(kUart, 1) != ESP_OK ||
        uart_set_pin(kUart, pins_.dataIn, pins_.dataOut, UART_PIN_NO_CHANGE, UART_PIN_NO_CHANGE) != ESP_OK ||
        gpio_pullup_en(static_cast<gpio_num_t>(pins_.dataOut)) != ESP_OK) {
        (void)uart_driver_delete(kUart);
        ESP_LOGE(kTag, "UART2 not set on GPIO%d/GPIO%d: SIO line off", pins_.dataIn, pins_.dataOut);
        return false;
    }
    ready_ = true;
    ESP_LOGI(kTag, "SIO line: UART2 19200 TX GPIO%d RX GPIO%d, COMMAND GPIO%d", pins_.dataIn, pins_.dataOut,
             pins_.command);
    return true;
}

void EspSioUart::end() noexcept
{
    if (ready_) {
        (void)uart_driver_delete(kUart);
        ready_ = false;
        parked_ = false;
    }
}

bool EspSioUart::park() noexcept
{
    // Plan §2.4 step 1: the disk's last answer must be out before the cassette may drive GPIO21.
    if (!ready_ || uart_wait_tx_done(kUart, 0) != ESP_OK) {
        return false;
    }
    parked_ = true;
    return true;
}

bool EspSioUart::resume() noexcept
{
    if (!ready_) {
        return false;
    }
    // Plan «Reanudar el disco»: the cassette gave GPIO21 back; the line must be the disk's again.
    const uint32_t baud = rb::sio::SioLineTransport::kBaud;
    uint32_t actual = 0;
    if (uart_set_baudrate(kUart, baud) != ESP_OK || uart_get_baudrate(kUart, &actual) != ESP_OK ||
        actual * 100u < baud * (100u - kBaudTolerancePct) || actual * 100u > baud * (100u + kBaudTolerancePct)) {
        return false;
    }
    uart_word_length_t bits{};
    uart_parity_t parity{};
    uart_stop_bits_t stop{};
    if (uart_get_word_length(kUart, &bits) != ESP_OK || bits != UART_DATA_8_BITS ||
        uart_get_parity(kUart, &parity) != ESP_OK || parity != UART_PARITY_DISABLE ||
        uart_get_stop_bits(kUart, &stop) != ESP_OK || stop != UART_STOP_BITS_1) {
        return false;
    }
    if (!flushed()) {  // what came in at the tape's rate is garbage for the disk (review of PR #107)
        return false;
    }
    parked_ = false;
    return true;
}

bool EspSioUart::commandHigh() const noexcept { return gpio_get_level(static_cast<gpio_num_t>(pins_.command)) != 0; }

bool EspSioUart::dataOutHigh() const noexcept { return gpio_get_level(static_cast<gpio_num_t>(pins_.dataOut)) != 0; }

bool EspSioUart::waitCommandLevel(bool asserted, uint32_t timeoutUs)
{
    const int64_t start = esp_timer_get_time();
    for (;;) {
        if (commandHigh() != asserted) {
            return true;
        }
        const int64_t elapsed = esp_timer_get_time() - start;
        if (elapsed >= static_cast<int64_t>(timeoutUs)) {
            return false;
        }
        const int64_t remaining = static_cast<int64_t>(timeoutUs) - elapsed;
        if (remaining >= 1000) {
            vTaskDelay(1);  // a tick: other tasks (and the idle task's watchdog) run
        } else {
            esp_rom_delay_us(remaining < kFinePollUs ? static_cast<uint32_t>(remaining) : kFinePollUs);
        }
    }
}

rb::core::Result<std::size_t> EspSioUart::read(rb::core::Span<uint8_t> out, uint32_t timeoutUs)
{
    if (!ready_) {
        return rb::core::Failure{rb::core::ErrorCode::TransportError};
    }
    if (parked_) {  // the tape owns DATA IN: no frame is the disk's; wait as a read would, take nothing
        vTaskDelay(ticksFor(timeoutUs));
        return std::size_t{0};
    }
    const int n = uart_read_bytes(kUart, out.data(), static_cast<uint32_t>(out.size()), ticksFor(timeoutUs));
    if (n < 0) {
        return rb::core::Failure{rb::core::ErrorCode::TransportError};
    }
    return static_cast<std::size_t>(n);
}

rb::core::Result<void> EspSioUart::write(rb::core::Span<const uint8_t> bytes)
{
    if (!ready_ || parked_) {  // parked: never transmit over the tape
        return rb::core::Failure{rb::core::ErrorCode::TransportError};
    }
    const int n = uart_write_bytes(kUart, bytes.data(), bytes.size());
    const uint32_t lineMs = (rb::sio::SioLineTransport::byteTimeUs(bytes.size()) + 999u) / 1000u;
    if (n != static_cast<int>(bytes.size()) ||
        uart_wait_tx_done(kUart, pdMS_TO_TICKS(lineMs + kTxDoneMarginMs)) != ESP_OK) {
        return rb::core::Failure{rb::core::ErrorCode::TransportError};
    }
    return {};
}

void EspSioUart::flushInput() { (void)flushed(); }

bool EspSioUart::flushed() noexcept { return ready_ && uart_flush_input(kUart) == ESP_OK; }

void EspSioUart::idleLine()
{
    // The UART rests at mark after the last stop bit: DATA IN is released once
    // nothing is left to send.
    if (ready_ && !parked_) {  // parked: the bytes going out are the tape's
        (void)uart_wait_tx_done(kUart, pdMS_TO_TICKS(kTxDoneMarginMs));
    }
}

}  // namespace rb::esp
