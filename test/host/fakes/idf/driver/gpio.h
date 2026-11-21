#pragma once
// Host stand-in for ESP-IDF's driver/gpio.h (see fake_idf.hpp).
#include "esp_err.h"
typedef int gpio_num_t;
esp_err_t gpio_reset_pin(gpio_num_t gpio_num);
