#pragma once

#include <cstddef>
#include <cstdint>

#include "rb/core/result.hpp"
#include "rb/core/span.hpp"
#include "rb/netsio/datagram_port.hpp"

namespace rb::host {

// DatagramPort over one connected UDP socket (Issue #32, E1b; menu-tnfs T008).
// open(peerPort) binds 127.0.0.1 on an ephemeral port and connects to
// 127.0.0.1:<peerPort>, so only datagrams from the hub or emulator are accepted
// and nothing listens on an external interface. openTo() connects to another
// IPv4 address (a TNFS server): it binds the loopback interface when the peer is
// on loopback and any interface otherwise; being connected, the socket still
// accepts datagrams from that peer only. Windows (Winsock 2) and POSIX.
//
// Contract of rb::netsio::DatagramPort:
//   * receive() returns Timeout when nothing arrives in time, TransportError on a
//     receive failure, including ICMP "port unreachable" (no peer listening) and a
//     datagram larger than the buffer.
class UdpDatagramPort final : public rb::netsio::DatagramPort {
public:
    UdpDatagramPort() = default;
    ~UdpDatagramPort();

    // TransportError if the socket cannot be created, bound or connected.
    rb::core::Result<void> open(uint16_t peerPort);
    // `peerAddress` is IPv4 in host byte order.
    rb::core::Result<void> openTo(uint32_t peerAddress, uint16_t peerPort);
    void close() noexcept;
    uint16_t localPort() const noexcept { return localPort_; }

    rb::core::Result<void> send(rb::core::Span<const uint8_t> datagram) override;
    rb::core::Result<std::size_t> receive(rb::core::Span<uint8_t> out, uint32_t timeoutUs) override;

private:
    intptr_t socket_ = -1;
    bool started_ = false;  // Winsock started by this port
    uint16_t localPort_ = 0;
};

}  // namespace rb::host
