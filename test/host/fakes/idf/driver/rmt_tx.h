#pragma once
// Host stand-in for ESP-IDF's driver/rmt_tx.h (see fake_idf.hpp).
#include <cstddef>
#include <cstdint>
#include "driver/gpio.h"
#include "driver/rmt_encoder.h"
#include "esp_err.h"
typedef struct fake_rmt_channel* rmt_channel_handle_t;
enum rmt_clock_source_t { RMT_CLK_SRC_DEFAULT = 0 };
struct rmt_tx_channel_config_t {
    gpio_num_t gpio_num;
    rmt_clock_source_t clk_src;
    uint32_t resolution_hz;
    size_t mem_block_symbols;
    size_t trans_queue_depth;
    struct {
        uint32_t init_level : 1;
    } flags;
};
struct rmt_transmit_config_t {
    int loop_count;
    struct {
        uint32_t eot_level : 1;
    } flags;
};
esp_err_t rmt_new_tx_channel(const rmt_tx_channel_config_t* config, rmt_channel_handle_t* ret);
esp_err_t rmt_del_channel(rmt_channel_handle_t channel);
esp_err_t rmt_enable(rmt_channel_handle_t channel);
esp_err_t rmt_disable(rmt_channel_handle_t channel);
esp_err_t rmt_transmit(rmt_channel_handle_t channel, rmt_encoder_handle_t encoder, const void* payload,
                       size_t payload_bytes, const rmt_transmit_config_t* config);
esp_err_t rmt_tx_wait_all_done(rmt_channel_handle_t channel, int timeout_ms);
struct rmt_tx_done_event_data_t {
    size_t num_symbols;
};
typedef bool (*rmt_tx_done_callback_t)(rmt_channel_handle_t tx_chan, const rmt_tx_done_event_data_t* edata,
                                       void* user_ctx);
struct rmt_tx_event_callbacks_t {
    rmt_tx_done_callback_t on_trans_done;
};
esp_err_t rmt_tx_register_event_callbacks(rmt_channel_handle_t tx_channel, const rmt_tx_event_callbacks_t* cbs,
                                          void* user_data);
