#include "wifi_station.hpp"

#include <atomic>
#include <cstdio>
#include <cstring>

#include "esp_event.h"
#include "esp_log.h"
#include "esp_netif.h"
#include "esp_timer.h"
#include "esp_wifi.h"
#include "freertos/FreeRTOS.h"
#include "freertos/event_groups.h"
#include "freertos/task.h"

namespace rb::menusio {

namespace {

constexpr const char* kTag = "rb_wifi";
constexpr TickType_t kConnectWait = pdMS_TO_TICKS(20000);
constexpr TickType_t kRetryPause = pdMS_TO_TICKS(5000);
constexpr EventBits_t kGotIp = BIT0;
constexpr EventBits_t kDisconnected = BIT1;

std::atomic<int> g_state{static_cast<int>(WifiState::Connecting)};
EventGroupHandle_t g_events = nullptr;

void setState(WifiState state) noexcept { g_state.store(static_cast<int>(state), std::memory_order_relaxed); }

long long msNow() { return static_cast<long long>(esp_timer_get_time() / 1000); }

// Line 1: network name (1..32 bytes), line 2: key (empty or 8..63 bytes). The key is copied straight
// into `config` and the local copy is wiped; only its length is ever logged.
bool readWifiFile(const char* path, wifi_config_t& config)
{
    std::FILE* file = std::fopen(path, "rb");
    if (file == nullptr) {
        ESP_LOGW(kTag, "Wi-Fi off: no %s", path);
        setState(WifiState::NoFile);
        return false;
    }
    char lines[2][80] = {};
    for (auto& line : lines) {
        if (std::fgets(line, sizeof line, file) == nullptr) {
            line[0] = '\0';
        }
        line[std::strcspn(line, "\r\n")] = '\0';
    }
    std::fclose(file);
    const std::size_t ssid = std::strlen(lines[0]);
    const std::size_t key = std::strlen(lines[1]);
    bool ok = false;
    if (ssid == 0 || ssid > sizeof config.sta.ssid) {
        ESP_LOGW(kTag, "Wi-Fi off: network name of %u bytes in %s (1..32)", static_cast<unsigned>(ssid), path);
    } else if (key != 0 && (key < 8 || key > 63)) {
        ESP_LOGW(kTag, "Wi-Fi off: key of %u bytes in %s (empty or 8..63)", static_cast<unsigned>(key), path);
    } else {
        std::memcpy(config.sta.ssid, lines[0], ssid);
        std::memcpy(config.sta.password, lines[1], key);
        config.sta.threshold.authmode = key == 0 ? WIFI_AUTH_OPEN : WIFI_AUTH_WPA2_PSK;
        ESP_LOGI(kTag, "Wi-Fi: network '%s', key of %u bytes", lines[0], static_cast<unsigned>(key));
        ok = true;
    }
    std::memset(lines, 0, sizeof lines);  // the key stays only in the driver's RAM copy
    if (!ok) {
        setState(WifiState::BadFile);
    }
    return ok;
}

void onEvent(void*, esp_event_base_t base, int32_t id, void* data)
{
    if (base == WIFI_EVENT && id == WIFI_EVENT_STA_DISCONNECTED) {
        xEventGroupClearBits(g_events, kGotIp);
        xEventGroupSetBits(g_events, kDisconnected);
    } else if (base == IP_EVENT && id == IP_EVENT_STA_GOT_IP) {
        const auto* got = static_cast<const ip_event_got_ip_t*>(data);
        ESP_LOGI(kTag, "Wi-Fi: IP " IPSTR, IP2STR(&got->ip_info.ip));
        xEventGroupClearBits(g_events, kDisconnected);
        xEventGroupSetBits(g_events, kGotIp);
    }
}

bool check(const char* call, esp_err_t err)
{
    if (err != ESP_OK) {
        ESP_LOGE(kTag, "Wi-Fi off: %s -> %s", call, esp_err_to_name(err));
        return false;
    }
    return true;
}

// The default event loop; one that already exists is used as it is.
esp_err_t eventLoop()
{
    const esp_err_t err = esp_event_loop_create_default();
    return err == ESP_ERR_INVALID_STATE ? ESP_OK : err;
}

}  // namespace

WifiState wifiState() noexcept { return static_cast<WifiState>(g_state.load(std::memory_order_relaxed)); }

void runWifiStation(const char* path) noexcept
{
    wifi_config_t config{};
    if (!readWifiFile(path, config)) {
        return;
    }
    esp_log_level_set("wifi", ESP_LOG_WARN);  // its INFO lines print the MAC
    esp_log_level_set("wifi_init", ESP_LOG_WARN);
    esp_log_level_set("phy_init", ESP_LOG_WARN);
    g_events = xEventGroupCreate();
    wifi_init_config_t init = WIFI_INIT_CONFIG_DEFAULT();
    const bool ready = g_events != nullptr && check("esp_netif_init", esp_netif_init()) &&
                       check("esp_event_loop_create_default", eventLoop()) &&
                       esp_netif_create_default_wifi_sta() != nullptr && check("esp_wifi_init", esp_wifi_init(&init)) &&
                       check("esp_wifi_set_storage", esp_wifi_set_storage(WIFI_STORAGE_RAM)) &&
                       check("handler", esp_event_handler_register(WIFI_EVENT, WIFI_EVENT_STA_DISCONNECTED, onEvent,
                                                                   nullptr)) &&
                       check("handler", esp_event_handler_register(IP_EVENT, IP_EVENT_STA_GOT_IP, onEvent, nullptr)) &&
                       check("esp_wifi_set_mode", esp_wifi_set_mode(WIFI_MODE_STA)) &&
                       check("esp_wifi_set_config", esp_wifi_set_config(WIFI_IF_STA, &config)) &&
                       check("esp_wifi_start", esp_wifi_start());
    std::memset(&config, 0, sizeof config);
    if (!ready) {
        setState(WifiState::Failed);
        return;
    }
    for (;;) {
        setState(WifiState::Connecting);
        xEventGroupClearBits(g_events, kGotIp | kDisconnected);
        const long long from = msNow();
        if (esp_wifi_connect() == ESP_OK &&
            (xEventGroupWaitBits(g_events, kGotIp | kDisconnected, pdFALSE, pdFALSE, kConnectWait) & kGotIp) != 0) {
            setState(WifiState::Connected);
            ESP_LOGI(kTag, "Wi-Fi connected in %lld ms", msNow() - from);
            // Stay here until the link drops; then try again.
            xEventGroupWaitBits(g_events, kDisconnected, pdFALSE, pdFALSE, portMAX_DELAY);
            ESP_LOGW(kTag, "Wi-Fi link lost: reconnecting");
        } else {
            ESP_LOGW(kTag, "Wi-Fi: no IP after %lld ms; retry in 5 s", msNow() - from);
            (void)esp_wifi_disconnect();
        }
        vTaskDelay(kRetryPause);
    }
}

}  // namespace rb::menusio
