#pragma once
// Host stand-in for ESP-IDF's driver/gpio.h (see fake_idf.hpp).
#include <cstdint>

#include "esp_err.h"
typedef int gpio_num_t;
esp_err_t gpio_reset_pin(gpio_num_t gpio_num);

// SIO line adapter (menu-sio-fisico T4): inputs only.
enum gpio_mode_t { GPIO_MODE_DISABLE = 0, GPIO_MODE_INPUT = 1 };
esp_err_t gpio_set_direction(gpio_num_t gpio_num, gpio_mode_t mode);
esp_err_t gpio_pullup_en(gpio_num_t gpio_num);
int gpio_get_level(gpio_num_t gpio_num);
esp_err_t gpio_set_level(gpio_num_t gpio_num, uint32_t level);  // cassette-motor-fujinet (fake_idf_fsk.cpp)
