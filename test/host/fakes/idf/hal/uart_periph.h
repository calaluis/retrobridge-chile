#pragma once
// Host stand-in (cassette-motor-fujinet): ESP-IDF 6.1 hal/uart_periph.h, the UART TX signal of each UART.
#include <cstdint>
typedef enum {
    SOC_UART_PERIPH_SIGNAL_TX = 0,
    SOC_UART_PERIPH_SIGNAL_RX = 1,
    SOC_UART_PERIPH_SIGNAL_MAX = 6,
} soc_uart_periph_signal_t;
typedef struct {
    struct {
        uint32_t signal;
    } pins[SOC_UART_PERIPH_SIGNAL_MAX];
} uart_signal_conn_t;
extern const uart_signal_conn_t uart_periph_signal[3];
