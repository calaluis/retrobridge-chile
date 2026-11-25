#pragma once

// The SIO task's DiskLine over the menu's UART2 (spec menu-cinta, plan §2.4 decision 1, T9a.7b): park and
// resume move the permission to transmit on DATA IN, never the driver. EspSioUart does the work; this only
// adapts it to rb_menu's contract (rb_esp_sio cannot see rb_menu). Built by the host tests too.

#include "rb/esp/esp_sio_uart.hpp"
#include "rb/menu/data_in_arbiter.hpp"

namespace rb::menusio {

class UartDiskLine final : public rb::host::DiskLine {
public:
    // The line exists only once start() made it: until then park() is false (the disk never lets go).
    void attach(rb::esp::EspSioUart& line) noexcept { line_ = &line; }
    bool park() noexcept override;
    rb::host::LineResult resume() noexcept override;

private:
    rb::esp::EspSioUart* line_ = nullptr;
};

}  // namespace rb::menusio
