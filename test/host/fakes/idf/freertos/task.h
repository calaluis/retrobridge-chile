#pragma once
#include "freertos/FreeRTOS.h"
// Advances the fake time by `ticks` ms (CONFIG_FREERTOS_HZ=1000).
void vTaskDelay(TickType_t ticks);
