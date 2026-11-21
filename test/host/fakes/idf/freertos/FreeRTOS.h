#pragma once
// Host stand-in for the FreeRTOS types the SIO line adapter uses (menu-sio-fisico T4).
#include <cstdint>
typedef uint32_t TickType_t;
#define portMAX_DELAY 0xFFFFFFFFu
#define pdMS_TO_TICKS(ms) (static_cast<TickType_t>(ms))  // CONFIG_FREERTOS_HZ=1000
