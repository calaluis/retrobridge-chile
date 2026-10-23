#include "rb/esp/esp_clock.hpp"

#include "esp_rom_sys.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

namespace rb::esp {

uint64_t EspClock::nowMicros() const
{
    return static_cast<uint64_t>(esp_timer_get_time());
}

void EspClock::waitUntil(uint64_t micros)
{
    for (;;) {
        const uint64_t now = nowMicros();
        if (now >= micros) {
            return;
        }
        const uint64_t remaining = micros - now;
        if (remaining > kBusyWaitThresholdUs) {
            const uint64_t sleepMs = (remaining - kBusyWaitThresholdUs) / 1000;
            TickType_t ticks = pdMS_TO_TICKS(static_cast<uint32_t>(sleepMs));
            vTaskDelay(ticks > 0 ? ticks : 1);
        } else {
            esp_rom_delay_us(static_cast<uint32_t>(remaining));
        }
    }
}

}  // namespace rb::esp
