#pragma once

#include <cstddef>
#include <cstdint>

#include "rb/sio/sio_line_transport.hpp"

namespace rb::esp {

// The physical SIO line of the FujiNet 1.3 (menu-sio-fisico T4): UART2 at 19200
// 8N1, TX on DATA IN (GPIO21, to a 74LS07 open collector), RX on DATA OUT
// (GPIO33, from a 74LS07, internal pull-up: the schematic shows none), COMMAND
// as an input-only level (GPIO39, R11 pull-up). It only exists in the menu-sio
// test package: the normal firmware never transmits on SIO (H4).
//
// COMMAND is polled once per tick (CONFIG_FREERTOS_HZ=1000) while the wait is
// long, and every 50 us in its last millisecond. The transport discards stale
// input before each wait, so a fall seen up to one tick late loses nothing:
// the UART keeps the frame bytes. The COMMAND -> reply latency is logged by
// SioLineTransport (risk R1 of the plan).
class EspSioUart final : public rb::sio::SioUartPort {
public:
    struct Pins {
        int command;  // 39
        int dataOut;  // 33: the Atari transmits, UART RX
        int dataIn;   // 21: the FujiNet transmits, UART TX
    };

    static constexpr int kUartRxBuffer = 1024;
    static constexpr int kUartTxBuffer = 0;  // uart_write_bytes() blocks until the FIFO takes everything
    static constexpr uint32_t kTxDoneMarginMs = 20;

    explicit EspSioUart(Pins pins) noexcept : pins_(pins) {}
    EspSioUart(const EspSioUart&) = delete;
    EspSioUart& operator=(const EspSioUart&) = delete;

    // Installs UART2 and sets the pins. False (and nothing installed) on failure.
    bool begin() noexcept;
    void end() noexcept;
    bool ready() const noexcept { return ready_; }

    // Levels for the console (`sio status`): true = high (idle).
    bool commandHigh() const noexcept;
    bool dataOutHigh() const noexcept;

    bool waitCommandLevel(bool asserted, uint32_t timeoutUs) override;
    rb::core::Result<std::size_t> read(rb::core::Span<uint8_t> out, uint32_t timeoutUs) override;
    rb::core::Result<void> write(rb::core::Span<const uint8_t> bytes) override;
    void flushInput() override;
    void idleLine() override;

private:
    Pins pins_;
    bool ready_ = false;
};

}  // namespace rb::esp
