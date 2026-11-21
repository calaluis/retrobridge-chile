#include "bench_c8.hpp"

#include <cstdint>

#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "rb/cassette/fsk_wave.hpp"
#include "soc/gpio_reg.h"
#include "soc/gpio_sig_map.h"
#include "soc/soc.h"

namespace rb::bench {

namespace {

constexpr const char* kTag = "c8";
constexpr TickType_t kTick = pdMS_TO_TICKS(2);  // the cassette loop's period

enum class Owner { Released, Uart2, Rmt, Other };

const char* owner_name(Owner owner)
{
    switch (owner) {
        case Owner::Released: return "released (output off)";
        case Owner::Uart2: return "UART2 TX";
        case Owner::Rmt: return "RMT";
        default: return "other";
    }
}

uint32_t g_calls = 0;
uint32_t g_failedCalls = 0;

void trace(const char* call, int err)
{
    ++g_calls;
    if (err != 0) {
        ++g_failedCalls;
        ESP_LOGE(kTag, "C8 call %s -> %d (FAIL)", call, err);
    } else {
        ESP_LOGI(kTag, "C8 call %s -> 0", call);
    }
}

// What drives the pin, read from the GPIO matrix: output signal and output enable.
Owner route(int pin, uint32_t& signal, bool& enabled)
{
    signal = REG_GET_FIELD(GPIO_FUNC0_OUT_SEL_CFG_REG + 4u * static_cast<uint32_t>(pin), GPIO_FUNC0_OUT_SEL);
    enabled = ((REG_READ(GPIO_ENABLE_REG) >> pin) & 1u) != 0;
    if (!enabled) {
        return Owner::Released;
    }
    if (signal == U2TXD_OUT_IDX) {
        return Owner::Uart2;
    }
    if (signal >= RMT_SIG_OUT0_IDX && signal <= RMT_SIG_OUT7_IDX) {
        return Owner::Rmt;
    }
    return Owner::Other;
}

bool expect(const char* step, int pin, Owner want)
{
    uint32_t signal = 0;
    bool enabled = false;
    const Owner got = route(pin, signal, enabled);
    const bool pass = got == want;
    if (pass) {
        ESP_LOGI(kTag, "C8 [PASS] %s: GPIO%d out_sel=%u oe=%d -> %s", step, pin, static_cast<unsigned>(signal),
                 enabled ? 1 : 0, owner_name(got));
    } else {
        ESP_LOGE(kTag, "C8 [FAIL] %s: GPIO%d out_sel=%u oe=%d -> %s, expected %s", step, pin,
                 static_cast<unsigned>(signal), enabled ? 1 : 0, owner_name(got), owner_name(want));
    }
    return pass;
}

bool check(const char* step, bool ok)
{
    if (ok) {
        ESP_LOGI(kTag, "C8 [PASS] %s", step);
    } else {
        ESP_LOGE(kTag, "C8 [FAIL] %s", step);
    }
    return ok;
}

// Queues `count` portions of `ticks` us, alternating space and mark (a square wave).
bool queue_run(rb::esp::EspDataIn& dataIn, uint32_t count, uint32_t ticks)
{
    for (uint32_t i = 0; i < count; ++i) {
        if (!dataIn.push(rb::cassette::FskPortion{(i & 1u) != 0, ticks})) {
            return false;
        }
    }
    return true;
}

// Pumps every tick, as the cassette loop does, until `done` or `ticks` ticks.
template <typename Done>
bool pump_until(rb::esp::EspDataIn& dataIn, Done done, uint32_t ticks)
{
    for (uint32_t i = 0; i < ticks; ++i) {
        dataIn.pump();
        if (done()) {
            return true;
        }
        vTaskDelay(kTick);
    }
    return false;
}

}  // namespace

bool run_c8(const rb::board::BoardProfile& profile, rb::esp::EspDataIn& dataIn) noexcept
{
    ESP_LOGW(kTag, "C8 BENCH BEGIN: board '%s', H4 %s, M3a %s; USB only, the Atari must be disconnected",
             profile.name, profile.sioGateApproved ? "signed" : "not signed",
             profile.motorGateApproved ? "signed" : "not signed");
    const auto grant = rb::board::validate_for_bench_data_in(profile);
    if (!grant.ok()) {
        ESP_LOGE(kTag, "C8 RESULT FAIL: no bench grant for DATA IN (error %d)", static_cast<int>(grant.error()));
        ESP_LOGW(kTag, "C8 BENCH END");
        return false;
    }
    const int pin = grant.value().dataIn();
    bool ok = true;
    g_calls = 0;
    g_failedCalls = 0;
    dataIn.setTrace(&trace);

    ok &= expect("0 before configure", pin, Owner::Released);
    ok &= check("1 configure (UART2 on DATA IN)", dataIn.configure(grant.value()));
    ok &= expect("1 configured", pin, Owner::Uart2);

    const uint8_t bytes[4] = {0x55, 0x55, 0x55, 0x55};
    ok &= check("2 four bytes by UART2 at 600 baud", dataIn.send(rb::core::Span<const uint8_t>(bytes, 4)));
    vTaskDelay(pdMS_TO_TICKS(100));
    ok &= expect("2 after the bytes", pin, Owner::Uart2);

    // Run A: 1500 portions of 200 us (0.3 s), played to its end.
    ok &= check("3 run A queued (1500 x 200 us)", queue_run(dataIn, 1500, 200));
    ok &= check("3 run A started", pump_until(dataIn, [&] { return dataIn.fskActive(); }, 50));
    ok &= expect("3 run A playing", pin, Owner::Rmt);
    ok &= check("4 run A played to its end",
                pump_until(dataIn, [&] { return dataIn.idle() && !dataIn.fskActive(); }, 1500));
    ok &= expect("4 run A ended", pin, Owner::Uart2);

    // Run B: 4000 portions of 250 us (1 s), cancelled after about 100 ms, as a MOTOR drop would.
    ok &= check("5 run B queued (4000 x 250 us)", queue_run(dataIn, 4000, 250));
    ok &= check("5 run B started", pump_until(dataIn, [&] { return dataIn.fskActive(); }, 50));
    (void)pump_until(dataIn, [] { return false; }, 50);
    ok &= expect("5 run B playing", pin, Owner::Rmt);
    dataIn.stop();
    ok &= check("6 run B cancelled with stop()", dataIn.idle() && !dataIn.fskActive());
    ok &= expect("6 run B cancelled", pin, Owner::Uart2);

    ok &= check("7 no fault (faulted, pinFaults, failures)",
                !dataIn.faulted() && dataIn.pinFaults() == 0 && dataIn.failures() == 0);
    ESP_LOGI(kTag, "C8 counters: faulted=%d pinFaults=%u failures=%u underruns=%u", dataIn.faulted() ? 1 : 0,
             static_cast<unsigned>(dataIn.pinFaults()), static_cast<unsigned>(dataIn.failures()),
             static_cast<unsigned>(dataIn.underruns()));

    dataIn.release();
    ok &= check("8 released (driver disabled)", !dataIn.enabled());
    ok &= expect("8 released", pin, Owner::Released);
    dataIn.setTrace(nullptr);

    ESP_LOGI(kTag, "C8 calls: %u traced, %u failed", static_cast<unsigned>(g_calls),
             static_cast<unsigned>(g_failedCalls));
    ok &= g_failedCalls == 0;
    if (ok) {
        ESP_LOGI(kTag, "C8 RESULT PASS: GPIO21 UART2 -> RMT -> UART2 (end and stop), then released");
    } else {
        ESP_LOGE(kTag, "C8 RESULT FAIL: see the [FAIL] lines above");
    }
    ESP_LOGW(kTag, "C8 BENCH END (electrical check of SIO pin 3 still pending)");
    return ok;
}

}  // namespace rb::bench
