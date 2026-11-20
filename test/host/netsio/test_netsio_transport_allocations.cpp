// No dynamic allocation while SioSession runs over NetSioSioTransport (ADR-011).
// Same mechanism as test_sio_session_allocations: the global allocation
// functions are replaced and counted around SioSession::step() only; setup may
// allocate freely, and a positive control proves the counter is live.
// Limits: direct malloc() calls are not counted (rb_netsio contains none), and
// the real UDP adapter of the PC host is outside this test.

#include <array>
#include <cstdint>
#include <cstdlib>
#include <new>

#include "fakes/fake_byte_source.hpp"
#include "netsio/netsio_harness.hpp"
#include "rb/sio/sio_codes.hpp"
#include "support/atr_header.hpp"
#include "unity.h"

namespace {
unsigned long g_allocations = 0;

void* counted_allocation(std::size_t size) noexcept
{
    ++g_allocations;
    return std::malloc(size == 0 ? 1 : size);
}
}  // namespace

// Built with -fno-exceptions: allocation failure aborts instead of throwing.
void* operator new(std::size_t size)
{
    void* p = counted_allocation(size);
    if (p == nullptr) {
        std::abort();
    }
    return p;
}
void* operator new[](std::size_t size)
{
    return ::operator new(size);
}
void* operator new(std::size_t size, const std::nothrow_t&) noexcept
{
    return counted_allocation(size);
}
void* operator new[](std::size_t size, const std::nothrow_t&) noexcept
{
    return counted_allocation(size);
}
void operator delete(void* p) noexcept
{
    std::free(p);
}
void operator delete[](void* p) noexcept
{
    std::free(p);
}
void operator delete(void* p, std::size_t) noexcept
{
    std::free(p);
}
void operator delete[](void* p, std::size_t) noexcept
{
    std::free(p);
}

using rb::netsio::NetSioConfig;
using rb::netsio::PeerProfile;
using rb::sio::StepOutcome;
using rb::test::command_frame;
using rb::test::FakeByteSource;
using rb::test::kAtrHeaderSize;
using rb::test::kD1;
using rb::test::kD2;
using rb::test::make_atr_header;
using rb::test::NetSioHarness;
using rb::test::sd_data_size;

namespace {

StepOutcome step_without_allocation(NetSioHarness& h)
{
    const unsigned long before = g_allocations;
    const StepOutcome outcome = h.session.step();
    TEST_ASSERT_EQUAL_UINT32_MESSAGE(0, g_allocations - before, "SioSession::step() over NetSIO allocated");
    return outcome;
}

// The harness holds ~100 KB of scripted datagrams: keep it off the stack.
NetSioHarness* make_harness(const NetSioConfig& config)
{
    return new NetSioHarness{config};
}

}  // namespace

void setUp(void) {}
void tearDown(void) {}

static void test_counter_detects_allocations(void)
{
    const unsigned long before = g_allocations;
    int* p = new int(42);
    TEST_ASSERT_EQUAL_UINT32(1, g_allocations - before);
    delete p;
}

static void test_transactions_do_not_allocate(void)
{
    NetSioConfig config = rb::test::harness_config(PeerProfile::NetsiohubTrailingByte);
    config.aliveIntervalUs = 10000;  // exercise C4 inside the measured window
    NetSioHarness* h = make_harness(config);
    FakeByteSource sd{make_atr_header(sd_data_size(720), 128), kAtrHeaderSize + sd_data_size(720)};
    h->mount(sd);
    h->connect(1);

    h->push_frame(command_frame(kD1, rb::sio::command::kStatus), 0x01);         // completed, credit 1 -> 0
    h->push_frame(command_frame(kD1, rb::sio::command::kReadSector, 1, 0), 0x02);
    h->port.push({0xC7, 0x03}, 3000);                                            // credit after C6
    h->push_frame(command_frame(kD1, 0x50, 1, 0), 0x03);                         // NAK
    h->push_frame(command_frame(kD2, rb::sio::command::kStatus), 0x04);         // ignored
    h->from_atari({0xFF});                                                       // cold reset
    h->from_atari({0x18, 0x05});                                                 // stray sync

    TEST_ASSERT_TRUE(step_without_allocation(*h) == StepOutcome::Completed);
    TEST_ASSERT_TRUE(step_without_allocation(*h) == StepOutcome::Completed);
    TEST_ASSERT_TRUE(step_without_allocation(*h) == StepOutcome::Nacked);
    TEST_ASSERT_TRUE(step_without_allocation(*h) == StepOutcome::Ignored);
    TEST_ASSERT_TRUE(step_without_allocation(*h) == StepOutcome::Idle);
    TEST_ASSERT_TRUE(step_without_allocation(*h) == StepOutcome::Idle);
    TEST_ASSERT_TRUE(h->transport.stats().aliveRequests > 0);
    TEST_ASSERT_EQUAL_UINT32(1, h->transport.stats().creditRequests);
    delete h;
}

static void test_write_data_frame_does_not_allocate(void)
{
    NetSioHarness* h = make_harness(rb::test::harness_config(PeerProfile::NetsiohubTrailingByte));
    h->connect();
    h->push_frame(command_frame(kD1, 0x57, 4, 0), 0x07);
    std::array<uint8_t, 1 + 128> block{};
    block[0] = 0x02;
    h->from_atari_bytes(block.data(), block.size());
    h->from_atari({0x09, 0x00, 0x08});
    std::array<uint8_t, 129> frame{};
    const uint8_t ack[] = {0x41};

    const unsigned long before = g_allocations;
    TEST_ASSERT_TRUE(h->transport.waitCommandFrame(20000).ok());
    TEST_ASSERT_TRUE(h->transport.receiveAfterAck(rb::core::Span<uint8_t>{frame}).ok());
    TEST_ASSERT_TRUE(h->transport.send(rb::core::Span<const uint8_t>{ack}).ok());
    TEST_ASSERT_EQUAL_UINT32_MESSAGE(0, g_allocations - before, "write data frame over NetSIO allocated");
    delete h;
}

static void test_failures_do_not_allocate(void)
{
    NetSioHarness* h = make_harness(rb::test::harness_config(PeerProfile::Fep003Strict));
    FakeByteSource sd{make_atr_header(sd_data_size(720), 128), kAtrHeaderSize + sd_data_size(720)};
    h->mount(sd);
    h->connect(0);
    h->push_frame(command_frame(kD1, rb::sio::command::kStatus), 0x06);  // no credit: times out
    TEST_ASSERT_TRUE(step_without_allocation(*h) == StepOutcome::TransportFailed);
    TEST_ASSERT_EQUAL_UINT32(1, h->transport.stats().creditRequests);

    h->port.push_receive_error();  // queued after the credit wait, so it reaches waitCommandFrame()
    TEST_ASSERT_TRUE(step_without_allocation(*h) == StepOutcome::TransportFailed);
    delete h;
}

int main(void)
{
    UNITY_BEGIN();
    RUN_TEST(test_counter_detects_allocations);
    RUN_TEST(test_transactions_do_not_allocate);
    RUN_TEST(test_write_data_frame_does_not_allocate);
    RUN_TEST(test_failures_do_not_allocate);
    return UNITY_END();
}
