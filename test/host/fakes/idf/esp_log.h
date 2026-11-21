#pragma once
// Host stand-in for ESP-IDF logging: errors are counted (fake_idf.hpp), nothing is printed. The
// arguments are checked against the format, as the real macros do.
#include "esp_err.h"
void fake_idf_log(char level, const char* tag, const char* format, ...) __attribute__((format(printf, 3, 4)));
#define ESP_LOGE(tag, format, ...) fake_idf_log('E', tag, format, ##__VA_ARGS__)
#define ESP_LOGW(tag, format, ...) fake_idf_log('W', tag, format, ##__VA_ARGS__)
#define ESP_LOGI(tag, format, ...) fake_idf_log('I', tag, format, ##__VA_ARGS__)
