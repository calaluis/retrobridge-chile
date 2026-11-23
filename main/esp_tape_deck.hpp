#pragma once

// EspTapeDeck (spec menu-cinta, plan §2.1, T5): the menu's tape deck on the ESP32.
//
// CassetteService::request() waits for the cassette loop (up to 2 s: a mount indexes the tape), and
// the menu runs on the SIO task, which must never wait that long. So a small task ("tape_cmd") does
// the waiting: the SIO task posts one `cas` line into a queue of one and later takes the reply from
// another queue of one, both without blocking (EspCasChannel). CasTapeDeck (rb_menu, tested on the
// PC) turns those replies into the TapeDeck states the menu shows.
//
// The gates (AC8) come from the board profile: validate_for_cassette_output() grants DATA IN only
// with H4 and M3a. Checking it touches no GPIO. Without them a tape still goes on C: but never plays.
//
// Memory: the two queues and the task stack are created once in start(); nothing afterwards.

#include <cstddef>

#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "freertos/task.h"
#include "rb/board/board_profile.hpp"
#include "rb/esp/cassette_service.hpp"
#include "rb/menu/cas_tape_deck.hpp"

namespace rb::menusio {

class EspCasChannel final : public rb::host::CasChannel {
public:
    static constexpr std::size_t kLineMax = rb::esp::CassetteService::kLineMax;
    static constexpr std::size_t kReplyMax = rb::esp::CassetteService::kReplyMax;

    explicit EspCasChannel(rb::esp::CassetteService& service) noexcept : service_(service) {}

    // Creates the queues and the task. False if they cannot be created; the channel then stays
    // unavailable and every mount answers NoMemory.
    bool start() noexcept;

    bool available() const noexcept override { return worker_ != nullptr && service_.started(); }
    bool send(const char* args) noexcept override;
    bool receive(char* reply, std::size_t size) noexcept override;

private:
    struct Line {
        char text[kLineMax];
    };
    struct Reply {
        char text[kReplyMax];
    };

    static void task(void* self);
    void run() noexcept;

    rb::esp::CassetteService& service_;
    QueueHandle_t lines_ = nullptr;
    QueueHandle_t replies_ = nullptr;
    TaskHandle_t worker_ = nullptr;
    bool inFlight_ = false;  // touched by the SIO task only
};

class EspTapeDeck {
public:
    EspTapeDeck(rb::esp::CassetteService& service, const rb::board::BoardProfile& board) noexcept;

    bool start() noexcept { return channel_.start(); }

    // The TapeDeck the menu is given (MenuController::setTapeDeck). Call deck().pump() every SIO step.
    rb::host::CasTapeDeck& deck() noexcept { return deck_; }

private:
    EspCasChannel channel_;
    rb::host::CasTapeDeck deck_;
};

}  // namespace rb::menusio
