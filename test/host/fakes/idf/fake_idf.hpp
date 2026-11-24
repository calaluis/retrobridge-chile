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
    int uartRxFullThreshold = -1;        // uart_set_rx_full_threshold (-1: driver default, 120)
    int uartRxTimeout = -1;              // uart_set_rx_timeout (-1: driver default, 10 characters)
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

    // --- UART2 TX timing model (menu-cinta T9a.2, decision 3) --------------------------------
    // Off by default: the older tests keep their simple UART. On: uart_write_bytes() goes through the
    // TX ring (if one was installed) or straight into the 128-byte FIFO, the "interrupt" moves bytes
    // from the ring to the FIFO, the shift register sends one byte every 10 bits at uartBaud, and
    // uart_wait_tx_done() waits on that. Time is nowUs. Every write costs the ring a header besides its
    // data (kTxRingHeader, as the driver's per-write item headers), so the ring looks fuller than its data.
    bool txModel = false;
    int txRing = 0;                      // TX ring size (uart_driver_install's tx_buffer_size)
    int txRingUsed = 0;                  // ring bytes taken, headers included
    std::vector<int> txRingData;         // data bytes left in each write still in the ring, oldest first
    std::vector<int> txRingCost;         // what each of those writes costs the ring
    int txFifo = 0;                      // bytes in the hardware FIFO (0..128)
    int64_t txShiftLeftUs = 0;           // > 0: a byte is in the shift register for this long
    bool txIsrHeld = false;              // the interrupt does not move ring -> FIFO (until released)
    bool txIsrAfterRingRead = false;     // the interrupt runs right after uart_get_tx_buffer_free_size()
    int64_t txWriteBlockedUs = 0;        // the longest single uart_write_bytes() call, in model time
    int txMaxInFlight = 0;               // most data bytes ever in ring + FIFO + shift register
    unsigned uartInstalls = 0;
    unsigned uartDeletes = 0;

    // --- GPIO matrix of the DATA IN pin (menu-cinta T9a.2, Fault recovery) --------------------
    uint32_t outSel = 256;               // GPIO_FUNCn_OUT_SEL: 256 = SIG_GPIO_OUT (plain GPIO)
    bool outEnabled = false;
    bool routeForced = false;            // readOutputRoute() returns the forced values instead
    uint32_t forcedSel = 0;
    bool forcedEnabled = false;
};

constexpr int kTxFifo = 128;
constexpr int kTxRingHeader = 28;        // per write: the driver's tx header item and the data item's header
constexpr uint32_t kU2TxdOutIdx = 198;   // U2TXD_OUT_IDX on the ESP32
constexpr uint32_t kRmtOut0Idx = 87;     // RMT_SIG_OUT0_IDX on the ESP32

extern int g_channel;
extern int g_encoder;

State& state();
void reset();
// Plays the run in flight to its end through the encoder callback, as the RMT would, then calls
// on_trans_done from "the interrupt". Returns the number of symbols the encoder produced.
std::size_t play_all();
// True when DATA IN is at mark: nothing drives it low. A run in flight may drive it.
bool line_released();
// TX model: lets `us` of line time pass (the shift register sends, the interrupt refills the FIFO).
void tx_advance(int64_t us);
// TX model: data bytes not yet on the wire (ring + FIFO + shift register), headers not counted.
int tx_in_flight();
bool tx_empty();
void tx_hardware_now();  // runs the interrupt and the hardware once, without time passing

}  // namespace fake_idf
