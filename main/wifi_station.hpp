#pragma once
// Wi-Fi station of the menu-sio package (menu-sio-wifi-tnfs T4, first demo): connects in the background
// to the network named in /sd/retrobridge-wifi.txt and reconnects on its own when the link drops.
//
//   * File: line 1 the network name (1..32 bytes), line 2 the key (empty for an open network, or 8..63
//     bytes). Only CR/LF are removed, so names and keys may hold spaces.
//   * The key lives only in RAM: it is never logged (only its length), never written to a file, and the
//     Wi-Fi driver keeps its copy in RAM (WIFI_STORAGE_RAM; the NVS is neither read nor written:
//     CONFIG_ESP_WIFI_NVS_ENABLED and CONFIG_ESP_PHY_CALIBRATION_AND_DATA_STORAGE are off in
//     sdkconfig.menu-sio).
//   * The driver log is set to WARN before it starts, so its INFO lines (which print the MAC) never reach
//     the console.
//
// runWifiStation() never returns while the station works: it is the body of the network task, which
// finish() creates after D1 is already served, so the Wi-Fi never delays the menu.
namespace rb::menusio {

// The state the menu may show later (T5); for the first demo only the console logs it.
enum class WifiState : int { NoFile, BadFile, Failed, Connecting, Connected };

WifiState wifiState() noexcept;

// Reads `path`, starts the station and keeps it connected. Returns only on a failure that a retry cannot
// fix (no file, a malformed file, the driver does not start).
void runWifiStation(const char* path) noexcept;

}  // namespace rb::menusio
