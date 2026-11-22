#pragma once

// DatagramPort to a TNFS server named in --sources (menu-tnfs T008): the name is
// resolved to an IPv4 address and UDP port 16384 is used. A name that does not
// resolve gives NotFound, shown as "ERROR: SERVIDOR NO ENCONTRADO" (AC7).
// The resolver is injected so tests do not depend on DNS.

#include <cstddef>
#include <cstdint>

#include "rb/core/result.hpp"
#include "rb/core/span.hpp"
#include "rb/net_tnfs/tnfs_codec.hpp"
#include "rb/netsio/datagram_port.hpp"
#include "udp_datagram_port.hpp"

namespace rb::host {

class NameResolver {
public:
    // The IPv4 address of `name`, in host byte order. NotFound if it has none.
    virtual rb::core::Result<uint32_t> resolveIpv4(const char* name) = 0;

protected:
    NameResolver() = default;
    ~NameResolver() = default;
};

// The operating system's resolver (getaddrinfo, IPv4 only).
class SystemNameResolver final : public NameResolver {
public:
    rb::core::Result<uint32_t> resolveIpv4(const char* name) override;
};

class UdpTnfsPort final : public rb::netsio::DatagramPort {
public:
    explicit UdpTnfsPort(NameResolver& resolver) noexcept : resolver_(resolver) {}

    // NotFound if the name does not resolve; TransportError if the socket fails.
    rb::core::Result<void> open(const char* serverName, uint16_t port = rb::net_tnfs::kPort);
    void close() noexcept;

    rb::core::Result<void> send(rb::core::Span<const uint8_t> datagram) override;
    rb::core::Result<std::size_t> receive(rb::core::Span<uint8_t> out, uint32_t timeoutUs) override;

private:
    NameResolver& resolver_;
    UdpDatagramPort udp_;
};

}  // namespace rb::host
