// Host stand-in for rb_esp_cassette's gpio_route.cpp: the GPIO matrix as fake_idf models it.
#include "fake_idf.hpp"
#include "hal/uart_ll.h"
#include "rb/esp/gpio_route.hpp"

namespace rb::esp {

OutputRoute readOutputRoute(int pin) noexcept
{
    const auto& s = fake_idf::state();
    OutputRoute route;
    if (s.routeForced) {
        route.signal = s.forcedSel;
        route.enabled = s.forcedEnabled;
    } else if (pin == s.pin) {
        route.signal = s.outSel;
        route.enabled = s.outEnabled;
    }
    return route;
}

uint32_t uart2TxSignal() noexcept { return fake_idf::kU2TxdOutIdx; }

}  // namespace rb::esp

namespace {
uart_dev_t g_uart2{2};
}  // namespace

uart_dev_t* fake_uart_hw(int /*num*/) { return &g_uart2; }

uint32_t uart_ll_get_txfifo_len(uart_dev_t* /*hw*/)
{
    return static_cast<uint32_t>(fake_idf::kTxFifo - fake_idf::state().txFifo);
}
