// RetroBridge adapter (written for RetroBridge, not taken from FujiNet): PIN_UART2_TX, the SIO DATA IN
// pin. RetroBridge reads it from the board profile at run time, so it is an expression, not a number.
#pragma once

namespace rb::fujinet {
int uart2TxPin() noexcept;          // -1 until set
void setUart2TxPin(int pin) noexcept;
}  // namespace rb::fujinet

#define PIN_UART2_TX (rb::fujinet::uart2TxPin())
