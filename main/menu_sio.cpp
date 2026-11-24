#include "menu_sio.hpp"

#include "cassette_ports.hpp"

#include "sdkconfig.h"

#include <array>
#include <atomic>
#include <cerrno>
#include <cstdio>
#include <cstring>
#include <new>
#include <type_traits>

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
#include "freertos/queue.h"
#include "freertos/semphr.h"
#include "freertos/task.h"
#include "rb/esp/esp_clock.hpp"
#include "rb/esp/esp_sio_uart.hpp"
#include "rb/esp/sd_card_storage.hpp"
#include "rb/menu/data_in_arbiter.hpp"
#include "rb/menu/drive_memory.hpp"
#include "rb/menu/drive_memory_link.hpp"
#include "rb/menu/drive_slot_store.hpp"
#include "rb/menu/host_image_repository.hpp"
#include "rb/menu/menu_catalog.hpp"
#include "rb/menu/menu_image.hpp"
#include "rb/menu/menu_sources.hpp"
#include "rb/menu/menu_station.hpp"
#include "rb/menu/network_gate.hpp"
#include "rb/menu/sio_power_watch.hpp"
#include "rb/menu/startup_flags.hpp"
#include "rb/menu/storage_reason.hpp"
#include "rb/sio/sio_line_transport.hpp"

#include "esp_pthread.h"
#include "esp_tape_deck.hpp"
#include "tnfs_link.hpp"
#include "tnfs_loader.hpp"
#include "udp_tnfs_port.hpp"
#include "wifi_station.hpp"

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

// E2 (menu-sio-wifi-tnfs): the TNFS loader of the PC (rb_menu_tnfs) over the FujiNet's Wi-Fi.
constexpr const char* kSourcesPath = "/sd/retrobridge-fuentes.txt";
constexpr const char* kWifiPath = "/sd/retrobridge-wifi.txt";
constexpr uint32_t kNetTaskStack = 8192;
constexpr std::size_t kLoaderStack = 12288;
rb::host::TnfsLoader* g_loader = nullptr;   // created in start(); null: sources 2..8 say the client is missing
rb::host::MenuSources g_loadedSources;      // written by the network task before g_sourcesPublished
std::atomic<bool> g_sourcesPublished{false};
bool g_sourcesApplied = false;              // SIO task only

// Menu events on the console. Only the outcome of a TNFS job, a mount, a rejected server and the restore
// of the drives (menu_restore: a few lines per power-up, one per drive and outcome; the bench of
// menu-tnfs-persistente T8 needs them) are shown; every key and every TNFS request stays at DEBUG, so the
// SIO task never waits on the UART for a line.
class FirmwareMenuEvents final : public rb::host::MenuEvents {
public:
    void menuEvent(const char* name, const char* detail) override
    {
        const char* text = detail != nullptr ? detail : "";
        if (std::strcmp(name, "menu_tnfs") == 0 || std::strcmp(name, "menu_fetch") == 0 ||
            std::strcmp(name, "menu_mount") == 0 || std::strcmp(name, "menu_source_rejected") == 0 ||
            std::strcmp(name, "menu_restore") == 0) {
            ESP_LOGI("rb_menu_sio", "%s %s", name, text);
        } else {
            ESP_LOGD("rb_menu_sio", "%s %s", name, text);
        }
    }
};
FirmwareMenuEvents g_events;

// T5 (C7): what the menu asks before a TNFS job. It reads the station's flag; it never waits.
class WifiGate final : public rb::host::NetworkGate {
public:
    rb::host::NetworkState state() const noexcept override
    {
        using rb::host::NetworkState;
        switch (rb::menusio::wifiState()) {
        case rb::menusio::WifiState::Connected:
            return NetworkState::Ready;
        case rb::menusio::WifiState::NoFile:
            return NetworkState::NoFile;
        case rb::menusio::WifiState::BadFile:
            return NetworkState::BadFile;
        case rb::menusio::WifiState::Failed:
            return NetworkState::Failed;
        case rb::menusio::WifiState::NoNetwork:
            return NetworkState::NoNetwork;
        case rb::menusio::WifiState::BadKey:
            return NetworkState::BadKey;
        case rb::menusio::WifiState::Connecting:
            break;
        }
        return NetworkState::Connecting;
    }
};

WifiGate g_wifiGate;
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
SemaphoreHandle_t g_adcLock = nullptr;  // the SIO task, the console and the boot record all read GPIO35
constexpr int64_t kPowerSampleUs = 50000;  // plan §4: the SIO task samples the +5 V every 50 ms, between frames

// Persistent drives (menu-unidades-persistentes). finish() reads the slots once, writes g_plan, creates the
// queues and rb_drive_save, and only then publishes the plan (g_startup). The SIO task touches g_plan
// and the queues only after it has seen that flag (acquire), and the queues only when g_plan allows saving.
// The SIO task never writes to the microSD.
// The tape deck (menu-cinta T6): finish() starts the cassette and creates the deck, then publishes it
// BEFORE the plan (g_startup). The SIO task reads both with one g_startup.read() per step, plan first
// (review of PR #85): with the plan seen the deck is seen too, gives the deck to the controller between
// two frames and only then applies the plan, so the restore of C: always has it; and pumps it every step.
const rb::board::BoardProfile* g_board = nullptr;  // start(): the gates come from it
EspTapeDeck* g_tape = nullptr;                     // finish() creates it; then the SIO task only
bool g_tapeApplied = false;                        // SIO task only
rb::host::StartupFlags g_startup;                  // finish(): publishTape(), then publishPlan()
rb::esp::EspMotorIn* g_motor = nullptr;            // finish(), before publishTape(); reads off without M3a
// "The tape played to its end" (menu-cinta T9a.7, requirement 2): the cassette task publishes it every tick
// through g_progressSink (given before start()); the SIO task reads it with tapeEnded() (T9a.7b). Both
// accesses are in cassette_ports.cpp, the object CI checks for lock-free atomics.
rb::host::TapeProgress g_progress;
ProgressSink g_progressSink{g_progress};

// Who owns DATA IN (menu-cinta T7, plan §2.3): one arbiter, stepped by the SIO task between two frames.
// The disk line is the SIO UART. The tape line is not built yet: CassetteService chooses its outputs once,
// at start(), so it cannot take DATA IN while it runs (T9, after M3a/H4). kTapeOutput keeps the arbiter's
// gates closed until then: the disk never lets go and the SIO UART is never ended (AC8, rule 4).
constexpr bool kTapeOutput = false;

// T9a.1 changed the line contract (park/resume, asynchronous take/release). These adapters keep
// today's behaviour and are never reached while kTapeOutput is false (the gates stay closed). T9a.7
// replaces them: park/resume without deleting the UART2 driver, and the cassette over DataInHandover.
class SioDiskLine final : public rb::host::DiskLine {
public:
    bool park() noexcept override
    {
        if (g_line != nullptr) {
            g_line->end();
        }
        return true;
    }
    rb::host::LineResult resume() noexcept override
    {
        return g_line != nullptr && g_line->begin() ? rb::host::LineResult::Done : rb::host::LineResult::Failed;
    }
};

class PendingTapeLine final : public rb::host::TapeLine {
public:
    rb::host::LineResult take() noexcept override { return rb::host::LineResult::Failed; }  // never reached
    rb::host::LineResult release() noexcept override { return rb::host::LineResult::Done; }
    void abandon() noexcept override {}
};

SioDiskLine g_diskLine;
PendingTapeLine g_tapeLine;
rb::host::DataInArbiter g_arbiter{g_diskLine, g_tapeLine};  // SIO task only
bool g_atariOff = false;                                     // SIO task only: SioPowerWatch's last event

rb::host::DriveMemoryPlan g_plan;            // written by finish() before g_startup.publishPlan()
rb::host::DriveMemoryLink g_link;            // SIO task only (~1 KB: not on its stack)
rb::host::SlotFileStore* g_store = nullptr;  // finish() reads it once; then rb_drive_save only

struct SaveRequest {
    uint32_t version = 0;
    rb::host::DriveMemory memory;
};
struct SaveOutcome {
    uint32_t version = 0;
    bool ok = false;
    int error = 0;
};
static_assert(std::is_trivially_copyable_v<SaveRequest> && std::is_trivially_copyable_v<SaveOutcome>,
              "FreeRTOS queues copy items byte by byte");
QueueHandle_t g_saveRequests = nullptr;  // one item, overwritten: only the newest memory matters
QueueHandle_t g_saveOutcomes = nullptr;  // one item, overwritten: only the newest result changes the screen
constexpr uint32_t kSaveTaskStack = CONFIG_RB_DRIVE_SAVE_STACK;  // the package checks the frames against it

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
    g_adcLock = xSemaphoreCreateMutex();
    if (g_adcLock == nullptr) {
        return false;
    }
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
// `wait`: false for the SIO task, which skips a sample rather than wait for another reader.
bool readSio5v(int& pinMv, int& sioMv, bool wait = true)
{
    if (g_adc == nullptr || g_adcLock == nullptr) {
        return false;
    }
    if (xSemaphoreTake(g_adcLock, wait ? portMAX_DELAY : 0) != pdTRUE) {
        return false;
    }
    int sum = 0;
    for (int i = 0; i < 8; ++i) {
        int raw = 0;
        if (adc_oneshot_read(g_adc, kSio5vChannel, &raw) != ESP_OK) {
            xSemaphoreGive(g_adcLock);
            return false;
        }
        int mv = raw * 3300 / 4095;  // uncalibrated fallback
        if (g_adcCali != nullptr) {
            (void)adc_cali_raw_to_voltage(g_adcCali, raw, &mv);
        }
        sum += mv;
    }
    xSemaphoreGive(g_adcLock);
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

uint32_t framesSoFar()
{
    const rb::sio::SioLineStats& line = g_transport->stats();
    return line.frames + line.shortFrames;
}

// rb_drive_save seen from the SIO task: never blocks. Used only when the published plan allows saving.
class QueueSaver final : public rb::host::DriveMemoryLink::Saver {
public:
    void submit(uint32_t version, const rb::host::DriveMemory& memory) noexcept override
    {
        request_.version = version;
        request_.memory = memory;
        (void)xQueueOverwrite(g_saveRequests, &request_);
    }
    bool poll(uint32_t& version, bool& ok, int& error) noexcept override
    {
        SaveOutcome outcome;
        if (xQueueReceive(g_saveOutcomes, &outcome, 0) != pdTRUE) {
            return false;
        }
        version = outcome.version;
        ok = outcome.ok;
        error = outcome.error;
        return true;
    }

private:
    SaveRequest request_;
};
QueueSaver g_saver;  // SIO task only (~1 KB: not on its stack)

// Between two frames: once the plan is published, restores (after the catalog) and handles saves.
// `planPublished`: this step's g_startup.read(), the same read that applied the deck (review of PR #85).
void serviceDriveMemory(rb::host::MenuController& controller, bool planPublished, bool& planSeen,
                        bool& restoreLogged)
{
    if (!planSeen) {
        planSeen = planPublished;
        if (!planSeen) {
            return;  // g_plan and the queues are not ready yet
        }
    }
    (void)g_link.apply(controller, g_plan, g_catalogApplied);
    if (g_link.applied() && !restoreLogged && !controller.restoring()) {  // D2..D8 mount one per step
        restoreLogged = true;
        ESP_LOGI(kTag, "unidades restauradas a %lld ms (%s); guardado %s", msOf(esp_timer_get_time()),
                 g_plan.restore ? "memoria leida" : g_plan.message, g_plan.saveAllowed ? "activo" : "DESACTIVADO");
    }
    g_link.saves(controller, g_plan, g_saver);
}

void driveSaveTask(void*)
{
    static SaveRequest request;  // ~1 KB: off the stack
    for (;;) {
        if (xQueueReceive(g_saveRequests, &request, portMAX_DELAY) != pdTRUE) {
            continue;
        }
        const int64_t began = esp_timer_get_time();
        const rb::host::SlotFileStore::SaveResult saved = g_store->save(request.memory);
        const SaveOutcome outcome{request.version, saved.ok, saved.error};
        (void)xQueueOverwrite(g_saveOutcomes, &outcome);
        if (saved.ok) {
            ESP_LOGI(kTag, "drive memory version %lu saved in %lld ms", static_cast<unsigned long>(request.version),
                     msOf(esp_timer_get_time() - began));
        } else {
            ESP_LOGW(kTag, "drive memory version %lu NOT saved: %s (errno %d)",
                     static_cast<unsigned long>(request.version), rb::host::storageReason(saved.error), saved.error);
        }
    }
}

void sioTask(void*)
{
    bool wasMenu = true;
    bool buttonDown = false;
    bool buttonHandled = false;
    int64_t buttonSince = 0;
    bool planSeen = false;
    bool restoreLogged = false;
    rb::host::SioPowerWatch power;
    int64_t lastPowerSample = 0;
    int64_t poweredOnUs = 0;  // non-zero until the first frame after a power-on is logged
    uint32_t framesAtPowerOn = 0;
    for (;;) {
        if (!g_catalogApplied && g_catalogPublished.load(std::memory_order_acquire)) {
            *g_catalog = *g_loaded;  // between two frames: the controller is not using it
            g_catalogApplied = true;
            ESP_LOGI(kTag, "catalog: %u image(s) on the microSD root", static_cast<unsigned>(g_catalog->count()));
        }
        if (!g_sourcesApplied && g_sourcesPublished.load(std::memory_order_acquire)) {
            g_sources = g_loadedSources;  // between two frames: the controller is not using it
            if (g_loader != nullptr) {
                for (unsigned n = 2; n <= rb::host::TnfsLoader::kMaxSource; ++n) {
                    const char* server = g_sources.name(n);
                    g_loader->changeServer(n, server != nullptr ? server : "");
                }
            }
            g_station->controller().setSourcesFile(kSourcesPath);  // E edits it (menu-editar-servidor)
            g_station->controller().setNetworkGate(&g_wifiGate);    // T5: sources 2..8 say why without a network
            g_sourcesApplied = true;
            ESP_LOGI(kTag, "sources: %u TNFS server(s)", static_cast<unsigned>(g_sources.count()));
        }
        // One read per step, plan first: if the plan is seen, the deck published before it is seen too.
        const rb::host::StartupFlags::Seen published = g_startup.read();
        if (!g_tapeApplied && published.tape) {  // before the drive memory: the restore of C: finds the deck
            g_station->controller().setTapeDeck(&g_tape->deck());
            g_tapeApplied = true;
            ESP_LOGI(kTag, "cas: deck en el menu (puertas %s)", g_tape->deck().gatesOpen() ? "abiertas" : "cerradas");
            if (!g_tape->deck().gatesOpen()) {
                ESP_LOGW(kTag, "cas: sin M3a/H4, la cinta no se reproduce; DATA IN queda para el disco");
            } else if (!kTapeOutput) {
                ESP_LOGW(kTag, "cas: la cinta aun no toma DATA IN (T9); DATA IN queda para el disco");
            }
        }
        serviceDriveMemory(g_station->controller(), published.plan, planSeen, restoreLogged);

        const rb::sio::StepOutcome stepped = g_station->step();
        if (g_tapeApplied) {
            g_tape->deck().pump();  // takes `cas` replies and sends what waits; never blocks
        }

        const bool down = gpio_get_level(kButtonA) == 0;
        const int64_t now = esp_timer_get_time();
        // +5 V only after a step that sent nothing: the 8 ADC reads (~0.2 ms) never delay an answer.
        // With the 65XE off, COMMAND stays low and every step ends in TransportFailed (powerSampleAfter).
        if (rb::host::powerSampleAfter(stepped) && now - lastPowerSample >= kPowerSampleUs) {
            lastPowerSample = now;
            int pinMv = 0;
            int sioMv = 0;
            if (readSio5v(pinMv, sioMv, false)) {
                switch (power.sample(sioMv, static_cast<uint64_t>(now))) {
                case rb::host::SioPowerWatch::Event::PoweredOff:
                    g_station->returnToMenu();  // ready before the next power-on reads D1; drives kept
                    poweredOnUs = 0;
                    g_atariOff = true;  // the arbiter gives DATA IN back to the disk (rule 3)
                    ESP_LOGI(kTag, "65XE off at %lld ms (SIO +5V %d mV): D1 back to the menu, D1-D8 kept",
                             msOf(now), sioMv);
                    break;
                case rb::host::SioPowerWatch::Event::PoweredOn:
                    poweredOnUs = now;
                    g_atariOff = false;
                    framesAtPowerOn = framesSoFar();
                    ESP_LOGI(kTag, "65XE on at %lld ms (SIO +5V %d mV)", msOf(now), sioMv);
                    break;
                case rb::host::SioPowerWatch::Event::None:
                    break;
                }
            }
        }
        if (g_tapeApplied) {
            // DATA IN between two frames (T7). Inert until T9: the gates stay closed (kTapeOutput).
            rb::host::DataInReadings readings;
            readings.gatesOpen = g_tape->deck().gatesOpen();
            readings.tapeOutput = kTapeOutput;
            readings.motorOn = g_motor != nullptr && g_motor->on();
            readings.commandHigh = g_line->commandHigh();
            readings.tapeOnC = g_tape->deck().loaded()[0] != '\0';
            readings.atariOff = g_atariOff;
            const rb::host::DataInChange change =
                g_arbiter.step(rb::host::dataInInputs(readings), static_cast<uint64_t>(now));
            if (change != rb::host::DataInChange::None) {
                ESP_LOGW(kTag, "DATA IN: cambio %d, ahora %s", static_cast<int>(change),
                         rb::host::dataInOwnerName(g_arbiter.owner()));
            }
        }
        if (poweredOnUs != 0 && framesSoFar() != framesAtPowerOn) {
            ESP_LOGI(kTag, "first COMMAND frame after power-on at %lld ms (%lld ms after +5V)", msOf(now),
                     msOf(now - poweredOnUs));
            poweredOnUs = 0;
        }
        if (g_firstFrameUs.load(std::memory_order_relaxed) == 0) {
            if (framesSoFar() > 0) {
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

// Network task (E2), created by finish() after D1 is served: reads the TNFS sources, publishes them to
// the SIO task, then keeps the Wi-Fi up. Never touches the SIO.
void netTask(void*)
{
    switch (rb::host::recoverSourcesFile(kSourcesPath)) {
    case rb::host::SourcesRecovery::Discarded:
        ESP_LOGW(kTag, "leftovers of an unfinished save of %s removed", kSourcesPath);
        break;
    case rb::host::SourcesRecovery::Restored:
        ESP_LOGW(kTag, "%s restored from its .bak (the last save did not finish)", kSourcesPath);
        break;
    case rb::host::SourcesRecovery::Failed:
        ESP_LOGE(kTag, "%s missing and its .bak could not be renamed", kSourcesPath);
        break;
    case rb::host::SourcesRecovery::Nothing:
        break;
    }
    std::FILE* file = std::fopen(kSourcesPath, "r");
    if (file == nullptr) {
        ESP_LOGW(kTag, "no %s: sources 2..8 empty", kSourcesPath);
    } else {
        char error[96] = {};
        if (!g_loadedSources.read(file, error, sizeof error)) {
            ESP_LOGW(kTag, "%s not read: %s", kSourcesPath, error);
        }
        std::fclose(file);
    }
    g_sourcesPublished.store(true, std::memory_order_release);  // also when empty: E may create the file
    runWifiStation(kWifiPath);
    vTaskDelete(nullptr);
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
    g_board = &board;
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
    // E2: the TNFS loader of the PC. Its thread (core 0) waits until a source 2..8 is opened; the links
    // get their servers once the network task has read the sources file. Without it the menu is E1.
    static rb::host::SystemNameResolver resolver;
    static rb::host::HostTnfsClock tnfsClock{g_clock};
    static rb::host::HeapImageMemory imageMemory;
    rb::host::TnfsLink* links[rb::host::TnfsLoader::kMaxSource + 1] = {};
    bool linksReady = true;
    for (unsigned n = 2; n <= rb::host::TnfsLoader::kMaxSource; ++n) {
        links[n] = createOnce<rb::host::UdpTnfsLink>("", n, resolver, g_events);
        linksReady = linksReady && links[n] != nullptr;
    }
    // menu-tnfs-persistente T4: the restore session's link (one more UDP socket, opened only by a
    // restore job). Without it the loader still runs; a restore then gives SERVIDOR NO ENCONTRADO.
    rb::host::TnfsLink* restoreLink =
        createOnce<rb::host::UdpTnfsLink>("", rb::host::TnfsLoader::kRestore, resolver, g_events);
    if (linksReady) {
        esp_pthread_cfg_t loaderThread = esp_pthread_get_default_config();
        loaderThread.stack_size = kLoaderStack;
        loaderThread.prio = 5;
        loaderThread.pin_to_core = 0;  // the SIO task owns core 1
        loaderThread.thread_name = "rb_tnfs";
        (void)esp_pthread_set_cfg(&loaderThread);
        g_loader = createOnce<rb::host::TnfsLoader>(links, g_repository.memory(), tnfsClock, imageMemory, nullptr,
                                                    rb::esp::SdCardStorage::kMountPoint, restoreLink);
        const esp_pthread_cfg_t defaults = esp_pthread_get_default_config();
        (void)esp_pthread_set_cfg(&defaults);
    }
    if (g_loader == nullptr || !g_loader->ready()) {
        ESP_LOGE(kTag, "no memory for the TNFS loader: sources 2..8 off");
        g_loader = nullptr;
    }

    g_station = createOnce<rb::host::MenuStation>(
        transport, g_clock, rb::host::MenuStationParts{*g_catalog, g_sources, g_repository, &g_events, g_loader});
    if (g_station == nullptr || !g_station->controller().ready()) {
        ESP_LOGE(kTag, "no memory for the menu");
        line.end();
        return false;
    }
    g_station->start();  // D1 = menu: the next boot of the Atari loads it
    g_station->controller().beginRestore();  // RESTAURANDO UNIDADES until finish() has read the microSD
    // menu-tnfs-persistente T6: the network restore asks the Wi-Fi before its first job and measures
    // its deadlines with the monotonic clock. Set before the SIO task exists.
    g_station->controller().setNetworkGate(&g_wifiGate);
    g_station->controller().setClock(&g_clock);
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

// The cassette (menu-cinta T6, plan §4 and R3): started after D1, with the free PSRAM logged before and
// after its bank is reserved. The deck is created even if the cassette cannot start: it then answers
// "no memory". Without the microSD there are no tapes: no deck, and C: says the cassette is missing.
static void startCassette(bool sdMounted, rb::esp::CassetteService& cassette, rb::esp::EspMotorIn& motor)
{
    g_motor = &motor;  // before publishTape(): the SIO task reads it only after it sees the deck
    if (!sdMounted || g_board == nullptr) {
        ESP_LOGW(kTag, "cas: sin microSD, la casetera no arranca");
        return;
    }
    const std::size_t freeBefore = heap_caps_get_free_size(MALLOC_CAP_SPIRAM);
    const std::size_t largestBefore = heap_caps_get_largest_free_block(MALLOC_CAP_SPIRAM);
    ESP_LOGI(kTag, "PSRAM libre antes de la casetera: %u B (bloque mayor %u B)", static_cast<unsigned>(freeBefore),
             static_cast<unsigned>(largestBefore));
    cassette.reportEnd(g_progressSink);  // before start(): the loop publishes from its first tick
    const bool started = cassette.start();
    ESP_LOGI(kTag, "PSRAM libre despues de la casetera: %u B (bloque mayor %u B); RAM interna libre %u B",
             static_cast<unsigned>(heap_caps_get_free_size(MALLOC_CAP_SPIRAM)),
             static_cast<unsigned>(heap_caps_get_largest_free_block(MALLOC_CAP_SPIRAM)),
             static_cast<unsigned>(heap_caps_get_free_size(MALLOC_CAP_INTERNAL)));
    g_tape = createOnce<EspTapeDeck>(cassette, *g_board);
    if (g_tape == nullptr) {
        ESP_LOGE(kTag, "cas: sin memoria para el deck; C: dice CASETERA NO DISPONIBLE");
        return;
    }
    if (!started) {
        ESP_LOGW(kTag, "cas: no arranca (falta PSRAM para el banco de %u B); montar una cinta dice SIN MEMORIA",
                 static_cast<unsigned>(rb::esp::CassetteService::kBankBytes));
    } else if (!g_tape->start()) {
        ESP_LOGW(kTag, "cas: tarea tape_cmd no creada; montar una cinta dice SIN MEMORIA");
    } else {
        ESP_LOGI(kTag, "cas: listo (puertas %s)", g_tape->deck().gatesOpen() ? "abiertas" : "cerradas");
    }
    g_startup.publishTape();  // before the plan: the SIO task applies it before the restore
}

void finish(bool sdMounted, const rb::hwdiag::BootRecord& boot, rb::esp::CassetteService& cassette,
            rb::esp::EspMotorIn& motor)
{
    g_boot = boot;
    if (g_station == nullptr) {
        return;  // start() failed: nothing is served
    }
    const char* failure = nullptr;  // why the drives are not restored: " MICROSD: <reason>"
    int failureError = 0;
    g_loaded = createOnce<rb::host::MenuCatalog>();
    if (g_loaded == nullptr) {
        ESP_LOGE(kTag, "no memory for the catalog: the menu lists no images");
        failure = "SIN RAM";
        failureError = ENOMEM;
    } else if (!sdMounted) {
        ESP_LOGW(kTag, "microSD not mounted: the menu lists no images");
        failure = "NO MONTADA";
        failureError = ENODEV;
    } else {
        errno = 0;
        if (rb::host::readCatalog(rb::esp::SdCardStorage::kMountPoint, *g_loaded)) {
            g_catalogPublished.store(true, std::memory_order_release);  // the SIO task applies it
        } else if (errno == ENOMEM) {
            ESP_LOGE(kTag, "microSD catalog not read: no memory for the 4 KB path buffer");
            failure = "SIN RAM";
            failureError = ENOMEM;
        } else {
            failureError = errno != 0 ? errno : EIO;
            ESP_LOGW(kTag, "microSD catalog not read (%s): the menu lists no images", std::strerror(failureError));
            failure = rb::host::storageReason(failureError);
        }
    }

    // Drive memory: the slots are read once, here. Saving needs that read to give Loaded or Empty and
    // rb_drive_save to run; otherwise it stays off for this session (a save could replace E0).
    // A pointer into the store (heap): no copy of the ~1 KB result on app_main's 3584 B stack.
    const rb::host::SlotFileStore::LoadResult* loaded = nullptr;
    bool saverReady = false;
    if (failure == nullptr) {
        g_store = createOnce<rb::host::SlotFileStore>(rb::esp::SdCardStorage::kMountPoint);
        if (g_store == nullptr) {
            ESP_LOGE(kTag, "no memory for the drive memory store: drives neither restored nor saved");
        }
    }
    if (g_store != nullptr) {
        const int64_t began = esp_timer_get_time();
        loaded = &g_store->load();
        if (loaded->status == rb::host::SlotFileStore::LoadStatus::Error) {
            ESP_LOGW(kTag, "drive memory not read: %s (errno %d); saving off this session",
                     rb::host::storageReason(loaded->error), loaded->error);
        } else {
            ESP_LOGI(kTag, "drive memory read in %lld ms: %s", msOf(esp_timer_get_time() - began),
                     loaded->status == rb::host::SlotFileStore::LoadStatus::Loaded ? "slot found" : "no valid slot");
            g_saveRequests = xQueueCreate(1, sizeof(SaveRequest));
            g_saveOutcomes = xQueueCreate(1, sizeof(SaveOutcome));
            saverReady = g_saveRequests != nullptr && g_saveOutcomes != nullptr &&
                         xTaskCreatePinnedToCore(driveSaveTask, "rb_drive_save", kSaveTaskStack, nullptr,
                                                 tskIDLE_PRIORITY + 2, nullptr, 0) == pdPASS;
            if (!saverReady) {
                ESP_LOGE(kTag, "drive memory save task not created: changes are NOT saved");
            }
        }
    }
    // menu-cinta T6: the deck is published before the plan, so the restore of C: finds it.
    startCassette(sdMounted, cassette, motor);

    // rb_drive_save touches the store only after the plan is published, so *loaded is still this read.
    rb::host::planDriveMemory(failure, failureError, loaded, saverReady, g_plan);
    g_startup.publishPlan();  // after the deck, g_plan, the queues and the task

    if (sdMounted && xTaskCreatePinnedToCore(netTask, "rb_net", kNetTaskStack, nullptr, tskIDLE_PRIORITY + 2, nullptr,
                                             0) != pdPASS) {
        ESP_LOGW(kTag, "network task not created: sources 2..8 off");
    }
    if (!registerCommand("menu","menu: D1 vuelve al menu (luego apaga y enciende el 65XE)", menuCommand) ||
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
