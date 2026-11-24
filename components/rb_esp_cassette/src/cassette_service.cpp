#include "rb/esp/cassette_service.hpp"

#include <cstdio>
#include <new>

#include "driver/gpio.h"
#include "esp_heap_caps.h"
#include "esp_log.h"
#include "esp_timer.h"

namespace rb::esp {

namespace {

constexpr const char* kTag = "cassette";
constexpr TickType_t kLoopPeriod = pdMS_TO_TICKS(2);
constexpr TickType_t kIdlePeriod = pdMS_TO_TICKS(10);
constexpr TickType_t kReplyTimeout = pdMS_TO_TICKS(2000);
constexpr uint32_t kLoopStackBytes = 8192;      // mount: paths, stdio, VFS and FATFS
constexpr uint32_t kProducerStackBytes = 6144;  // stdio, VFS and FATFS

}  // namespace

bool EspMotorIn::configure(const rb::board::BoardProfile& profile) noexcept
{
    const auto granted = rb::board::validate_for_motor(profile);
    if (!granted.ok()) {
        return false;  // M3a not signed: GPIO36 is not touched
    }
    const int8_t pin = granted.value().pin();
    gpio_config_t config{};
    config.pin_bit_mask = 1ULL << pin;
    config.mode = GPIO_MODE_INPUT;  // input only: MOTOR is never driven
    config.pull_up_en = GPIO_PULLUP_DISABLE;
    config.pull_down_en = GPIO_PULLDOWN_DISABLE;
    config.intr_type = GPIO_INTR_DISABLE;
    if (gpio_config(&config) != ESP_OK) {
        return false;
    }
    pin_ = pin;
    return true;
}

bool EspMotorIn::on() const
{
    return pin_ >= 0 && gpio_get_level(static_cast<gpio_num_t>(pin_)) == 1;
}

CassetteService::CassetteService(const char* root, EspMotorIn& motor, EspDataIn& dataIn) noexcept
    : motor_(motor), dataIn_(dataIn), files_(root)
{
}

void CassetteService::release() noexcept
{
    if (producer_ != nullptr) {
        vTaskDelete(producer_);
        producer_ = nullptr;
    }
    if (requests_ != nullptr) {
        vQueueDelete(requests_);
        requests_ = nullptr;
    }
    if (replies_ != nullptr) {
        vQueueDelete(replies_);
        replies_ = nullptr;
    }
    if (callers_ != nullptr) {
        vSemaphoreDelete(callers_);
        callers_ = nullptr;
    }
    heap_caps_free(bank_);
    heap_caps_free(tableA_);
    heap_caps_free(tableB_);
    bank_ = nullptr;
    tableA_ = nullptr;
    tableB_ = nullptr;
}

bool CassetteService::start() noexcept
{
    if (started() || attempted_) {
        return started();  // once: a failed start is not retried
    }
    attempted_ = true;
    const uint32_t caps = MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT;
    const std::size_t tableBytes = kSegments * sizeof(rb::cassette::CasSegment);
    bank_ = static_cast<uint8_t*>(heap_caps_malloc(kBankBytes, caps));
    tableA_ = static_cast<rb::cassette::CasSegment*>(heap_caps_malloc(tableBytes, caps));
    tableB_ = static_cast<rb::cassette::CasSegment*>(heap_caps_malloc(tableBytes, caps));
    callers_ = xSemaphoreCreateMutex();
    requests_ = xQueueCreate(1, sizeof(Request));
    replies_ = xQueueCreate(1, sizeof(Reply));
    if (bank_ == nullptr || tableA_ == nullptr || tableB_ == nullptr || callers_ == nullptr || requests_ == nullptr ||
        replies_ == nullptr) {
        ESP_LOGE(kTag, "no PSRAM for the cassette (bank %u bytes): cassette disabled",
                 static_cast<unsigned>(kBankBytes));
        release();
        return false;
    }
    for (std::size_t i = 0; i < kSegments; ++i) {
        new (&tableA_[i]) rb::cassette::CasSegment{};
        new (&tableB_[i]) rb::cassette::CasSegment{};
    }
    loader_.bind(rb::core::Span<uint8_t>(bank_, kBankBytes));
    if (dataIn_.enabled() && !dataIn_.shared()) {
        // Exclusive (H4 and M3a signed, no disk on UART2): the cassette owns DATA IN from the start.
        outputs_.setLive(dataIn_, dataIn_);
        (void)outputs_.select(true, player_);  // no tape loaded yet: nothing to move
        tick_.pump(dataIn_);
        driving_ = true;
    } else if (dataIn_.enabled() && lender_ != nullptr) {
        // Shared (menu-cinta T9a.7): live only once the SIO task lends DATA IN (plan §2.4 steps 3 and 6).
        outputs_.setLive(dataIn_, dataIn_);
        tick_.lend(*lender_, dataIn_);
        driving_ = true;
    } else if (dataIn_.enabled()) {
        ESP_LOGE(kTag, "DATA IN shared with the disk but nobody lends it: not driven");
    }
    commands_ = new (commandsStorage_) rb::cassette::CassetteCommands(
        player_, loader_, files_, rb::core::Span<rb::cassette::CasSegment>(tableA_, kSegments),
        rb::core::Span<rb::cassette::CasSegment>(tableB_, kSegments));
    if (xTaskCreate(&CassetteService::producerTask, "cas_load", kProducerStackBytes, this, 4, &producer_) != pdPASS) {
        producer_ = nullptr;
        commands_ = nullptr;
        release();
        ESP_LOGE(kTag, "cassette tasks could not start");
        return false;
    }
    if (xTaskCreate(&CassetteService::loopTask, "cas_loop", kLoopStackBytes, this, 5, nullptr) != pdPASS) {
        commands_ = nullptr;
        release();  // also stops the producer
        ESP_LOGE(kTag, "cassette tasks could not start");
        return false;
    }
    ESP_LOGI(kTag, "cassette ready: MOTOR %s, DATA IN %s",
             motor_.enabled() ? "on GPIO input" : "not read (M3a not signed)",
             driving_ ? "driven (UART and RMT)" : "not driven (H4 or M3a not signed)");
    return true;
}

bool CassetteService::request(const char* args, char* reply, std::size_t replySize) noexcept
{
    if (!started() || reply == nullptr || replySize == 0) {
        return false;
    }
    if (xSemaphoreTake(callers_, kReplyTimeout) != pdTRUE) {
        return false;
    }
    Request request{};
    request.sequence = ++sequence_;
    std::snprintf(request.line, sizeof request.line, "%s", args == nullptr ? "" : args);
    bool answered = false;
    if (xQueueSend(requests_, &request, kReplyTimeout) == pdTRUE) {
        // A reply to an earlier request that timed out may still arrive first: skip it.
        const TickType_t start = xTaskGetTickCount();
        Reply answer{};
        while (!answered && xTaskGetTickCount() - start < kReplyTimeout) {
            if (xQueueReceive(replies_, &answer, kReplyTimeout - (xTaskGetTickCount() - start)) != pdTRUE) {
                break;
            }
            answered = answer.sequence == request.sequence;
        }
        if (answered) {
            std::snprintf(reply, replySize, "%s", answer.text);
        }
    }
    xSemaphoreGive(callers_);
    return answered;
}

void CassetteService::loopTask(void* self)
{
    static_cast<CassetteService*>(self)->loop();
}

void CassetteService::producerTask(void* self)
{
    static_cast<CassetteService*>(self)->produce();
}

void CassetteService::loop() noexcept
{
    Request request{};
    Reply reply{};
    for (;;) {
        // The request is a copy: a caller that gave up cannot change it underneath.
        if (xQueueReceive(requests_, &request, 0) == pdTRUE) {
            commands_->run(request.line, motor_, outputs_.wave(), rb::core::Span<char>(reply.text, sizeof reply.text));
            reply.sequence = request.sequence;
            (void)xQueueOverwrite(replies_, &reply);  // a reply nobody waits for is replaced
        }
        tick_.tick(static_cast<uint64_t>(esp_timer_get_time()));  // play, pump, publish, answer the lender
        vTaskDelay(kLoopPeriod);
    }
}

void CassetteService::produce() noexcept
{
    uint32_t steps = 0;
    for (;;) {
        if (loader_.state() != rb::cassette::LoaderState::Loading) {
            vTaskDelay(kIdlePeriod);
            continue;
        }
        (void)loader_.step();
        if (++steps % 16 == 0) {
            vTaskDelay(1);  // let the idle task run (task watchdog)
        }
    }
}

}  // namespace rb::esp
