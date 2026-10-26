// No dynamic allocation in CommandFrameAssembler (ADR-011).
//
// Same mechanism as test_sio_session_allocations.cpp: this binary replaces the
// global allocation functions ([replacement.functions]) and counts every call
// around the assembler's operations; setup may allocate freely. A positive
// control proves the counter is live, so a zero result is meaningful.

#include <array>
#include <cstdint>
#include <cstdlib>
#include <new>

#include "rb/core/span.hpp"
#include "rb/sio/command_frame_assembler.hpp"
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

using rb::core::Span;
using rb::sio::AssemblyOutcome;
using rb::sio::CommandFrameAssembler;
using rb::sio::CommandLineLimits;

void setUp(void) {}
void tearDown(void) {}

static void test_counter_detects_allocations(void)
{
    const unsigned long before = g_allocations;
    int* p = new int(42);
    TEST_ASSERT_EQUAL_UINT32(1, g_allocations - before);
    delete p;
}

static void test_every_path_runs_without_allocating(void)
{
    auto created = CommandFrameAssembler::create(CommandLineLimits{100, 6000, 50000});
    TEST_ASSERT_TRUE(created.ok());
    CommandFrameAssembler a = created.value();
    const std::array<uint8_t, 5> bytes{0x31, 0x53, 0, 0, 0x84};
    const Span<const uint8_t> span{bytes};

    const unsigned long before = g_allocations;
    a.onBytes(span);                                      // stray
    a.onFall(0);                                          // valid frame
    a.onBytes(span);
    a.onRise(4000);
    a.onBytes(span);                                      // late
    TEST_ASSERT_TRUE(a.complete().outcome == AssemblyOutcome::Frame);
    a.onFall(10000);                                      // glitch
    a.onRise(10050);
    TEST_ASSERT_TRUE(a.complete().outcome == AssemblyOutcome::Dropped);
    a.onFall(20000);                                      // overlong
    a.onRise(27000);
    TEST_ASSERT_TRUE(a.complete().outcome == AssemblyOutcome::Dropped);
    a.onFall(30000);                                      // stuck
    a.poll(90000);
    a.onRise(95000);
    TEST_ASSERT_TRUE(a.complete().outcome == AssemblyOutcome::Dropped);
    a.onFall(100000);                                     // lost rise, then superseded
    a.onFall(101000);
    a.onRise(104000);
    a.onFall(120000);
    a.onReceiveError();                                   // receive error
    a.onRise(124000);
    TEST_ASSERT_TRUE(a.complete().outcome == AssemblyOutcome::ReceiveError);
    a.onFall(130000);                                     // queue overflow
    a.onQueueOverflow();
    a.onRise(134000);
    TEST_ASSERT_TRUE(a.complete().outcome == AssemblyOutcome::Dropped);
    a.onRise(140000);                                     // rise without fall
    a.onFall(1);                                          // rejected time
    TEST_ASSERT_TRUE(a.complete().outcome == AssemblyOutcome::NothingToDeliver);
    TEST_ASSERT_EQUAL_UINT32_MESSAGE(0, g_allocations - before, "CommandFrameAssembler allocated");
}

int main(void)
{
    UNITY_BEGIN();
    RUN_TEST(test_counter_detects_allocations);
    RUN_TEST(test_every_path_runs_without_allocating);
    return UNITY_END();
}
