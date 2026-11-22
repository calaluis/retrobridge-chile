#pragma once
// Bench test of the Wi-Fi demand (menu-sio-wifi-tnfs T1a and T1b). Built only with CONFIG_RB_BENCH_WIFI; never
// published. Touches no SIO pin: no UART2, no GPIO, no ADC. The radio goes through fixed phases, over and over,
// so the current can be read on a meter at known times: idle (radio off), scan, connected and idle, and TNFS
// transfer of the test ATR (its SHA-256 compared). The network name and key come from retrobridge-wifi.txt on
// the microSD (line 1: name, line 2: key); the key stays in RAM and is never printed or written.

namespace rb::bench {

// Never returns while the phases run; returns false at once if the microSD or retrobridge-wifi.txt is missing
// or invalid, or if the Wi-Fi driver could not start (the reason is logged).
bool run_wifi(bool sdMounted) noexcept;

}  // namespace rb::bench
