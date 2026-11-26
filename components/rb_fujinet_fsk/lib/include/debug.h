// RetroBridge adapter (written for RetroBridge, not taken from FujiNet): Debug_printf and Debug_println
// for the owner's FSK code. On the ESP32 they go to the log under the tag "fsk"; on the host, nowhere.
#pragma once

#ifdef ESP_PLATFORM
#include "esp_log.h"
#define Debug_printf(...) ESP_LOGI("fsk", __VA_ARGS__)
#define Debug_println(text) ESP_LOGI("fsk", "%s", text)
#else
#define Debug_printf(...) ((void)0)
#define Debug_println(text) ((void)(text))
#endif
