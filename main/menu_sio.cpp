#include "menu_sio.hpp"

#include <atomic>
#include <cstdio>
#include <cstring>
#include <new>

#include "driver/gpio.h"
#include "esp_adc/adc_cali.h"
#include "esp_adc/adc_cali_scheme.h"
#include "esp_adc/adc_oneshot.h"
#include "esp_console.h"
#include "esp_heap_caps.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "rb/esp/esp_clock.hpp"
#include "rb/esp/esp_sio_uart.hpp"
#include "rb/esp/sd_card_storage.hpp"
#include "rb/menu/host_image_repository.hpp"
#include "rb/menu/menu_catalog.hpp"
#include "rb/menu/menu_image.hpp"
#include "rb/menu/menu_sources.hpp"
#include "rb/menu/menu_station.hpp"
#include "rb/sio/sio_line_transport.hpp"

namespace rb::menusio {

namespace {

constexpr const char* kTag = "rb_menu_sio";
constexpr gpio_num_t kButtonA = GPIO_NUM_0;     // FujiNet ButtonA: read only, after boot (GPIO0 is a strap)
constexpr int64_t kButtonDebounceUs = 50000;
constexpr adc_channel_t kSio5vChannel = ADC_CHANNEL_7;  // GPIO35
// SIO +5 V divider before revision 1.6: R1 = 2.0 k, R2 = 3.9 k (owner's fork, read as evidence only);
// to confirm against the 1.3 schematic. Below ~500 mV at the pin there is no +5 V on the SIO cable.
constexpr int kDividerNum = 5900;
constexpr int kDividerDen = 3900;
constexpr int kNoSioPowerMv = 500;
constexpr uint32_t kTaskStack = 12288;
constexpr UBaseType_t kTaskPriority = configMAX_PRIORITIES - 2;
constexpr BaseType_t kTaskCore = 1;

rb::esp::EspClock g_clock;
rb::host::MenuImage g_menuImage;
rb::host::MenuSources g_sources;  // E2 (TNFS) fills them; E1 has none
rb::host::HostImageRepository g_repository{rb::esp::SdCardStorage::kMountPoint, g_menuImage};
rb::esp::EspSioUart* g_line = nullptr;
rb::sio::SioLineTransport* g_transport = nullptr;
rb::host::MenuCatalog* g_catalog = nullptr;  // ~129 KB: PSRAM
rb::host::MenuStation* g_station = nullptr;

std::atomic<bool> g_returnRequested{false};  // console `menu`: applied by the SIO task
portMUX_TYPE g_d1Lock = portMUX_INITIALIZER_UNLOCKED;
char g_d1[rb::host::MenuCatalog::kMaxName + 1] = "menu";  // what D1 holds, copied by the SIO task

adc_oneshot_unit_handle_t g_adc = nullptr;
adc_cali_handle_t g_adcCali = nullptr;

template <typename T, typename... Args>
T* createOnce(Args&&... args)
{
    void* memory = heap_caps_malloc(sizeof(T), MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    if (memory == nullptr) {
        memory = heap_caps_malloc(sizeof(T), MALLOC_CAP_8BIT);
    }
    return memory != nullptr ? new (memory) T(static_cast<Args&&>(args)...) : nullptr;
}

bool startAdc()
{
    adc_oneshot_unit_init_cfg_t unit{};
    unit.unit_id = ADC_UNIT_1;
    unit.ulp_mode = ADC_ULP_MODE_DISABLE;
    if (adc_oneshot_new_unit(&unit, &g_adc) != ESP_OK) {
        g_adc = nullptr;
        return false;
    }
    adc_oneshot_chan_cfg_t channel{};
    channel.atten = ADC_ATTEN_DB_12;
    channel.bitwidth = ADC_BITWIDTH_12;
    if (adc_oneshot_config_channel(g_adc, kSio5vChannel, &channel) != ESP_OK) {
        return false;
    }
    adc_cali_line_fitting_config_t cali{};
    cali.unit_id = ADC_UNIT_1;
    cali.atten = ADC_ATTEN_DB_12;
    cali.bitwidth = ADC_BITWIDTH_12;
#if CONFIG_IDF_TARGET_ESP32
    cali.default_vref = 1100;
#endif
    if (adc_cali_create_scheme_line_fitting(&cali, &g_adcCali) != ESP_OK) {
        g_adcCali = nullptr;
    }
    return true;
}

// Pin millivolts and the SIO +5 V they mean; false if the ADC is not available.
bool readSio5v(int& pinMv, int& sioMv)
{
    if (g_adc == nullptr) {
        return false;
    }
    int sum = 0;
    for (int i = 0; i < 8; ++i) {
        int raw = 0;
        if (adc_oneshot_read(g_adc, kSio5vChannel, &raw) != ESP_OK) {
            return false;
        }
        int mv = raw * 3300 / 4095;  // uncalibrated fallback
        if (g_adcCali != nullptr) {
            (void)adc_cali_raw_to_voltage(g_adcCali, raw, &mv);
        }
        sum += mv;
    }
    pinMv = sum / 8;
    sioMv = pinMv < kNoSioPowerMv ? 0 : pinMv * kDividerNum / kDividerDen;
    return true;
}

void logSio5v(const char* when)
{
    int pinMv = 0;
    int sioMv = 0;
    if (readSio5v(pinMv, sioMv)) {
        ESP_LOGI(kTag, "%s: SIO +5V (pin 10) %d mV (GPIO35 %d mV)%s", when, sioMv, pinMv,
                 sioMv == 0 ? ": no power from the Atari" : "");
    } else {
        ESP_LOGW(kTag, "%s: SIO +5V not readable (ADC unavailable)", when);
    }
}

void setD1(const char* text)
{
    portENTER_CRITICAL(&g_d1Lock);
    std::strncpy(g_d1, text, sizeof g_d1 - 1);
    g_d1[sizeof g_d1 - 1] = '\0';
    portEXIT_CRITICAL(&g_d1Lock);
}

void copyD1(char* out, std::size_t size)
{
    portENTER_CRITICAL(&g_d1Lock);
    std::strncpy(out, g_d1, size - 1);
    out[size - 1] = '\0';
    portEXIT_CRITICAL(&g_d1Lock);
}

void sioTask(void*)
{
    bool wasMenu = true;
    bool buttonDown = false;
    bool buttonHandled = false;
    int64_t buttonSince = 0;
    for (;;) {
        (void)g_station->step();

        const bool down = gpio_get_level(kButtonA) == 0;
        const int64_t now = esp_timer_get_time();
        if (down && !buttonDown) {
            buttonSince = now;
            buttonHandled = false;
        }
        buttonDown = down;
        const bool buttonPressed = down && !buttonHandled && now - buttonSince >= kButtonDebounceUs;
        if (buttonPressed) {
            buttonHandled = true;
        }
        if (buttonPressed || g_returnRequested.exchange(false)) {
            g_station->returnToMenu();
            ESP_LOGI(kTag, "D1 back to the menu (%s): switch the 65XE off and on", buttonPressed ? "button A" : "console");
        }

        const bool isMenu = g_station->menuOnD1();
        if (isMenu != wasMenu) {
            const char* name = isMenu ? "menu" : g_station->controller().driveName(1);
            setD1(name != nullptr ? name : "?");
            ESP_LOGI(kTag, "D1: %s", name != nullptr ? name : "?");
            wasMenu = isMenu;
        }
    }
}

int menuCommand(int, char**)
{
    g_returnRequested.store(true);
    std::printf("D1 vuelve al menu en el proximo paso SIO. Apaga y enciende el 65XE.\n");
    return 0;
}

int sioCommand(int argc, char** argv)
{
    if (argc < 2 || std::strcmp(argv[1], "status") != 0) {
        std::printf("uso: sio status\n");
        return 1;
    }
    const rb::sio::SioLineStats& s = g_transport->stats();
    const rb::host::MenuTransportStats& m = g_station->stats();
    char d1[sizeof g_d1];
    copyD1(d1, sizeof d1);
    std::printf("D1: %s\n", d1);
    std::printf("COMMAND (GPIO39): %s  DATA OUT (GPIO33): %s\n", g_line->commandHigh() ? "alto" : "BAJO",
                g_line->dataOutHigh() ? "alto" : "BAJO");
    std::printf("tramas %lu cortas %lu command_pegado %lu err_rx %lu err_tx %lu liberaciones %lu\n",
                static_cast<unsigned long>(s.frames), static_cast<unsigned long>(s.shortFrames),
                static_cast<unsigned long>(s.stuckLows), static_cast<unsigned long>(s.receiveErrors),
                static_cast<unsigned long>(s.sendErrors), static_cast<unsigned long>(s.releases));
    std::printf("latencia COMMAND->respuesta: ultima %lu us, maxima %lu us\n",
                static_cast<unsigned long>(s.lastAckLatencyUs), static_cast<unsigned long>(s.maxAckLatencyUs));
    std::printf("menu: ordenes %lu reintentos %lu nak %lu descartadas_tras_vuelta %lu\n",
                static_cast<unsigned long>(m.menuCommands), static_cast<unsigned long>(m.menuRetries),
                static_cast<unsigned long>(m.menuNaks), static_cast<unsigned long>(m.droppedAfterReset));
    int pinMv = 0;
    int sioMv = 0;
    if (readSio5v(pinMv, sioMv)) {
        std::printf("SIO +5V (pin 10): %d mV (GPIO35 %d mV)%s\n", sioMv, pinMv,
                    sioMv == 0 ? " sin alimentacion del Atari" : "");
    } else {
        std::printf("SIO +5V: ADC no disponible\n");
    }
    return 0;
}

bool registerCommand(const char* name, const char* help, esp_console_cmd_func_t func)
{
    esp_console_cmd_t command{};
    command.command = name;
    command.help = help;
    command.func = func;
    return esp_console_cmd_register(&command) == ESP_OK;
}

}  // namespace

bool start(const rb::board::BoardProfile& board, bool sdMounted)
{
    ESP_LOGW(kTag, "menu-sio TEST PACKAGE: transmits on SIO DATA IN. H4 and M3a NOT approved. Use only with the "
                   "owner's written risk acceptance and the SIO adapter without pin 8.");
    if (board.sio.command < 0 || board.sio.rx < 0 || board.sio.tx < 0) {
        ESP_LOGE(kTag, "board profile has no SIO pins: menu not served");
        return false;
    }
    (void)gpio_set_direction(kButtonA, GPIO_MODE_INPUT);
    (void)gpio_pullup_en(kButtonA);
    if (!startAdc()) {
        ESP_LOGW(kTag, "ADC1 for GPIO35 not available");
    }
    logSio5v("boot");

    g_catalog = createOnce<rb::host::MenuCatalog>();
    if (g_catalog == nullptr) {
        ESP_LOGE(kTag, "no memory for the catalog");
        return false;
    }
    if (!sdMounted || !rb::host::readCatalog(rb::esp::SdCardStorage::kMountPoint, *g_catalog)) {
        ESP_LOGW(kTag, "microSD catalog not read: the menu lists no images");
    }
    ESP_LOGI(kTag, "catalog: %u image(s) on the microSD root", static_cast<unsigned>(g_catalog->count()));

    static rb::esp::EspSioUart line{rb::esp::EspSioUart::Pins{board.sio.command, board.sio.rx, board.sio.tx}};
    g_line = &line;
    ESP_LOGI(kTag, "idle levels before the UART: COMMAND %s, DATA OUT %s", line.commandHigh() ? "high" : "LOW",
             line.dataOutHigh() ? "high" : "LOW");
    if (!line.begin()) {
        return false;
    }
    static rb::sio::SioLineTransport transport{line, g_clock};
    g_transport = &transport;
    g_station = createOnce<rb::host::MenuStation>(
        transport, g_clock, rb::host::MenuStationParts{*g_catalog, g_sources, g_repository});
    if (g_station == nullptr || !g_station->controller().ready()) {
        ESP_LOGE(kTag, "no memory for the menu");
        line.end();
        return false;
    }
    g_station->start();  // D1 = menu: the next boot of the Atari loads it
    transport.releaseDataLine();

    if (!registerCommand("menu", "menu: D1 vuelve al menu (luego apaga y enciende el 65XE)", menuCommand) ||
        !registerCommand("sio", "sio status: linea SIO, D1, latencia y +5V del SIO", sioCommand)) {
        ESP_LOGW(kTag, "console commands menu/sio not registered");
    }
    if (xTaskCreatePinnedToCore(sioTask, "rb_sio_menu", kTaskStack, nullptr, kTaskPriority, nullptr, kTaskCore) !=
        pdPASS) {
        ESP_LOGE(kTag, "SIO task not created");
        line.end();
        return false;
    }
    ESP_LOGI(kTag, "D1: menu. Switch the 65XE on.");
    return true;
}

}  // namespace rb::menusio
