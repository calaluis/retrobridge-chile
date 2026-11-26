// RetroBridge adapter (written for RetroBridge): force-included in the owner's cassetteFSK.cpp on ESP-IDF
// 6.1 only. IDF 6.1 dropped three names that file uses; the values are those of the classic ESP32:
// - SOC_RMT_SUPPORT_TX_ASYNC_STOP: no asynchronous TX stop on the classic ESP32 (0).
// - SOC_RMT_CHANNELS_PER_GROUP: 8 channels (RMT_LL_CHANS_PER_INST in IDF 6.1 hal/rmt_ll.h).
// - rmt_ll_tx_get_mem_blocks(): the counterpart of IDF 6.1 rmt_ll_tx_set_mem_blocks() (conf0.mem_size).
#pragma once

#include "hal/rmt_ll.h"

#ifndef SOC_RMT_SUPPORT_TX_ASYNC_STOP
#define SOC_RMT_SUPPORT_TX_ASYNC_STOP 0
#endif
#ifndef SOC_RMT_CHANNELS_PER_GROUP
#define SOC_RMT_CHANNELS_PER_GROUP RMT_LL_CHANS_PER_INST
#endif

static inline uint32_t rmt_ll_tx_get_mem_blocks(rmt_dev_t* dev, uint32_t channel)
{
    return dev->conf_ch[channel].conf0.mem_size;
}
