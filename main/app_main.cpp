// RetroBridge Chile — composition root.
//
// H0 skeleton: only proves that the pinned toolchain builds the component graph.
// It deliberately configures no GPIO, UART or SD peripheral (hardware gate H4).

#include "esp_log.h"
#include "rb/core/toolchain_checks.hpp"

namespace {
constexpr const char* kTag = "retrobridge";
}

extern "C" void app_main(void)
{
    ESP_LOGI(kTag, "RetroBridge Chile H0 skeleton");
}
