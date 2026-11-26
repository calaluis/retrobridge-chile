#pragma once
// Host stand-in for the FreeRTOS types the SIO line adapter uses (menu-sio-fisico T4).
#include <cstdint>
typedef uint32_t TickType_t;
#define portMAX_DELAY 0xFFFFFFFFu
#define pdMS_TO_TICKS(ms) (static_cast<TickType_t>(ms))  // CONFIG_FREERTOS_HZ=1000
// cassette-motor-fujinet: what the owner's cassetteFSK.cpp and cassetteFSKLoader.cpp use.
typedef int BaseType_t;
typedef unsigned UBaseType_t;
#define pdPASS 1
#define IRAM_ATTR
struct portMUX_TYPE { int unused; };
#define portMUX_INITIALIZER_UNLOCKED {0}
#define portENTER_CRITICAL(mux) ((void)(mux))
#define portEXIT_CRITICAL(mux) ((void)(mux))
