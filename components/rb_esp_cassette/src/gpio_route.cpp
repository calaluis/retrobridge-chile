#include "rb/esp/gpio_route.hpp"

#include "soc/gpio_reg.h"
#include "soc/gpio_sig_map.h"
#include "soc/soc.h"

namespace rb::esp {

OutputRoute readOutputRoute(int pin) noexcept
{
    OutputRoute route;
    if (pin < 0 || pin > 31) {
        return route;
    }
    // A volatile load, not REG_GET_FIELD: its DPORT check needs a constant address, and the GPIO matrix is not DPORT.
    const uint32_t cfg = GPIO_FUNC0_OUT_SEL_CFG_REG + 4u * static_cast<uint32_t>(pin);
    route.signal = (*reinterpret_cast<const volatile uint32_t*>(cfg) >> GPIO_FUNC0_OUT_SEL_S) & GPIO_FUNC0_OUT_SEL_V;
    route.enabled = ((REG_READ(GPIO_ENABLE_REG) >> pin) & 1u) != 0;
    return route;
}

uint32_t uart2TxSignal() noexcept { return U2TXD_OUT_IDX; }

}  // namespace rb::esp
