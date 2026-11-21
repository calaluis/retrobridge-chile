#pragma once

// rb_esp_cassette (cassette-cas C106): the cassette on the ESP32.
//
// - EspMotorIn reads SIO pin 8 (MOTOR) on its input-only GPIO, and only once
//   validate_for_motor() grants it (M3a signed). Otherwise MOTOR reads as off.
// - CassetteService owns the tape files on the microSD, the FSK loader, the player and the
//   `cas` commands. Two tasks: the cassette loop (commands and CassettePlayer::tick(), the
//   same task as CassetteCommands requires) and the loader's producer (FskLoader::step()).
// - Until H4 is approved no SIO pin is an output: the player plays into DiscardByteOut and
//   DiscardWaveOut. The tape moves with MOTOR, so `cas status` follows a load attempt, but
//   DATA IN gets nothing (C107 brings UART and RMT).
//
// Memory (ADR-011): the loader bank and the two segment tables are reserved once in PSRAM by
// start(); nothing is allocated afterwards.

#include <cstddef>
#include <cstdint>

#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "freertos/semphr.h"
#include "freertos/task.h"
#include "rb/board/board_profile.hpp"
#include "rb/cassette/cassette_commands.hpp"
#include "rb/cassette/cassette_player.hpp"
#include "rb/cassette/discard_outputs.hpp"
#include "rb/cassette/fsk_loader.hpp"
#include "rb/cassette/stdio_tape_files.hpp"

namespace rb::esp {

class EspMotorIn final : public rb::cassette::MotorIn {
public:
    // Configures the MOTOR GPIO as a plain input (no pull) if the profile grants it.
    // Returns whether MOTOR is read; false leaves every GPIO untouched.
    bool configure(const rb::board::BoardProfile& profile) noexcept;
    bool enabled() const noexcept { return pin_ >= 0; }
    bool on() const override;

private:
    int8_t pin_ = -1;
};

class CassetteService {
public:
    static constexpr std::size_t kBankBytes = 1024u * 1024u;  // the longest real run is ~0.5 MB
    static constexpr std::size_t kSegments = 1024;            // per table
    static constexpr std::size_t kLineMax = 160;
    static constexpr std::size_t kReplyMax = 160;

    // `root`: prefix for tape paths, e.g. "/sd/".
    CassetteService(const char* root, EspMotorIn& motor) noexcept;

    // Reserves the memory and starts both tasks. False if PSRAM or the tasks are missing.
    bool start() noexcept;
    bool started() const noexcept { return commands_ != nullptr; }
    bool motorEnabled() const noexcept { return motor_.enabled(); }

    // Console task: runs `cas <args>` on the cassette loop and copies the reply.
    // False if not started or no reply in time.
    bool request(const char* args, char* reply, std::size_t replySize) noexcept;

private:
    struct Request {
        uint32_t sequence;
        char line[kLineMax];
    };
    struct Reply {
        uint32_t sequence;
        char text[kReplyMax];
    };

    static void loopTask(void* self);
    static void producerTask(void* self);
    void loop() noexcept;
    void produce() noexcept;
    void release() noexcept;

    EspMotorIn& motor_;
    rb::cassette::StdioTapeFiles files_;
    rb::cassette::FskLoader loader_;
    rb::cassette::CassettePlayer player_{loader_};
    rb::cassette::DiscardByteOut bytes_;
    rb::cassette::DiscardWaveOut wave_;
    alignas(rb::cassette::CassetteCommands) unsigned char commandsStorage_[sizeof(rb::cassette::CassetteCommands)];
    rb::cassette::CassetteCommands* commands_ = nullptr;

    // One caller at a time. Requests and replies travel by copy in queues of one, numbered,
    // so a late reply to a caller that gave up is never taken for the next one's.
    SemaphoreHandle_t callers_ = nullptr;
    QueueHandle_t requests_ = nullptr;
    QueueHandle_t replies_ = nullptr;
    uint32_t sequence_ = 0;
    TaskHandle_t producer_ = nullptr;
    bool attempted_ = false;
    uint8_t* bank_ = nullptr;
    rb::cassette::CasSegment* tableA_ = nullptr;
    rb::cassette::CasSegment* tableB_ = nullptr;
};

}  // namespace rb::esp
