// No dynamic allocation in SioListener::step() (ADR-011).
//
// Same mechanism as test_sio_session_allocations.cpp: this binary replaces the
// global allocation functions ([replacement.functions]) and counts every call
// around step(); setup may allocate freely. A positive control proves the
// counter is live, so a zero result is meaningful.

#include <array>
#include <cstdint>
#include <cstdlib>
#include <new>

#include "fakes/fake_clock.hpp"
#include "fakes/scripted_sio_transport.hpp"
#include "rb/core/span.hpp"
#include "rb/sio/sio_checksum.hpp"
#include "rb/sio/sio_listener.hpp"
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

using rb::sio::ListenOutcome;
using rb::sio::SioListener;
using rb::test::FakeClock;
using rb::test::ScriptedSioTransport;

namespace {

constexpr uint32_t kWaitUs = 20000;  // test value

std::array<uint8_t, 5> valid_frame(uint8_t aux1)
{
    std::array<uint8_t, 5> bytes{0x31, 0x52, aux1, 0, 0};
    bytes[4] = rb::sio::sio_checksum(rb::core::Span<const uint8_t>{bytes.data(), 4});
    return bytes;
}

ListenOutcome step_without_allocation(SioListener& listener)
{
    const unsigned long before = g_allocations;
    const ListenOutcome outcome = listener.step(kWaitUs);
    TEST_ASSERT_EQUAL_UINT32_MESSAGE(0, g_allocations - before, "SioListener::step() allocated");
    return outcome;
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

static void test_every_outcome_and_a_full_ring_do_not_allocate(void)
{
    FakeClock clock;
    ScriptedSioTransport transport{clock};
    SioListener listener{transport, clock};

    auto bad = valid_frame(1);
    bad[4] ^= 0xFF;
    transport.push_frame(valid_frame(0));
    transport.push_frame(bad);
    transport.push_frame(valid_frame(2), 3);
    transport.push_frame(valid_frame(3), 7);
    transport.push_receive_error();
    transport.push_timeout();
    for (uint8_t i = 0; i < SioListener::kCapacity; ++i) {
        transport.push_frame(valid_frame(i));  // fills and then overwrites the ring
    }

    TEST_ASSERT_TRUE(step_without_allocation(listener) == ListenOutcome::Frame);
    TEST_ASSERT_TRUE(step_without_allocation(listener) == ListenOutcome::ChecksumMismatch);
    TEST_ASSERT_TRUE(step_without_allocation(listener) == ListenOutcome::Incomplete);
    TEST_ASSERT_TRUE(step_without_allocation(listener) == ListenOutcome::Overlong);
    TEST_ASSERT_TRUE(step_without_allocation(listener) == ListenOutcome::TransportError);
    TEST_ASSERT_TRUE(step_without_allocation(listener) == ListenOutcome::Timeout);
    for (std::size_t i = 0; i < SioListener::kCapacity; ++i) {
        TEST_ASSERT_TRUE(step_without_allocation(listener) == ListenOutcome::Frame);
    }
    TEST_ASSERT_TRUE(listener.stats().overwritten > 0);  // the overwrite path ran
    TEST_ASSERT_EQUAL_UINT(0, transport.sends());
    TEST_ASSERT_EQUAL_UINT(0, transport.releases());
}

int main(void)
{
    UNITY_BEGIN();
    RUN_TEST(test_counter_detects_allocations);
    RUN_TEST(test_every_outcome_and_a_full_ring_do_not_allocate);
    return UNITY_END();
}
