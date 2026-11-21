#pragma once
// Host stand-in for ESP-IDF's driver/rmt_encoder.h (see fake_idf.hpp).
#include <cstddef>
#include <cstdint>
#include "esp_err.h"
struct rmt_symbol_word_t {
    uint16_t duration0 : 15;
    uint16_t level0 : 1;
    uint16_t duration1 : 15;
    uint16_t level1 : 1;
};
typedef struct fake_rmt_encoder* rmt_encoder_handle_t;
typedef size_t (*rmt_encode_simple_cb_t)(const void* data, size_t data_size, size_t symbols_written,
                                         size_t symbols_free, rmt_symbol_word_t* symbols, bool* done, void* arg);
struct rmt_simple_encoder_config_t {
    rmt_encode_simple_cb_t callback;
    void* arg;
    size_t min_chunk_size;
};
esp_err_t rmt_new_simple_encoder(const rmt_simple_encoder_config_t* config, rmt_encoder_handle_t* ret);
esp_err_t rmt_del_encoder(rmt_encoder_handle_t encoder);
