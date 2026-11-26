// Host stand-ins for the owner's FujiNet FSK engine (cassette-motor-fujinet): heap, GPIO matrix routing,
// RMT registers and memory, UART signal table. Only the engine's test links this file.
#include <cstdio>
#include <cstdlib>

#include "driver/gpio.h"
#include "esp_err.h"
#include "esp_heap_caps.h"
#include "esp_rom_gpio.h"
#include "fake_idf.hpp"
#include "hal/rmt_ll.h"
#include "hal/uart_periph.h"

extern "C" {
volatile uint32_t RMTMEM[512] = {};
}
rmt_dev_t RMT = {};
const uart_signal_conn_t uart_periph_signal[3] = {
    {{{0}, {0}, {0}, {0}, {0}, {0}}},
    {{{0}, {0}, {0}, {0}, {0}, {0}}},
    {{{fake_idf::kU2TxdOutIdx}, {0}, {0}, {0}, {0}, {0}}},
};

const char* esp_err_to_name(esp_err_t code)
{
    return code == ESP_OK ? "ESP_OK" : "ESP_ERR";
}

void fake_idf_error_check_failed(esp_err_t code)
{
    std::fprintf(stderr, "ESP_ERROR_CHECK failed: %d\n", code);
    std::abort();
}

void* heap_caps_malloc(std::size_t size, uint32_t)
{
    auto& s = fake_idf::state();
    if (s.failHeap) {
        return nullptr;
    }
    ++s.heapAllocs;
    return std::malloc(size == 0 ? 1 : size);
}

void* heap_caps_calloc(std::size_t n, std::size_t size, uint32_t)
{
    auto& s = fake_idf::state();
    if (s.failHeap) {
        return nullptr;
    }
    ++s.heapAllocs;
    return std::calloc(n == 0 ? 1 : n, size == 0 ? 1 : size);
}

void heap_caps_free(void* ptr)
{
    if (ptr != nullptr) {
        ++fake_idf::state().heapFrees;
    }
    std::free(ptr);
}

std::size_t heap_caps_get_free_size(uint32_t) { return 4u * 1024u * 1024u; }

void esp_rom_gpio_connect_out_signal(uint32_t gpio_num, uint32_t signal_idx, bool, bool)
{
    auto& s = fake_idf::state();
    ++s.outSignalConnects;
    s.pin = static_cast<int>(gpio_num);
    s.outSel = signal_idx;
    s.outEnabled = true;
    if (signal_idx == fake_idf::kU2TxdOutIdx) {
        s.owner = fake_idf::Owner::Uart;
    } else if (signal_idx == 256) {
        s.owner = fake_idf::Owner::Gpio;
    }
}

esp_err_t gpio_set_level(gpio_num_t, uint32_t level)
{
    fake_idf::state().gpioLevel = static_cast<int>(level);
    return ESP_OK;
}

uint32_t rmt_ll_tx_get_mem_blocks(rmt_dev_t*, uint32_t) { return fake_idf::state().rmtMemBlocks; }

void rmt_ll_tx_reset_pointer(rmt_dev_t*, uint32_t) { ++fake_idf::state().rmtPointerResets; }
