#pragma once
// Host stand-in for ESP-IDF's esp_err.h: only what rb_esp_cassette's DATA IN driver uses.
typedef int esp_err_t;
#define ESP_OK 0
#define ESP_FAIL -1
#define ESP_ERR_INVALID_STATE 0x103
#define ESP_ERR_TIMEOUT 0x107
// cassette-motor-fujinet: the owner's cassetteFSK.cpp logs and checks with these.
const char* esp_err_to_name(esp_err_t code);
void fake_idf_error_check_failed(esp_err_t code);
#define ESP_ERROR_CHECK(x) do { const esp_err_t rc_ = (x); if (rc_ != ESP_OK) fake_idf_error_check_failed(rc_); } while (0)
