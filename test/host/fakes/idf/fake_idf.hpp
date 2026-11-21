#pragma once
// A scripted model of the ESP-IDF calls EspDataIn makes (UART2, RMT TX, GPIO reset, logging), so
// the DATA IN driver's pin hand-over runs on the host. It records who owns the DATA IN pin and
// lets a test make any call fail. It models ownership only: no timing, no levels on the wire
// beyond "released" (mark) or "driven by a run in flight". Like ESP-IDF v6.1 on the bench (C8,
// d05bfe2): rmt_disable() of a channel already disabled fails with ESP_ERR_INVALID_STATE and logs
// an error, and rmt_tx_wait_all_done() that times out logs "flush timeout" every time.
#include <cstddef>
#include <cstdint>
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
    bool failGpioReset = false;
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
