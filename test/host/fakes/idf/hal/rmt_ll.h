#pragma once
// Host stand-in (cassette-motor-fujinet, fake_idf_fsk.cpp): the RMT registers the owner's cassetteFSK.cpp
// touches when MOTOR drops (mem blocks of channel 0, read pointer reset).
#include <cstdint>
struct rmt_dev_t {
    int unused;
};
extern rmt_dev_t RMT;
uint32_t rmt_ll_tx_get_mem_blocks(rmt_dev_t* dev, uint32_t channel);
void rmt_ll_tx_reset_pointer(rmt_dev_t* dev, uint32_t channel);
