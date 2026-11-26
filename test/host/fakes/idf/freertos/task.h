#pragma once
#include "freertos/FreeRTOS.h"
// Advances the fake time by `ticks` ms (CONFIG_FREERTOS_HZ=1000).
void vTaskDelay(TickType_t ticks);
// cassette-motor-fujinet: a task is a host thread (the owner's FSK loader). In it, vTaskDelay and
// taskYIELD only yield; the model time (nowUs) belongs to the test thread.
typedef void* TaskHandle_t;
typedef void (*TaskFunction_t)(void*);
BaseType_t xTaskCreatePinnedToCore(TaskFunction_t fn, const char* name, uint32_t stack, void* arg,
                                   UBaseType_t priority, TaskHandle_t* handle, BaseType_t core);
void vTaskDelete(TaskHandle_t task);
void taskYIELD();
