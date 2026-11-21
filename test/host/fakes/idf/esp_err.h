#pragma once
// Host stand-in for ESP-IDF's esp_err.h: only what rb_esp_cassette's DATA IN driver uses.
typedef int esp_err_t;
#define ESP_OK 0
#define ESP_FAIL -1
#define ESP_ERR_TIMEOUT 0x107
