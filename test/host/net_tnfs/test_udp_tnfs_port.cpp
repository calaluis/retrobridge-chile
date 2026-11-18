// UdpTnfsPort over a real loopback UDP socket (menu-tnfs T008, AC7). A
// FakeTnfsServer answers behind its own socket on a thread; TnfsClient talks to
// it through UdpTnfsPort, with an injected resolver so no DNS is used.
//
// The server listens on 127.0.0.2, not on 127.0.0.1, so the port must really
// connect to the address the resolver gave. Windows and Linux route all of
// 127.0.0.0/8 to loopback; where 127.0.0.2 cannot be bound (macOS without an
// alias) the server falls back to 127.0.0.1: MOUNT and READ are still checked,
// only the address check is lost. If neither can be bound, the test fails.
// Unity leaves a test with longjmp on a failed assertion, so nothing is asserted
// while the server thread runs: results are kept and checked after stop().

#include <atomic>
#include <chrono>
#include <cstdint>
#include <cstring>
#include <thread>
#include <vector>

#ifdef _WIN32
#include <winsock2.h>
#include <ws2tcpip.h>
#else
#include <arpa/inet.h>
#include <netinet/in.h>
#include <sys/select.h>
#include <sys/socket.h>
#include <unistd.h>
#endif

#include "fakes/fake_clock.hpp"
#include "fakes/fake_tnfs_server.hpp"
#include "rb/net_tnfs/tnfs_client.hpp"
#include "udp_tnfs_port.hpp"
#include "unity.h"

using rb::core::ErrorCode;
using rb::core::Failure;
using rb::core::Result;
using rb::core::Span;
using rb::host::UdpTnfsPort;
using rb::net_tnfs::TnfsClient;
using rb::net_tnfs::TnfsResult;
using rb::test::FakeClock;
using rb::test::FakeTnfsServer;

namespace {

using Bytes = std::vector<uint8_t>;

#ifdef _WIN32
using Native = SOCKET;
constexpr Native kInvalid = INVALID_SOCKET;
void closeNative(Native s) { ::closesocket(s); }
#else
using Native = int;
constexpr Native kInvalid = -1;
void closeNative(Native s) { ::close(s); }
#endif

constexpr uint32_t kServerAddress = 0x7F000002;  // 127.0.0.2

// A UDP socket bound to `address` on an ephemeral port, or kInvalid.
Native bindOn(uint32_t address4, uint16_t& port)
{
    const Native s = ::socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
    if (s == kInvalid) {
        return kInvalid;
    }
    sockaddr_in address{};
    address.sin_family = AF_INET;
    address.sin_addr.s_addr = htonl(address4);
    address.sin_port = 0;
#ifdef _WIN32
    int length = sizeof address;
#else
    socklen_t length = sizeof address;
#endif
    if (::bind(s, reinterpret_cast<const sockaddr*>(&address), sizeof address) != 0 ||
        ::getsockname(s, reinterpret_cast<sockaddr*>(&address), &length) != 0) {
        closeNative(s);
        return kInvalid;
    }
    port = ntohs(address.sin_port);
    return s;
}

// 127.0.0.2 if it can be bound, else 127.0.0.1; `bound` says which.
Native bindServerSocket(uint16_t& port, uint32_t& bound)
{
    bound = kServerAddress;
    Native s = bindOn(bound, port);
    if (s == kInvalid) {
        bound = INADDR_LOOPBACK;
        s = bindOn(bound, port);
    }
    return s;
}

// The FakeTnfsServer behind a UDP socket on 127.0.0.2 (or 127.0.0.1, see the
// top), served by one thread.
// The tree is set up before start(); log() is read only after stop().
class LoopbackServer {
public:
    explicit LoopbackServer(FakeTnfsServer& server) : server_(server) {}
    ~LoopbackServer() { stop(); }

    // The port the server listens on, or 0 if no loopback address can be bound.
    uint16_t start()
    {
        uint16_t port = 0;
        socket_ = bindServerSocket(port, address_);
        if (socket_ == kInvalid) {
            return 0;
        }
        running_ = true;
        thread_ = std::thread([this] { serve(); });
        return port;
    }

    void stop()
    {
        if (running_.exchange(false)) {
            thread_.join();
        }
        if (socket_ != kInvalid) {
            closeNative(socket_);
            socket_ = kInvalid;
        }
    }

    uint32_t address() const { return address_; }

private:
    void serve()
    {
        uint8_t buffer[2048];
        while (running_) {
            fd_set readable;
            FD_ZERO(&readable);
            FD_SET(socket_, &readable);
            timeval timeout{0, 20000};
            if (::select(static_cast<int>(socket_ + 1), &readable, nullptr, nullptr, &timeout) <= 0) {
                continue;
            }
            sockaddr_in from{};
#ifdef _WIN32
            int fromLength = sizeof from;
#else
            socklen_t fromLength = sizeof from;
#endif
            const auto got = ::recvfrom(socket_, reinterpret_cast<char*>(buffer), sizeof buffer, 0,
                                        reinterpret_cast<sockaddr*>(&from), &fromLength);
            if (got <= 0) {
                continue;
            }
            for (const Bytes& reply : server_.respond(Span<const uint8_t>(buffer, static_cast<std::size_t>(got)))) {
                ::sendto(socket_, reinterpret_cast<const char*>(reply.data()), static_cast<int>(reply.size()), 0,
                         reinterpret_cast<const sockaddr*>(&from), fromLength);
            }
        }
    }

    FakeTnfsServer& server_;
    Native socket_ = kInvalid;
    uint32_t address_ = 0;
    std::atomic<bool> running_{false};
    std::thread thread_;
};

// Resolves one name to `address`; every other name has no address.
struct FakeResolver final : rb::host::NameResolver {
    const char* known = "pulgarcito.test";
    uint32_t address = kServerAddress;
    int calls = 0;
    Result<uint32_t> resolveIpv4(const char* name) override
    {
        ++calls;
        if (name != nullptr && std::strcmp(name, known) == 0) {
            return address;
        }
        return Failure{ErrorCode::NotFound};
    }
};

// Real time for the client's waits.
struct SteadyClock final : rb::net_tnfs::TnfsClock {
    std::chrono::steady_clock::time_point start = std::chrono::steady_clock::now();
    uint64_t nowMicros() const override
    {
        return static_cast<uint64_t>(
            std::chrono::duration_cast<std::chrono::microseconds>(std::chrono::steady_clock::now() - start).count());
    }
    void sleepUntil(uint64_t micros) override
    {
        const uint64_t now = nowMicros();
        if (micros > now) {
            std::this_thread::sleep_for(std::chrono::microseconds(micros - now));
        }
    }
};

struct Sink final : rb::net_tnfs::ReadSink {
    Bytes bytes;
    bool begin(uint32_t size) override
    {
        bytes.assign(size, 0);
        return true;
    }
    void data(uint32_t offset, Span<const uint8_t> got) override
    {
        std::memcpy(bytes.data() + offset, got.data(), got.size());
    }
    bool cancelled() override { return false; }
};

Bytes image(std::size_t n)
{
    Bytes bytes(n);
    for (std::size_t i = 0; i < n; ++i) {
        bytes[i] = static_cast<uint8_t>((i * 7 + 3) & 0xFF);
    }
    return bytes;
}

std::size_t count(const FakeTnfsServer& server, uint8_t command)
{
    std::size_t n = 0;
    for (const auto& entry : server.log()) {
        n += entry.command == command ? 1 : 0;
    }
    return n;
}

}  // namespace

void setUp(void) {}
void tearDown(void) {}

static void test_mount_and_read_through_a_real_socket(void)
{
    FakeClock fakeClock;
    FakeTnfsServer server(fakeClock);
    const Bytes file = image(1300);
    server.addFile("/Juegos/a.atr", file);
    LoopbackServer loopback(server);
    const uint16_t serverPort = loopback.start();
    TEST_ASSERT_TRUE_MESSAGE(serverPort != 0, "no loopback address can be bound");
    if (loopback.address() != kServerAddress) {
        TEST_MESSAGE("127.0.0.2 cannot be bound: the resolved address is not checked here");
    }

    // Nothing is asserted until the server thread has stopped (see the top).
    FakeResolver resolver;
    resolver.address = loopback.address();
    UdpTnfsPort port(resolver);
    const bool opened = port.open("pulgarcito.test", serverPort).ok();
    SteadyClock clock;
    TnfsClient client(port, clock);
    TnfsResult mounted = TnfsResult::NoResponse;
    TnfsResult read = TnfsResult::NoResponse;
    Sink sink;
    if (opened) {
        mounted = client.mount();
        read = client.readWholeReadOnly("/Juegos/a.atr", sink);
        client.umount();
    }
    port.close();
    loopback.stop();

    TEST_ASSERT_TRUE(opened);
    TEST_ASSERT_TRUE(mounted == TnfsResult::Ok);
    TEST_ASSERT_TRUE(read == TnfsResult::Ok);
    TEST_ASSERT_EQUAL_size_t(file.size(), sink.bytes.size());
    TEST_ASSERT_EQUAL_HEX8_ARRAY(file.data(), sink.bytes.data(), file.size());

    TEST_ASSERT_EQUAL_size_t(1, count(server, 0x00));
    TEST_ASSERT_EQUAL_size_t(3, count(server, 0x21));  // 512 + 512 + 276
    TEST_ASSERT_EQUAL_size_t(1, count(server, 0x23));
    TEST_ASSERT_EQUAL_size_t(0, server.openHandles());
    for (const auto& entry : server.log()) {
        if (entry.command == 0x29) {
            TEST_ASSERT_EQUAL_HEX16(0x0001, entry.flags);
            TEST_ASSERT_EQUAL_HEX16(0x0000, entry.mode);
        }
    }
}

static void test_a_name_that_does_not_resolve_is_not_found(void)
{
    FakeResolver resolver;
    UdpTnfsPort port(resolver);
    auto opened = port.open("nadie.test");
    TEST_ASSERT_FALSE(opened.ok());
    TEST_ASSERT_TRUE(opened.error() == ErrorCode::NotFound);
    TEST_ASSERT_EQUAL_INT(1, resolver.calls);
    const uint8_t datagram[] = {0, 0, 1, 0};
    TEST_ASSERT_FALSE(port.send(Span<const uint8_t>(datagram)).ok());  // nothing was opened
}

// The DatagramPort contract: a datagram larger than the buffer is a
// TransportError, not a shorter datagram.
static void test_a_datagram_larger_than_the_buffer_is_an_error(void)
{
    uint16_t peerPort = 0;
    uint32_t peerAddress = 0;
    const Native peer = bindServerSocket(peerPort, peerAddress);
    TEST_ASSERT_TRUE_MESSAGE(peer != kInvalid, "no loopback address can be bound");
    FakeResolver resolver;
    resolver.address = peerAddress;
    UdpTnfsPort port(resolver);
    const bool opened = port.open("pulgarcito.test", peerPort).ok();
    const uint8_t hello[] = {1};
    const bool sent = opened && port.send(Span<const uint8_t>(hello)).ok();
    bool answered = false;
    if (sent) {
        uint8_t got[16];
        sockaddr_in from{};
#ifdef _WIN32
        int fromLength = sizeof from;
#else
        socklen_t fromLength = sizeof from;
#endif
        if (::recvfrom(peer, reinterpret_cast<char*>(got), sizeof got, 0, reinterpret_cast<sockaddr*>(&from),
                       &fromLength) == 1) {
            const std::vector<uint8_t> big(100, 0x5A);
            answered = ::sendto(peer, reinterpret_cast<const char*>(big.data()), static_cast<int>(big.size()), 0,
                                reinterpret_cast<const sockaddr*>(&from), fromLength) == 100;
        }
    }
    uint8_t small[64];
    auto received = port.receive(Span<uint8_t>(small), 1000000);
    port.close();
    closeNative(peer);

    TEST_ASSERT_TRUE(opened);
    TEST_ASSERT_TRUE(sent);
    TEST_ASSERT_TRUE(answered);
    TEST_ASSERT_FALSE(received.ok());
    TEST_ASSERT_TRUE(received.error() == ErrorCode::TransportError);
}

static void test_the_system_resolver_finds_localhost(void)
{
    rb::host::SystemNameResolver resolver;
    auto local = resolver.resolveIpv4("localhost");
    TEST_ASSERT_TRUE(local.ok());
    TEST_ASSERT_EQUAL_HEX32(INADDR_LOOPBACK, local.value());
    auto empty = resolver.resolveIpv4("");
    TEST_ASSERT_TRUE(!empty.ok() && empty.error() == ErrorCode::NotFound);
    auto none = resolver.resolveIpv4(nullptr);
    TEST_ASSERT_TRUE(!none.ok() && none.error() == ErrorCode::NotFound);
}

int main(void)
{
#ifdef _WIN32
    WSADATA data;
    if (::WSAStartup(MAKEWORD(2, 2), &data) != 0) {
        return 1;
    }
#endif
    UNITY_BEGIN();
    RUN_TEST(test_mount_and_read_through_a_real_socket);
    RUN_TEST(test_a_name_that_does_not_resolve_is_not_found);
    RUN_TEST(test_a_datagram_larger_than_the_buffer_is_an_error);
    RUN_TEST(test_the_system_resolver_finds_localhost);
    const int failures = UNITY_END();
#ifdef _WIN32
    ::WSACleanup();
#endif
    return failures;
}
