#pragma once
// Host stand-in (cassette-motor-fujinet): the classic ESP32's RMT, as the owner's cassetteFSK.cpp reads it
// (on ESP-IDF 6.1 the first two come from adapt/rb_fujinet_idf6.h).
#define SOC_RMT_SUPPORT_TX_ASYNC_STOP 0
#define SOC_RMT_CHANNELS_PER_GROUP 8
#define SOC_RMT_MEM_WORDS_PER_CHANNEL 64
