// Bench C8a, phase F1 (docs/hardware/banco-c8a-aislado.md): the console order `c8a run`. Built only with
// CONFIG_RB_BENCH_C8. It reads the SIO +5 V on GPIO35 (ADC1 channel 7, an input-only pin: nothing is driven),
// asks the gate (main/bench_c8_gate.hpp) and, only on Answer::Run, runs bench C8 once. Never at boot (C-2).
#include <cstring>

#include "bench_c8.hpp"
#include "bench_c8_gate.hpp"
#include "esp_adc/adc_cali.h"
#include "esp_adc/adc_cali_scheme.h"
#include "esp_adc/adc_oneshot.h"
#include "esp_console.h"
#include "esp_log.h"
#include "sdkconfig.h"

namespace rb::bench {
namespace {

constexpr const char* kTag = "c8a";
constexpr adc_channel_t kSio5vChannel = ADC_CHANNEL_7;  // GPIO35: SIO +5 V (pin 10) through R4 2 k / R5 3.9 k

const rb::board::BoardProfile* g_profile = nullptr;
rb::esp::EspDataIn* g_dataIn = nullptr;
adc_oneshot_unit_handle_t g_adc = nullptr;
adc_cali_handle_t g_adcCali = nullptr;
C8Trigger g_trigger;

// ADC1 on GPIO35, as menu-sio reads it. False leaves g_adc null: every sample then reads as failed (Invalid).
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
        (void)adc_oneshot_del_unit(g_adc);
        g_adc = nullptr;
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
        g_adcCali = nullptr;  // uncalibrated: raw * 3300 / 4095, as menu-sio
        ESP_LOGW(kTag, "SIO +5V: ADC without calibration");
    }
    return true;
}

// One sample per read, not an average: the gate needs every reading to judge their spread.
void readSamples(Sio5vSample (&samples)[kSio5vSamples])
{
    for (auto& s : samples) {
        s = Sio5vSample{};
        int raw = 0;
        if (g_adc == nullptr || adc_oneshot_read(g_adc, kSio5vChannel, &raw) != ESP_OK) {
            continue;  // ok stays false
        }
        int mv = raw * 3300 / 4095;
        if (g_adcCali != nullptr && adc_cali_raw_to_voltage(g_adcCali, raw, &mv) != ESP_OK) {
            continue;
        }
        s = Sio5vSample{true, mv};
    }
}

const char* name(Sio5v v)
{
    switch (v) {
    case Sio5v::Absent: return "ausente";
    case Sio5v::Present: return "PRESENTE";
    default: return "LECTURA NO VALIDA";
    }
}

int c8aCommand(int argc, char** argv)
{
    if (argc != 2 || std::strcmp(argv[1], "run") != 0) {
        printf("uso: c8a run (banco C8a: solo USB, cable SIO desconectado; una vez por arranque)\n");
        return 1;
    }
    if (g_profile == nullptr || g_dataIn == nullptr) {
        ESP_LOGE(kTag, "C8A REFUSED: not started");
        return 1;
    }
    Sio5vSample samples[kSio5vSamples];
    readSamples(samples);
    for (std::size_t i = 0; i < kSio5vSamples; ++i) {
        ESP_LOGI(kTag, "SIO +5V sample %u: %s %d mV (pin)", static_cast<unsigned>(i),
                 samples[i].ok ? "ok" : "FAILED", samples[i].pinMv);
    }
    const Sio5v sio5v = classifySio5v(samples, kSio5vSamples);
    ESP_LOGI(kTag, "SIO +5V: %s", name(sio5v));
    switch (g_trigger.request(sio5v)) {
    case C8Trigger::Answer::Run:
        break;
    case C8Trigger::Answer::RefusedSio5v:
        ESP_LOGE(kTag, "C8A REFUSED: SIO +5V present. Disconnect the SIO cable and run c8a run again");
        return 1;
    case C8Trigger::Answer::RefusedInvalid:
        ESP_LOGE(kTag, "C8A REFUSED: the SIO +5V reading is not valid; GPIO21 not touched");
        return 1;
    case C8Trigger::Answer::AlreadyRan:
    default:
        ESP_LOGE(kTag, "C8A REFUSED: bench already ran this boot; power cycle to run it again");
        return 1;
    }
    const bool pass = run_c8(*g_profile, *g_dataIn);
    return pass ? 0 : 1;
}

}  // namespace

bool start_c8_command(const rb::board::BoardProfile& profile, rb::esp::EspDataIn& dataIn) noexcept
{
    g_profile = &profile;
    g_dataIn = &dataIn;
    if (!startAdc()) {
        ESP_LOGE(kTag, "SIO +5V: ADC not available; c8a run will refuse");
    }
    esp_console_cmd_t command{};
    command.command = "c8a";
    command.help = "c8a run: banco C8a (solo USB, cable SIO desconectado), una vez por arranque";
    command.func = c8aCommand;
    if (esp_console_cmd_register(&command) != ESP_OK) {
        ESP_LOGE(kTag, "c8a command not registered");
        return false;
    }
    ESP_LOGI(kTag, "C8A READY: type c8a run (USB only, SIO cable disconnected)");
    return true;
}

}  // namespace rb::bench
