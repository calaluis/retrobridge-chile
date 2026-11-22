#include "bench_wifi.hpp"

#include <cstdarg>
#include <cstdint>
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
#include "lwip/netdb.h"
#include "lwip/sockets.h"
#include "rb/core/span.hpp"
#include "rb/menu/sha256.hpp"

namespace rb::bench {

namespace {

constexpr const char* kTag = "wifi_bench";
constexpr const char* kWifiFile = "/sd/retrobridge-wifi.txt";
constexpr const char* kLogFile = "/sd/retrobridge-banco-wifi.txt";

// The test transfer (plan menu-sio-wifi-tnfs, "Ensayo de transferencia"), checked from the PC on 2026-11-22.
constexpr const char* kServer = "tnfs.calaluis.cl";
constexpr const char* kPort = "16384";
constexpr const char* kAtrPath = "/Aportes_Eduardo_Antonio (Chalo)/Demoscene/NumenPAL.atr";
constexpr std::size_t kAtrBytes = 196496;
constexpr uint8_t kAtrSha256[32] = {0x7a, 0x47, 0xb8, 0xe0, 0xec, 0xb6, 0xe5, 0x3d, 0xa0, 0x8f, 0x77,
                                    0x3f, 0x82, 0x87, 0xe0, 0x7c, 0xd6, 0xe8, 0x11, 0xf3, 0x89, 0xce,
                                    0x09, 0xda, 0x6b, 0x46, 0x18, 0xaa, 0x82, 0xe1, 0xa2, 0xcc};

constexpr int64_t kPhaseUs = 30 * 1000 * 1000;  // long enough to read a meter
constexpr TickType_t kConnectWait = pdMS_TO_TICKS(20000);
constexpr int kTries = 4;
constexpr int kReplyTimeoutMs = 1000;

constexpr EventBits_t kGotIp = BIT0;
constexpr EventBits_t kDisconnected = BIT1;
EventGroupHandle_t g_events = nullptr;

long long msNow() { return static_cast<long long>(esp_timer_get_time() / 1000); }

void note(const char* format, ...) __attribute__((format(printf, 1, 2)));
void note(const char* format, ...)
{
    char line[160];
    va_list args;
    va_start(args, format);
    std::vsnprintf(line, sizeof line, format, args);
    va_end(args);
    ESP_LOGW(kTag, "WIFI BENCH %s", line);
    if (std::FILE* file = std::fopen(kLogFile, "a")) {  // for T1b, with the USB disconnected
        std::fprintf(file, "%lld ms %s\n", msNow(), line);
        std::fclose(file);
    }
}

// Line 1: network name (1..32 bytes), line 2: key (empty or 8..63 bytes). Only CR/LF are removed.
bool readWifiFile(wifi_config_t& config)
{
    std::FILE* file = std::fopen(kWifiFile, "rb");
    if (file == nullptr) {
        note("FAIL: no %s", kWifiFile);
        return false;
    }
    char lines[2][80] = {};
    bool ok = true;
    for (auto& line : lines) {
        if (std::fgets(line, sizeof line, file) == nullptr) {
            line[0] = '\0';
        }
        line[std::strcspn(line, "\r\n")] = '\0';
    }
    std::fclose(file);
    const std::size_t ssid = std::strlen(lines[0]);
    const std::size_t key = std::strlen(lines[1]);
    if (ssid == 0 || ssid > sizeof config.sta.ssid) {
        note("FAIL: network name of %u bytes (1..32)", static_cast<unsigned>(ssid));
        ok = false;
    } else if (key != 0 && (key < 8 || key > 63)) {
        note("FAIL: key of %u bytes (empty or 8..63)", static_cast<unsigned>(key));  // the length only
        ok = false;
    } else {
        std::memcpy(config.sta.ssid, lines[0], ssid);
        std::memcpy(config.sta.password, lines[1], key);
        config.sta.threshold.authmode = key == 0 ? WIFI_AUTH_OPEN : WIFI_AUTH_WPA2_PSK;
        note("network '%s', key of %u bytes", lines[0], static_cast<unsigned>(key));
    }
    std::memset(lines, 0, sizeof lines);  // the key stays only in the driver's RAM copy
    return ok;
}

void onEvent(void*, esp_event_base_t base, int32_t id, void*)
{
    if (base == WIFI_EVENT && id == WIFI_EVENT_STA_DISCONNECTED) {
        xEventGroupClearBits(g_events, kGotIp);
        xEventGroupSetBits(g_events, kDisconnected);
    } else if (base == IP_EVENT && id == IP_EVENT_STA_GOT_IP) {
        xEventGroupClearBits(g_events, kDisconnected);
        xEventGroupSetBits(g_events, kGotIp);
    }
}

bool check(const char* call, esp_err_t err)
{
    if (err != ESP_OK) {
        note("FAIL: %s -> %s", call, esp_err_to_name(err));
        return false;
    }
    return true;
}

// --- A minimal TNFS client for the transfer phase (UDP; mount, open, read, close, umount) ----------------

class Tnfs {
public:
    ~Tnfs()
    {
        if (socket_ >= 0) {
            ::close(socket_);
        }
    }

    bool open()
    {
        addrinfo hints{};
        hints.ai_family = AF_INET;
        hints.ai_socktype = SOCK_DGRAM;
        addrinfo* found = nullptr;
        if (::getaddrinfo(kServer, kPort, &hints, &found) != 0 || found == nullptr) {
            note("FAIL: %s not resolved", kServer);
            return false;
        }
        std::memcpy(&server_, found->ai_addr, sizeof server_);
        ::freeaddrinfo(found);
        socket_ = ::socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
        timeval timeout{0, kReplyTimeoutMs * 1000};
        return socket_ >= 0 && ::setsockopt(socket_, SOL_SOCKET, SO_RCVTIMEO, &timeout, sizeof timeout) == 0;
    }

    // Sends command `cmd` with `payload`; the reply (status at [4]) in reply_, its length returned; -1: none.
    int call(uint8_t cmd, const uint8_t* payload, std::size_t size)
    {
        uint8_t request[600];
        if (size + 4 > sizeof request) {
            return -1;
        }
        for (int t = 0; t < kTries; ++t) {
            ++seq_;
            request[0] = static_cast<uint8_t>(conn_ & 0xFF);
            request[1] = static_cast<uint8_t>(conn_ >> 8);
            request[2] = seq_;
            request[3] = cmd;
            std::memcpy(request + 4, payload, size);
            ::sendto(socket_, request, size + 4, 0, reinterpret_cast<const sockaddr*>(&server_), sizeof server_);
            for (;;) {
                const int got = ::recv(socket_, reply_, sizeof reply_, 0);
                if (got < 0) {
                    break;  // timeout: try again
                }
                if (got >= 5 && reply_[2] == seq_ && reply_[3] == cmd) {
                    return got;
                }
            }
        }
        return -1;
    }

    // Downloads the test ATR whole; true if size and SHA-256 match.
    bool fetch(std::size_t& bytes)
    {
        bytes = 0;
        static const uint8_t mount[] = {0x02, 0x01, '/', 0, 0, 0};
        conn_ = 0;
        if (call(0x00, mount, sizeof mount) < 5 || reply_[4] != 0) {
            note("FAIL: TNFS mount");
            return false;
        }
        conn_ = static_cast<uint16_t>(reply_[0] | (reply_[1] << 8));
        uint8_t open[4 + 128] = {0x01, 0x00, 0x00, 0x00};  // O_RDONLY, mode 0
        const std::size_t pathLength = std::strlen(kAtrPath) + 1;
        std::memcpy(open + 4, kAtrPath, pathLength);
        bool ok = false;
        if (call(0x29, open, 4 + pathLength) < 6 || reply_[4] != 0) {
            note("FAIL: TNFS open");
        } else {
            const uint8_t handle = reply_[5];
            rb::host::Sha256 sha;
            for (;;) {
                const uint8_t read[] = {handle, 0x00, 0x02};  // 512 bytes
                const int got = call(0x21, read, sizeof read);
                if (got < 5 || reply_[4] != 0) {
                    break;  // end of file (or no reply)
                }
                const std::size_t n = static_cast<std::size_t>(reply_[5] | (reply_[6] << 8));
                if (n == 0 || static_cast<int>(7 + n) > got) {
                    break;
                }
                sha.update(rb::core::Span<const uint8_t>{reply_ + 7, n});
                bytes += n;
            }
            const uint8_t close[] = {handle};
            (void)call(0x23, close, sizeof close);
            const auto digest = sha.finish();
            ok = bytes == kAtrBytes && std::memcmp(digest.data(), kAtrSha256, sizeof kAtrSha256) == 0;
        }
        (void)call(0x01, nullptr, 0);
        return ok;
    }

private:
    int socket_ = -1;
    sockaddr_in server_{};
    uint16_t conn_ = 0;
    uint8_t seq_ = 0;
    uint8_t reply_[1024] = {};
};

bool connect()
{
    xEventGroupClearBits(g_events, kGotIp | kDisconnected);
    const long long from = msNow();
    if (!check("esp_wifi_connect", esp_wifi_connect())) {
        return false;
    }
    const EventBits_t bits = xEventGroupWaitBits(g_events, kGotIp, pdFALSE, pdFALSE, kConnectWait);
    if ((bits & kGotIp) == 0) {
        note("FAIL: no IP after %lld ms", msNow() - from);
        return false;
    }
    note("connected in %lld ms", msNow() - from);
    return true;
}

}  // namespace

bool run_wifi(bool sdMounted) noexcept
{
    ESP_LOGW(kTag, "WIFI BENCH: bench package, never published; no SIO pin is used");
    if (!sdMounted) {
        ESP_LOGE(kTag, "WIFI BENCH FAIL: no microSD");
        return false;
    }
    wifi_config_t config{};
    if (!readWifiFile(config)) {
        return false;
    }
    esp_log_level_set("wifi", ESP_LOG_WARN);
    g_events = xEventGroupCreate();
    wifi_init_config_t init = WIFI_INIT_CONFIG_DEFAULT();
    const bool ready = g_events != nullptr && check("esp_netif_init", esp_netif_init()) &&
                       check("esp_event_loop_create_default", esp_event_loop_create_default()) &&
                       esp_netif_create_default_wifi_sta() != nullptr && check("esp_wifi_init", esp_wifi_init(&init)) &&
                       check("esp_wifi_set_storage", esp_wifi_set_storage(WIFI_STORAGE_RAM)) &&
                       check("handler", esp_event_handler_register(WIFI_EVENT, ESP_EVENT_ANY_ID, onEvent, nullptr)) &&
                       check("handler", esp_event_handler_register(IP_EVENT, IP_EVENT_STA_GOT_IP, onEvent, nullptr)) &&
                       check("esp_wifi_set_mode", esp_wifi_set_mode(WIFI_MODE_STA)) &&
                       check("esp_wifi_set_config", esp_wifi_set_config(WIFI_IF_STA, &config));
    std::memset(&config, 0, sizeof config);
    if (!ready) {
        return false;
    }
    for (unsigned cycle = 1;; ++cycle) {
        note("cycle %u phase 0 IDLE (radio off) for 30 s", cycle);
        vTaskDelay(pdMS_TO_TICKS(kPhaseUs / 1000));

        if (!check("esp_wifi_start", esp_wifi_start())) {
            return false;
        }
        note("cycle %u phase 1 SCAN for 30 s", cycle);
        unsigned scans = 0;
        for (const int64_t end = esp_timer_get_time() + kPhaseUs; esp_timer_get_time() < end; ++scans) {
            uint16_t found = 0;
            if (esp_wifi_scan_start(nullptr, true) == ESP_OK) {
                (void)esp_wifi_scan_get_ap_num(&found);
                (void)esp_wifi_clear_ap_list();
            }
        }
        note("cycle %u scans %u", cycle, scans);

        note("cycle %u phase 2 CONNECTED IDLE for 30 s", cycle);
        const int64_t connectedEnd = esp_timer_get_time() + kPhaseUs;
        const bool online = connect();
        while (esp_timer_get_time() < connectedEnd) {
            vTaskDelay(pdMS_TO_TICKS(500));
        }

        if (online) {
            note("cycle %u phase 3 TNFS TRANSFER for 30 s", cycle);
            unsigned good = 0, bad = 0;
            for (const int64_t end = esp_timer_get_time() + kPhaseUs; esp_timer_get_time() < end;) {
                Tnfs tnfs;
                std::size_t bytes = 0;
                const long long from = msNow();
                const bool ok = tnfs.open() && tnfs.fetch(bytes);
                if (ok) {
                    ++good;
                } else {
                    ++bad;
                }
                note("cycle %u transfer %u bytes in %lld ms, sha256 %s", cycle, static_cast<unsigned>(bytes),
                     msNow() - from, ok ? "OK" : "FAIL");
                if (!ok) {
                    vTaskDelay(pdMS_TO_TICKS(1000));
                }
            }
            note("cycle %u transfers ok %u, failed %u", cycle, good, bad);
        }
        (void)esp_wifi_disconnect();
        (void)check("esp_wifi_stop", esp_wifi_stop());
    }
}

}  // namespace rb::bench
