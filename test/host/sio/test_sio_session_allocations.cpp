// No dynamic allocation during SIO transactions (ADR-011).
//
// Mechanism: this binary replaces the global allocation functions, which the C++
// standard allows ([replacement.functions]), and counts every call. Any
// `new`, including those made inside standard containers, std::function or
// std::string, goes through them. The count is taken only around
// SioSession::step(); setup may allocate freely. A positive control proves the
// counter is live, so a zero result is meaningful.
//
// Limits: direct malloc() calls are not counted (the pure layers contain none;
// check_layering and code review cover that), and over-aligned `new` is not
// replaced (no over-aligned types are used).

#include <array>
#include <cstdint>
#include <cstdlib>
#include <new>

#include "fakes/fake_byte_source.hpp"
#include "fakes/memory_byte_source.hpp"
#include "fakes/memory_image_repository.hpp"
#include "rb/app/image_location.hpp"
#include "rb/app/usecases/mount_disk_image.hpp"
#include "rb/domain/disk_image.hpp"
#include "session_harness.hpp"
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

using rb::app::ImageLocation;
using rb::domain::DiskImage;
using rb::domain::DriveNumber;
using rb::sio::StepOutcome;
using rb::test::dd_data_size;
using rb::test::FakeByteSource;
using rb::test::kAtrHeaderSize;
using rb::test::make_atr_header;
using rb::test::read_frame;
using rb::test::sd_data_size;
using rb::test::SessionHarness;
using rb::test::status_frame;

namespace {

// Runs one step and checks it allocated nothing.
StepOutcome step_without_allocation(SessionHarness& h)
{
    const unsigned long before = g_allocations;
    const StepOutcome outcome = h.session.step();
    TEST_ASSERT_EQUAL_UINT32_MESSAGE(0, g_allocations - before, "SioSession::step() allocated");
    return outcome;
}

void mount(SessionHarness& h, FakeByteSource& source)
{
    auto result = h.bay.mount(DriveNumber::from(1).value(), DiskImage::open(source).value(),
                              ImageLocation::from("test.atr").value());
    TEST_ASSERT_TRUE(result.ok());
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
    SessionHarness h;
    FakeByteSource dd{make_atr_header(dd_data_size(720), 256), kAtrHeaderSize + dd_data_size(720)};
    mount(h, dd);

    h.transport.push_frame(status_frame());
    h.transport.push_frame(read_frame(1));    // 128-byte sector
    h.transport.push_frame(read_frame(4));    // 256-byte sector
    h.transport.push_frame(read_frame(0));    // NAK
    auto corrupted = status_frame();
    corrupted[4] ^= 0xFF;
    h.transport.push_frame(corrupted);        // ignored
    h.transport.push_timeout();               // idle

    TEST_ASSERT_TRUE(step_without_allocation(h) == StepOutcome::Completed);
    TEST_ASSERT_TRUE(step_without_allocation(h) == StepOutcome::Completed);
    TEST_ASSERT_TRUE(step_without_allocation(h) == StepOutcome::Completed);
    TEST_ASSERT_TRUE(step_without_allocation(h) == StepOutcome::Nacked);
    TEST_ASSERT_TRUE(step_without_allocation(h) == StepOutcome::Ignored);
    TEST_ASSERT_TRUE(step_without_allocation(h) == StepOutcome::Idle);
}

static void test_errors_and_recovery_do_not_allocate(void)
{
    SessionHarness h;
    FakeByteSource sd{make_atr_header(sd_data_size(720), 128), kAtrHeaderSize + sd_data_size(720)};
    mount(h, sd);
    h.transport.fail_send(0);
    h.transport.push_frame(read_frame(1));    // ACK fails
    h.transport.push_receive_error();
    h.transport.push_frame(read_frame(2));    // read error -> ERROR + zeroed frame

    TEST_ASSERT_TRUE(step_without_allocation(h) == StepOutcome::TransportFailed);
    TEST_ASSERT_TRUE(step_without_allocation(h) == StepOutcome::TransportFailed);
    sd.fail_reads(true);
    TEST_ASSERT_TRUE(step_without_allocation(h) == StepOutcome::Completed);
}

static void test_writes_do_not_allocate(void)
{
    // Built before measuring: the source's vector allocates, a write does not.
    rb::test::MemoryByteSource source{make_atr_header(sd_data_size(720), 128), kAtrHeaderSize + sd_data_size(720),
                                      true};
    rb::test::FakeClock clock;
    rb::test::ScriptedSioTransport transport{clock};
    rb::app::DriveBay bay{1};
    rb::test::FakeDriveMailbox mailbox;
    rb::sio::SioCommandDispatcher dispatcher{bay, true};
    rb::app::ApplyDriveRequest apply{bay, mailbox};
    rb::sio::SioSession session{transport, clock, dispatcher, apply};
    TEST_ASSERT_TRUE(bay.mount(DriveNumber::from(1).value(), DiskImage::open(source).value(),
                               rb::app::ImageLocation::from("w.atr").value())
                         .ok());
    const auto good = rb::test::data_frame(128, 0x10);
    const auto bad = rb::test::data_frame(128, 0x20, false);
    transport.push_frame(rb::test::sio_frame(rb::test::kD1, 0x57, 4, 0));
    transport.push_data(good.data(), 129);
    transport.push_frame(rb::test::sio_frame(rb::test::kD1, 0x50, 5, 0));
    transport.push_data(bad.data(), 129);
    transport.push_frame(rb::test::sio_frame(rb::test::kD1, 0x57, 6, 0));  // no data: dropped

    const unsigned long before = g_allocations;
    TEST_ASSERT_TRUE(session.step() == StepOutcome::Completed);
    TEST_ASSERT_TRUE(session.step() == StepOutcome::Nacked);
    TEST_ASSERT_TRUE(session.step() == StepOutcome::TransportFailed);
    TEST_ASSERT_EQUAL_UINT32_MESSAGE(0, g_allocations - before, "a write transaction allocated");
}

static void test_applying_a_mailbox_request_does_not_allocate(void)
{
    SessionHarness h;
    rb::test::MemoryImageRepository<1> repository;
    repository.add(rb::test::MemoryImage{"dd.atr", make_atr_header(dd_data_size(720), 256),
                                         kAtrHeaderSize + dd_data_size(720)});
    rb::app::MountDiskImage mount_image{repository, h.mailbox};  // storage context, outside the window
    TEST_ASSERT_TRUE(mount_image.execute(DriveNumber::from(1).value(), ImageLocation::from("dd.atr").value()).ok());

    TEST_ASSERT_TRUE(step_without_allocation(h) == StepOutcome::MailboxApplied);
}

static void test_format_does_not_allocate(void)
{
    rb::test::MemoryByteSource source{make_atr_header(sd_data_size(720), 128), kAtrHeaderSize + sd_data_size(720),
                                      true};
    rb::test::FakeClock clock;
    rb::test::ScriptedSioTransport transport{clock};
    rb::app::DriveBay bay{1};
    rb::test::FakeDriveMailbox mailbox;
    rb::sio::SioCommandDispatcher dispatcher{bay, true};
    rb::app::ApplyDriveRequest apply{bay, mailbox};
    rb::sio::SioSession session{transport, clock, dispatcher, apply};
    TEST_ASSERT_TRUE(bay.mount(DriveNumber::from(1).value(), DiskImage::open(source).value(),
                               rb::app::ImageLocation::from("f.atr").value())
                         .ok());
    transport.push_frame(rb::test::sio_frame(rb::test::kD1, 0x21, 0, 0));
    transport.push_frame(rb::test::sio_frame(rb::test::kD1, 0x22, 0, 0));  // other density: ERROR

    const unsigned long before = g_allocations;
    TEST_ASSERT_TRUE(session.step() == StepOutcome::Completed);
    TEST_ASSERT_TRUE(session.step() == StepOutcome::Completed);
    TEST_ASSERT_EQUAL_UINT32_MESSAGE(0, g_allocations - before, "a format transaction allocated");
}

int main(void)
{
    UNITY_BEGIN();
    RUN_TEST(test_counter_detects_allocations);
    RUN_TEST(test_transactions_do_not_allocate);
    RUN_TEST(test_errors_and_recovery_do_not_allocate);
    RUN_TEST(test_writes_do_not_allocate);
    RUN_TEST(test_format_does_not_allocate);
    RUN_TEST(test_applying_a_mailbox_request_does_not_allocate);
    return UNITY_END();
}
