#include "menu_sio.hpp"

#include <array>
#include <atomic>
#include <cerrno>
#include <cstdio>
#include <cstring>
#include <new>

#include "driver/gpio.h"
#include "esp_adc/adc_cali.h"
#include "esp_adc/adc_cali_scheme.h"
#include "esp_adc/adc_oneshot.h"
#include "esp_app_desc.h"
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
rb::host::MenuCatalog* g_catalog = nullptr;  // ~129 KB: PSRAM; read and written only by the SIO task after start()
rb::host::MenuStation* g_station = nullptr;

// Early start: D1 is served before the microSD is mounted. finish() reads the catalog into g_loaded and
// publishes it; the SIO task copies it into g_catalog between two frames, so the controller never sees it
// change while it handles one.
rb::host::MenuCatalog* g_loaded = nullptr;
std::atomic<bool> g_catalogPublished{false};
std::atomic<bool> g_catalogApplied{false};  // written by the SIO task only

// Boot timing (esp_timer: microseconds since the application started; the ROM and the bootloader come first).
int64_t g_readyUs = 0;                     // D1 served
std::atomic<int64_t> g_firstFrameUs{0};    // first COMMAND frame from the Atari (complete or short)
int g_boot5vMv = -1;                       // SIO +5V at start(); -1: not readable
rb::hwdiag::BootRecord g_boot{};
constexpr const char* kBootRecordPath = "/sd/retrobridge-arranques.txt";
constexpr uint32_t kBootRecordDelayMs = 10000;

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

// Logs the SIO +5V; returns it in mV, or -1 if the ADC is not available.
int logSio5v(const char* when)
{
    int pinMv = 0;
    int sioMv = 0;
    if (readSio5v(pinMv, sioMv)) {
        ESP_LOGI(kTag, "%s: SIO +5V (pin 10) %d mV (GPIO35 %d mV)%s", when, sioMv, pinMv,
                 sioMv == 0 ? ": no power from the Atari" : "");
        return sioMv;
    }
    ESP_LOGW(kTag, "%s: SIO +5V not readable (ADC unavailable)", when);
    return -1;
}

long long msOf(int64_t us) { return static_cast<long long>(us / 1000); }

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
        if (!g_catalogApplied && g_catalogPublished.load(std::memory_order_acquire)) {
            *g_catalog = *g_loaded;  // between two frames: the controller is not using it
            g_catalogApplied = true;
            ESP_LOGI(kTag, "catalog: %u image(s) on the microSD root", static_cast<unsigned>(g_catalog->count()));
        }

        (void)g_station->step();

        const bool down = gpio_get_level(kButtonA) == 0;
        const int64_t now = esp_timer_get_time();
        if (g_firstFrameUs.load(std::memory_order_relaxed) == 0) {
            const rb::sio::SioLineStats& line = g_transport->stats();
            if (line.frames + line.shortFrames > 0) {
                g_firstFrameUs.store(now, std::memory_order_relaxed);
                ESP_LOGI(kTag, "first COMMAND frame from the Atari at %lld ms (D1 ready at %lld ms)", msOf(now),
                         msOf(g_readyUs));
            }
        }
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

// Prints the end of the boot records file (the last ~2 KB).
int printBootRecords()
{
    FILE* file = std::fopen(kBootRecordPath, "r");
    if (file == nullptr) {
        std::printf("sin registros de arranque (%s): %s\n", kBootRecordPath, std::strerror(errno));
        return 1;
    }
    if (std::fseek(file, 0, SEEK_END) == 0) {
        const long size = std::ftell(file);
        if (size > 2048) {
            (void)std::fseek(file, size - 2048, SEEK_SET);
            std::array<char, 256> partial{};
            (void)std::fgets(partial.data(), static_cast<int>(partial.size()), file);  // skip the partial first line
        } else {
            (void)std::fseek(file, 0, SEEK_SET);
        }
    }
    std::array<char, 256> line{};
    while (std::fgets(line.data(), static_cast<int>(line.size()), file) != nullptr) {
        std::printf("%s", line.data());
    }
    std::fclose(file);
    return 0;
}

int sioCommand(int argc, char** argv)
{
    if (argc >= 2 && std::strcmp(argv[1], "arranques") == 0) {
        return printBootRecords();
    }
    if (argc < 2 || std::strcmp(argv[1], "status") != 0) {
        std::printf("uso: sio status | sio arranques\n");
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
    const int64_t first = g_firstFrameUs.load(std::memory_order_relaxed);
    std::printf("arranque (ms desde el inicio de la app): D1 listo %lld, primera trama %lld%s, catalogo %s\n",
                msOf(g_readyUs), msOf(first), first == 0 ? " (ninguna)" : "",
                g_catalogApplied ? "cargado" : "pendiente");
    return 0;
}

// Appends one line per boot to the microSD, once the boot has settled: works without USB, and is read
// afterwards with `sio arranques` or on a PC. Distinguishes a brownout or a reset loop (reason and counters
// since power-on) from a D1 that came too late (D1 ready vs. first frame).
void bootRecordTask(void*)
{
    vTaskDelay(pdMS_TO_TICKS(kBootRecordDelayMs));
    int pinMv = 0;
    int sioMv = -1;
    if (!readSio5v(pinMv, sioMv)) {
        sioMv = -1;
    }
    const rb::sio::SioLineStats& s = g_transport->stats();
    const rb::host::MenuTransportStats& m = g_station->stats();
    const int64_t first = g_firstFrameUs.load(std::memory_order_relaxed);
    FILE* file = std::fopen(kBootRecordPath, "a");
    if (file == nullptr) {
        ESP_LOGW(kTag, "boot record not written (%s): %s", kBootRecordPath, std::strerror(errno));
    } else {
        std::fprintf(file,
                     "version=%s arranque=%lu razon=%s brownout=%lu watchdog=%lu panic=%lu listo_ms=%lld "
                     "primera_trama_ms=%lld tramas=%lu cortas=%lu ordenes=%lu sio5v_arranque_mv=%d sio5v_10s_mv=%d\n",
                     esp_app_get_description()->version, static_cast<unsigned long>(g_boot.bootsSincePowerOn),
                     rb::hwdiag::reset_reason_name(g_boot.reason), static_cast<unsigned long>(g_boot.brownoutResets),
                     static_cast<unsigned long>(g_boot.watchdogResets), static_cast<unsigned long>(g_boot.panicResets),
                     msOf(g_readyUs), first == 0 ? -1LL : msOf(first), static_cast<unsigned long>(s.frames),
                     static_cast<unsigned long>(s.shortFrames), static_cast<unsigned long>(m.menuCommands),
                     g_boot5vMv, sioMv);
        std::fclose(file);
        ESP_LOGI(kTag, "boot record appended to %s", kBootRecordPath);
    }
    vTaskDelete(nullptr);
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

bool start(const rb::board::BoardProfile& board)
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
    g_boot5vMv = logSio5v("boot");

    g_catalog = createOnce<rb::host::MenuCatalog>();  // empty until finish() reads the microSD
    if (g_catalog == nullptr) {
        ESP_LOGE(kTag, "no memory for the catalog");
        return false;
    }

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

    g_readyUs = esp_timer_get_time();  // set before the SIO task exists: it reads it without a lock
    if (xTaskCreatePinnedToCore(sioTask, "rb_sio_menu", kTaskStack, nullptr, kTaskPriority, nullptr, kTaskCore) !=
        pdPASS) {
        ESP_LOGE(kTag, "SIO task not created");
        g_station = nullptr;
        line.end();
        return false;
    }
    ESP_LOGI(kTag, "D1: menu. Switch the 65XE on.");
    ESP_LOGI(kTag, "D1 ready at %lld ms (before the microSD and the catalog)", msOf(g_readyUs));
    return true;
}

void finish(bool sdMounted, const rb::hwdiag::BootRecord& boot)
{
    g_boot = boot;
    if (g_station == nullptr) {
        return;  // start() failed: nothing is served
    }
    g_loaded = createOnce<rb::host::MenuCatalog>();
    if (g_loaded == nullptr) {
        ESP_LOGE(kTag, "no memory for the catalog: the menu lists no images");
    } else if (!sdMounted) {
        ESP_LOGW(kTag, "microSD not mounted: the menu lists no images");
    } else {
        errno = 0;
        if (rb::host::readCatalog(rb::esp::SdCardStorage::kMountPoint, *g_loaded)) {
            g_catalogPublished.store(true, std::memory_order_release);  // the SIO task applies it
        } else if (errno == ENOMEM) {
            ESP_LOGE(kTag, "microSD catalog not read: no memory for the 4 KB path buffer");
        } else {
            ESP_LOGW(kTag, "microSD catalog not read (%s): the menu lists no images", std::strerror(errno));
        }
    }

    if (!registerCommand("menu", "menu: D1 vuelve al menu (luego apaga y enciende el 65XE)", menuCommand) ||
        !registerCommand("sio", "sio status | sio arranques: linea SIO, D1, +5V y tiempos de arranque",
                         sioCommand)) {
        ESP_LOGW(kTag, "console commands menu/sio not registered");
    }
    if (sdMounted && xTaskCreate(bootRecordTask, "rb_boot_rec", 4096, nullptr, tskIDLE_PRIORITY + 1, nullptr) !=
                         pdPASS) {
        ESP_LOGW(kTag, "boot record task not created");
    }
    ESP_LOGI(kTag, "app_main stack: %u B never used", static_cast<unsigned>(uxTaskGetStackHighWaterMark(nullptr)));
}

}  // namespace rb::menusio
