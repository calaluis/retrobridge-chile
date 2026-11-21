#pragma once
// A scripted model of the ESP-IDF calls EspDataIn makes (UART2, RMT TX, GPIO reset, logging), so
// the DATA IN driver's pin hand-over runs on the host. It records who owns the DATA IN pin and
// lets a test make any call fail. It models ownership only: no timing, no levels on the wire
// beyond "released" (mark) or "driven by a run in flight". Like ESP-IDF v6.1 on the bench (C8,
// d05bfe2): rmt_disable() of a channel already disabled fails with ESP_ERR_INVALID_STATE and logs
// an error, and rmt_tx_wait_all_done() that times out logs "flush timeout" every time.
#include <cstddef>
#include <cstdint>
#include <utility>
#include <vector>

namespace fake_idf {

enum class Owner { None, Uart, Rmt, Gpio };

struct State {
    // Who has the pin. None: unrouted (an input, not driven). Gpio: after gpio_reset_pin.
    Owner owner = Owner::None;
    int pin = -1;
    bool uartInstalled = false;
    bool rmtChannel = false;     // a TX channel exists
    bool rmtEnabled = false;
    bool rmtBusy = false;        // a transmission is in flight
    uint32_t initLevel = 0;
    uint32_t eotLevel = 0;
    void* encoderCallback = nullptr;
    void* encoderArg = nullptr;
    void* doneCallback = nullptr;  // rmt_tx_event_callbacks_t::on_trans_done
    void* doneContext = nullptr;
    std::vector<uint8_t> written;  // bytes handed to uart_write_bytes
    unsigned uartSetPinCalls = 0;
    unsigned gpioResets = 0;
    unsigned rmtNewChannels = 0;
    unsigned rmtTransmits = 0;
    unsigned errorLogs = 0;
    unsigned rmtDisableRefused = 0;  // rmt_disable() of a channel not enabled (IDF: invalid state)
    unsigned rmtWaitTimeouts = 0;    // rmt_tx_wait_all_done() that timed out (IDF: "flush timeout")
    // Scripted failures.
    bool failUartSetPin = false;
    bool failRmtNewChannel = false;
    bool failRmtEnable = false;
    bool failRmtDisable = false;    // rmt_disable() fails: the channel stays enabled, the run goes on
    bool failRmtDelChannel = false; // rmt_del_channel() fails: the channel and the pin stay the RMT's
    unsigned rmtDelChannelCalls = 0;
    unsigned rmtDelEncoderCalls = 0;
    bool failGpioReset = false;

    // --- SIO line (menu-sio-fisico T4) ------------------------------------------------------
    int64_t nowUs = 0;                   // esp_timer_get_time(); vTaskDelay, delays and reads advance it
    int uartBaud = 0;                    // uart_param_config
    int uartTxPin = -1;                  // uart_set_pin
    int uartRxPin = -1;
    std::vector<int> inputs;             // gpio_set_direction(INPUT)
    std::vector<int> pullups;            // gpio_pullup_en
    int commandPin = -1;                 // the pin gpio_get_level() reads as COMMAND
    int64_t commandLowFromUs = -1;       // COMMAND is low in [from, until)
    int64_t commandLowUntilUs = -1;
    std::vector<uint8_t> rx;             // bytes received on DATA OUT, oldest first
    std::vector<std::pair<int64_t, uint8_t>> rxAt;  // bytes still to arrive, at their time (ascending)
    bool failUartRead = false;           // uart_read_bytes() returns -1
    bool failUartWrite = false;          // uart_write_bytes() returns -1
    bool failUartInstall = false;
    unsigned uartFlushes = 0;
    unsigned uartWaitTxDone = 0;
    unsigned taskDelays = 0;
};

extern int g_channel;
extern int g_encoder;

State& state();
void reset();
// Plays the run in flight to its end through the encoder callback, as the RMT would, then calls
// on_trans_done from "the interrupt". Returns the number of symbols the encoder produced.
std::size_t play_all();
// True when DATA IN is at mark: nothing drives it low. A run in flight may drive it.
bool line_released();

}  // namespace fake_idf
