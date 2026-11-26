#pragma once

// rb_esp_cassette (cassette-cas C106): the cassette on the ESP32.
//
// - EspMotorIn reads SIO pin 8 (MOTOR) on its input-only GPIO, and only once
//   validate_for_motor() grants it (M3a signed). Otherwise MOTOR reads as off.
// - CassetteService owns the tape files on the microSD, the FSK loader, the player and the
//   `cas` commands. Two tasks: the cassette loop (commands and CassettePlayer::tick(), the
//   same task as CassetteCommands requires) and the loader's producer (FskLoader::step()).
// - DATA IN (C107): with EspDataIn configured (H4 and M3a signed, validate_for_cassette_output)
//   the player plays into it: records by UART, FSK by RMT, and the loop pumps it every tick.
//   Otherwise the player plays into DiscardByteOut and DiscardWaveOut: the tape moves with
//   MOTOR, so `cas status` follows a load attempt, but DATA IN gets nothing.
// - menu-cinta T9a.7: the loop runs CassetteTick (PlayerOutputs, GatedMotor). With EspDataIn in shared
//   mode (UART2 is the disk's) DATA IN is lent by the SIO task through a DataInLender: the player
//   plays live only between a confirmed Tape and the next release; the tape does not move otherwise.
//   Shared mode with no lender: never driven. An EndSink, when given, gets "the tape played to its
//   end" every tick.
//
// - cassette-motor-fujinet (variant RB_CAS_ENGINE_FUJINET, experimental): when DATA IN is driven
//   exclusively, the loop plays the tape with the owner's FujiNet FSK engine (EspFskEngine) through
//   FujiTapeRunner instead of CassetteTick. The gates are the same: nothing plays unless driving_ is
//   true in exclusive mode. Shared mode keeps CassetteTick.
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
#include "rb/cassette/cassette_tick.hpp"
#include "rb/cassette/discard_outputs.hpp"
#include "rb/cassette/fsk_loader.hpp"
#include "rb/cassette/stdio_tape_files.hpp"
#include "rb/esp/esp_data_in.hpp"
#include "rb/fujinet/esp_fsk_engine.hpp"
#include "rb/fujinet/fuji_tape_runner.hpp"

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

    // `root`: prefix for tape paths, e.g. "/sd/". `dataIn` is used only if it is enabled when
    // start() runs; the choice is fixed from then on.
    CassetteService(const char* root, EspMotorIn& motor, EspDataIn& dataIn) noexcept;

    // Before start(). Shared mode: who lends DATA IN (the SIO task, through DataInHandover).
    void lendDataIn(rb::cassette::DataInLender& lender) noexcept { lender_ = &lender; }
    // Before start() (menu-cinta T9a.7b): DATA IN on UART2 as the disk installed it (EspDataIn shared
    // mode, `txRing` its TX ring), lent by `lender`. False: not shared, the cassette never drives DATA IN.
    bool shareDataIn(const rb::board::CassetteOutputAssignment& pins, std::size_t txRing,
                     rb::cassette::DataInLender& lender) noexcept
    {
        if (!dataIn_.attachShared(pins, txRing)) {
            return false;
        }
        lender_ = &lender;
        return true;
    }
    // Before start(): where "the tape played to its end" goes every tick (TapeProgress).
    void reportEnd(rb::cassette::EndSink& sink) noexcept { tick_.report(sink); }

    // Reserves the memory and starts both tasks. False if PSRAM or the tasks are missing.
    bool start() noexcept;
    bool started() const noexcept { return commands_ != nullptr; }
    bool motorEnabled() const noexcept { return motor_.enabled(); }
    bool outputEnabled() const noexcept { return driving_; }

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
    EspDataIn& dataIn_;
    rb::cassette::StdioTapeFiles files_;
    rb::cassette::FskLoader loader_;
    rb::cassette::CassettePlayer player_{loader_};
    rb::cassette::DiscardByteOut bytes_;
    rb::cassette::DiscardWaveOut wave_;
    rb::cassette::PlayerOutputs outputs_{bytes_, wave_};
    rb::cassette::CassetteTick tick_{player_, outputs_, motor_};
    rb::cassette::DataInLender* lender_ = nullptr;
    bool driving_ = false;  // dataIn_ is the live output (exclusive, or lent in shared mode)
#if CONFIG_RB_CAS_ENGINE_FUJINET
    class TaskPause final : public rb::fujinet::Pause {
    public:
        void waitOneMs() override { vTaskDelay(pdMS_TO_TICKS(1)); }
        void yield() override { vTaskDelay(1); }
    };
    static bool fujiMotorDropped(void* motor);
    TaskPause pause_;
    rb::fujinet::EspFskEngine fskEngine_;
    rb::fujinet::FujiTapeRunner fuji_{dataIn_, motor_, fskEngine_, pause_};
    bool fujiEngine_ = false;  // start(): DATA IN exclusive and the engine has its memory
#endif
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
