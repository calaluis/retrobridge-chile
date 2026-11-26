// RetroBridge adapter (written for RetroBridge): ESP-IDF 6.1 moved soc/uart_periph.h to hal/uart_periph.h
// and renamed SOC_UART_TX_PIN_IDX to SOC_UART_PERIPH_SIGNAL_TX. The owner's cassetteFSK.cpp keeps its
// include; this header gives it the IDF 6.1 names.
#pragma once

#include "hal/uart_periph.h"

#ifndef SOC_UART_TX_PIN_IDX
#define SOC_UART_TX_PIN_IDX SOC_UART_PERIPH_SIGNAL_TX
#endif
