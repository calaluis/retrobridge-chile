#pragma once

// Whether the TNFS sources (2..8) can reach the network now (menu-sio-wifi-tnfs T5, C7).
// The menu asks before each TNFS job; when the answer is not Ready the job does not
// start and the main message line says why. Source 1 (the microSD) never asks.
// state() runs on the menu (SIO) thread: it reads a flag, never waits.

#include <cstdint>

namespace rb::host {

enum class NetworkState : uint8_t {
    Ready,       // an address is assigned: the TNFS job may start
    Connecting,  // the station is still joining the network
    NoFile,      // the Wi-Fi file is missing
    BadFile,     // the Wi-Fi file is there but its name or key is not valid
    NoNetwork,   // the network named in the file was not found
    BadKey,      // the network refused the key
    Failed,      // the Wi-Fi driver did not start
};

class NetworkGate {
public:
    NetworkGate(const NetworkGate&) = delete;
    NetworkGate& operator=(const NetworkGate&) = delete;

    virtual NetworkState state() const noexcept = 0;

protected:
    NetworkGate() = default;
    ~NetworkGate() = default;
};

// The message line for a state other than Ready (fits the 40-column line).
inline const char* networkText(NetworkState state) noexcept
{
    switch (state) {
    case NetworkState::Ready:
        return "";
    case NetworkState::Connecting:
        return " WI-FI CONECTANDO, REPITE EN UN RATO";
    case NetworkState::NoFile:
        return " SIN WI-FI: FALTA RETROBRIDGE-WIFI.TXT";
    case NetworkState::BadFile:
        return " SIN WI-FI: ARCHIVO WI-FI NO VALIDO";
    case NetworkState::NoNetwork:
        return " SIN WI-FI: RED NO ENCONTRADA";
    case NetworkState::BadKey:
        return " SIN WI-FI: CLAVE INCORRECTA";
    case NetworkState::Failed:
        break;
    }
    return " SIN WI-FI: EL WI-FI NO ARRANCO";
}

// A short English name for the log.
inline const char* networkName(NetworkState state) noexcept
{
    switch (state) {
    case NetworkState::Ready:
        return "ready";
    case NetworkState::Connecting:
        return "wifi connecting";
    case NetworkState::NoFile:
        return "no wifi file";
    case NetworkState::BadFile:
        return "wifi file not valid";
    case NetworkState::NoNetwork:
        return "network not found";
    case NetworkState::BadKey:
        return "key refused";
    case NetworkState::Failed:
        break;
    }
    return "wifi driver failed";
}

}  // namespace rb::host
