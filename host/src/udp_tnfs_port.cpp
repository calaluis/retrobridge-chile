// UdpTnfsPort (see udp_tnfs_port.hpp).
#include "udp_tnfs_port.hpp"

#ifdef _WIN32
#include <winsock2.h>
#include <ws2tcpip.h>
#else
#include <arpa/inet.h>
#include <netdb.h>
#include <netinet/in.h>
#include <sys/socket.h>
#endif

namespace rb::host {

using rb::core::ErrorCode;
using rb::core::Failure;
using rb::core::Result;
using rb::core::Span;

Result<uint32_t> SystemNameResolver::resolveIpv4(const char* name)
{
    if (name == nullptr || name[0] == '\0') {
        return Failure{ErrorCode::NotFound};
    }
#ifdef _WIN32
    WSADATA data;
    if (::WSAStartup(MAKEWORD(2, 2), &data) != 0) {
        return Failure{ErrorCode::TransportError};
    }
#endif
    addrinfo hints{};
    hints.ai_family = AF_INET;
    hints.ai_socktype = SOCK_DGRAM;
    hints.ai_protocol = IPPROTO_UDP;
    addrinfo* found = nullptr;
    const int status = ::getaddrinfo(name, nullptr, &hints, &found);
    Result<uint32_t> result = Failure{ErrorCode::NotFound};
    if (status == 0 && found != nullptr && found->ai_addr != nullptr &&
        found->ai_addrlen >= static_cast<decltype(found->ai_addrlen)>(sizeof(sockaddr_in))) {
        const auto* address = reinterpret_cast<const sockaddr_in*>(found->ai_addr);
        result = ntohl(address->sin_addr.s_addr);
    }
    if (found != nullptr) {
        ::freeaddrinfo(found);
    }
#ifdef _WIN32
    ::WSACleanup();
#endif
    return result;
}

Result<void> UdpTnfsPort::open(const char* serverName, uint16_t port)
{
    udp_.close();
    auto address = resolver_.resolveIpv4(serverName);
    if (!address.ok()) {
        return Failure{address.error() == ErrorCode::NotFound ? ErrorCode::NotFound : ErrorCode::TransportError};
    }
    return udp_.openTo(address.value(), port);
}

void UdpTnfsPort::close() noexcept
{
    udp_.close();
}

Result<void> UdpTnfsPort::send(Span<const uint8_t> datagram)
{
    return udp_.send(datagram);
}

Result<std::size_t> UdpTnfsPort::receive(Span<uint8_t> out, uint32_t timeoutUs)
{
    return udp_.receive(out, timeoutUs);
}

}  // namespace rb::host
