#pragma once
// Host stand-in for ESP-IDF's hal/uart_ll.h (component esp_hal_uart; see fake_idf.hpp): only the TX
// FIFO read that EspDataIn's shared mode makes (menu-cinta T9a.2, decision 3).
#include <cstdint>

struct uart_dev_t {
    int num;
};
uart_dev_t* fake_uart_hw(int num);
#define UART_LL_GET_HW(num) fake_uart_hw(num)
// As ESP-IDF 6.1 (esp32): the free room in the TX FIFO, 128 - bytes waiting in it.
uint32_t uart_ll_get_txfifo_len(uart_dev_t* hw);
