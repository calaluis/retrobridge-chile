#pragma once
// Host stand-in (cassette-motor-fujinet, fake_idf_fsk.cpp): routes an output signal to the DATA IN pin in
// fake_idf's GPIO matrix (outSel; 256 = plain GPIO, U2TXD = the UART).
#include <cstdint>

#include "soc/gpio_sig_map.h"  // SIG_GPIO_OUT_IDX: on the ESP32 it reaches the caller through the driver headers
void esp_rom_gpio_connect_out_signal(uint32_t gpio_num, uint32_t signal_idx, bool out_inv, bool oen_inv);
