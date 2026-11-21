#pragma once
// Host stand-in for ESP-IDF's driver/uart.h (see fake_idf.hpp).
#include <cstddef>
#include <cstdint>
#include "esp_err.h"
typedef int uart_port_t;
#define UART_NUM_2 2
#define UART_PIN_NO_CHANGE (-1)
enum uart_word_length_t { UART_DATA_8_BITS = 3 };
enum uart_parity_t { UART_PARITY_DISABLE = 0 };
enum uart_stop_bits_t { UART_STOP_BITS_1 = 1 };
enum uart_hw_flowcontrol_t { UART_HW_FLOWCTRL_DISABLE = 0 };
enum uart_sclk_t { UART_SCLK_DEFAULT = 0 };
struct uart_config_t {
    int baud_rate;
    uart_word_length_t data_bits;
    uart_parity_t parity;
    uart_stop_bits_t stop_bits;
    uart_hw_flowcontrol_t flow_ctrl;
    uart_sclk_t source_clk;
};
typedef void* QueueHandle_t;
esp_err_t uart_driver_install(uart_port_t port, int rx, int tx, int queue_size, QueueHandle_t* queue, int flags);
esp_err_t uart_driver_delete(uart_port_t port);
esp_err_t uart_param_config(uart_port_t port, const uart_config_t* config);
esp_err_t uart_set_pin(uart_port_t port, int tx, int rx, int rts, int cts);
esp_err_t uart_wait_tx_done(uart_port_t port, uint32_t ticks);
esp_err_t uart_set_baudrate(uart_port_t port, uint32_t baud);
int uart_write_bytes(uart_port_t port, const void* src, size_t size);

// SIO line adapter (menu-sio-fisico T4): reception.
int uart_read_bytes(uart_port_t port, void* buf, uint32_t length, uint32_t ticks);
esp_err_t uart_flush_input(uart_port_t port);
