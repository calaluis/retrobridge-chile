#include "udp_datagram_port.hpp"

#ifdef _WIN32
#include <winsock2.h>
#include <ws2tcpip.h>
#else
#include <arpa/inet.h>
#include <cerrno>
#include <netinet/in.h>
#include <sys/select.h>
#include <sys/socket.h>
#include <sys/uio.h>
#include <unistd.h>
#endif

namespace rb::host {

using rb::core::ErrorCode;
using rb::core::Failure;
using rb::core::Result;
using rb::core::Span;

namespace {

#ifdef _WIN32
using Native = SOCKET;
constexpr Native kInvalid = INVALID_SOCKET;
void closeNative(Native s) { ::closesocket(s); }
#else
using Native = int;
constexpr Native kInvalid = -1;
void closeNative(Native s) { ::close(s); }
#endif

Native native(intptr_t s) { return static_cast<Native>(s); }

sockaddr_in ipv4(uint32_t hostOrderAddress, uint16_t port)
{
    sockaddr_in address{};
    address.sin_family = AF_INET;
    address.sin_port = htons(port);
    address.sin_addr.s_addr = htonl(hostOrderAddress);
    return address;
}

bool isLoopback(uint32_t hostOrderAddress)
{
    return (hostOrderAddress >> 24) == 127;  // 127.0.0.0/8
}

}  // namespace

UdpDatagramPort::~UdpDatagramPort()
{
    close();
}

Result<void> UdpDatagramPort::open(uint16_t peerPort)
{
    return openTo(INADDR_LOOPBACK, peerPort);
}

Result<void> UdpDatagramPort::openTo(uint32_t peerAddress, uint16_t peerPort)
{
    close();
#ifdef _WIN32
    WSADATA data;
    if (::WSAStartup(MAKEWORD(2, 2), &data) != 0) {
        return Failure{ErrorCode::TransportError};
    }
    started_ = true;
#endif
    const Native s = ::socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
    if (s == kInvalid) {
        close();
        return Failure{ErrorCode::TransportError};
    }
    socket_ = static_cast<intptr_t>(s);

    sockaddr_in local = ipv4(isLoopback(peerAddress) ? INADDR_LOOPBACK : INADDR_ANY, 0);
    sockaddr_in peer = ipv4(peerAddress, peerPort);
    if (::bind(s, reinterpret_cast<const sockaddr*>(&local), sizeof local) != 0 ||
        ::connect(s, reinterpret_cast<const sockaddr*>(&peer), sizeof peer) != 0) {
        close();
        return Failure{ErrorCode::TransportError};
    }
    sockaddr_in bound{};
#ifdef _WIN32
    int boundLength = sizeof bound;
#else
    socklen_t boundLength = sizeof bound;
#endif
    if (::getsockname(s, reinterpret_cast<sockaddr*>(&bound), &boundLength) == 0) {
        localPort_ = ntohs(bound.sin_port);
    }
    return {};
}

void UdpDatagramPort::close() noexcept
{
    if (socket_ != -1) {
        closeNative(native(socket_));
        socket_ = -1;
    }
#ifdef _WIN32
    if (started_) {
        ::WSACleanup();
    }
#endif
    started_ = false;
    localPort_ = 0;
}

Result<void> UdpDatagramPort::send(Span<const uint8_t> datagram)
{
    if (socket_ == -1) {
        return Failure{ErrorCode::TransportError};
    }
    const auto sent = ::send(native(socket_), reinterpret_cast<const char*>(datagram.data()),
                             static_cast<int>(datagram.size()), 0);
    if (sent < 0 || static_cast<std::size_t>(sent) != datagram.size()) {
        return Failure{ErrorCode::TransportError};
    }
    return {};
}

Result<std::size_t> UdpDatagramPort::receive(Span<uint8_t> out, uint32_t timeoutUs)
{
    if (socket_ == -1) {
        return Failure{ErrorCode::TransportError};
    }
    const Native s = native(socket_);
    fd_set readable;
    FD_ZERO(&readable);
    FD_SET(s, &readable);
    timeval timeout{};
    timeout.tv_sec = static_cast<long>(timeoutUs / 1000000u);
    timeout.tv_usec = static_cast<long>(timeoutUs % 1000000u);
    const int ready = ::select(static_cast<int>(s + 1), &readable, nullptr, nullptr, &timeout);
    if (ready == 0) {
        return Failure{ErrorCode::Timeout};
    }
    if (ready < 0) {
        return Failure{ErrorCode::TransportError};
    }
#ifdef _WIN32
    const auto received = ::recv(s, reinterpret_cast<char*>(out.data()), static_cast<int>(out.size()), 0);
    // A negative result covers ICMP port unreachable (WSAECONNRESET) and a
    // datagram larger than `out` (WSAEMSGSIZE).
    if (received < 0) {
        return Failure{ErrorCode::TransportError};
    }
#else
    // recvmsg() marks a datagram larger than `out` with MSG_TRUNC (POSIX); recv()
    // would cut it silently. A negative result covers ICMP port unreachable
    // (ECONNREFUSED).
    iovec buffer{out.data(), out.size()};
    msghdr message{};
    message.msg_iov = &buffer;
    message.msg_iovlen = 1;
    const auto received = ::recvmsg(s, &message, 0);
    if (received < 0 || (message.msg_flags & MSG_TRUNC) != 0) {
        return Failure{ErrorCode::TransportError};
    }
#endif
    return static_cast<std::size_t>(received);
}

}  // namespace rb::host
