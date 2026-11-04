#pragma once

#include <cstddef>
#include <cstdint>

#include "rb/core/result.hpp"
#include "rb/core/span.hpp"

namespace rb::netsio {

// Outbound port owned by rb_netsio (ADR-002, amendment H1): one connected
// datagram endpoint, the NetSIO hub or emulator. UdpDatagramPort on the PC host;
// ScriptedDatagramPort in host tests.
//
// Contract:
//   * send() transmits one whole datagram or fails with TransportError.
//   * receive() writes the next datagram into `out` and returns its length;
//     Timeout if none arrives within `timeoutUs`; TransportError on a receive
//     failure, including "peer not listening" (ICMP port unreachable). A
//     datagram larger than `out` is a TransportError.
class DatagramPort {
public:
    DatagramPort(const DatagramPort&) = delete;
    DatagramPort& operator=(const DatagramPort&) = delete;

    virtual rb::core::Result<void> send(rb::core::Span<const uint8_t> datagram) = 0;
    virtual rb::core::Result<std::size_t> receive(rb::core::Span<uint8_t> out, uint32_t timeoutUs) = 0;

protected:
    DatagramPort() = default;
    ~DatagramPort() = default;  // never deleted through the interface
};

}  // namespace rb::netsio
