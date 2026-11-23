#include "esp_tape_deck.hpp"

#include <cstdio>

namespace rb::menusio {

namespace {

constexpr uint32_t kTaskStackBytes = 3072;  // two copies of a 160-byte line plus request()'s own
constexpr UBaseType_t kTaskPriority = 3;     // below cas_load (4) and cas_loop (5)

}  // namespace

bool EspCasChannel::start() noexcept
{
    if (worker_ != nullptr) {
        return true;
    }
    lines_ = xQueueCreate(1, sizeof(Line));
    replies_ = xQueueCreate(1, sizeof(Reply));
    if (lines_ == nullptr || replies_ == nullptr) {
        return false;
    }
    if (xTaskCreate(&EspCasChannel::task, "tape_cmd", kTaskStackBytes, this, kTaskPriority, &worker_) != pdPASS) {
        worker_ = nullptr;
        return false;
    }
    return true;
}

bool EspCasChannel::send(const char* args) noexcept
{
    if (inFlight_ || lines_ == nullptr) {
        return false;
    }
    Line line{};
    std::snprintf(line.text, sizeof line.text, "%s", args == nullptr ? "" : args);
    if (xQueueSend(lines_, &line, 0) != pdTRUE) {
        return false;
    }
    inFlight_ = true;
    return true;
}

bool EspCasChannel::receive(char* reply, std::size_t size) noexcept
{
    if (!inFlight_ || replies_ == nullptr || reply == nullptr || size == 0) {
        return false;
    }
    Reply answer{};
    if (xQueueReceive(replies_, &answer, 0) != pdTRUE) {
        return false;
    }
    inFlight_ = false;
    std::snprintf(reply, size, "%s", answer.text);
    return true;
}

void EspCasChannel::task(void* self)
{
    static_cast<EspCasChannel*>(self)->run();
}

void EspCasChannel::run() noexcept
{
    Line line{};
    Reply answer{};
    for (;;) {
        if (xQueueReceive(lines_, &line, portMAX_DELAY) != pdTRUE) {
            continue;
        }
        answer.text[0] = '\0';  // no reply in time: CasTapeDeck asks "status"
        if (!service_.request(line.text, answer.text, sizeof answer.text)) {
            answer.text[0] = '\0';
        }
        (void)xQueueSend(replies_, &answer, portMAX_DELAY);
    }
}

EspTapeDeck::EspTapeDeck(rb::esp::CassetteService& service, const rb::board::BoardProfile& board) noexcept
    : channel_(service), deck_(channel_, rb::board::validate_for_cassette_output(board).ok())
{
}

}  // namespace rb::menusio
