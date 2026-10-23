// DriveMailbox contract (ADR-014): one exchange in flight; a refused submit
// never consumes the request, so its disk image (and source) is not lost.

#include <cstring>
#include <optional>
#include <utility>

#include "fakes/fake_byte_source.hpp"
#include "fakes/fake_drive_mailbox.hpp"
#include "rb/app/ports/drive_mailbox.hpp"
#include "support/atr_header.hpp"
#include "unity.h"

using rb::app::DriveReply;
using rb::app::DriveRequest;
using rb::app::DriveRequestKind;
using rb::app::ImageLocation;
using rb::core::ErrorCode;
using rb::domain::DiskImage;
using rb::domain::DriveNumber;
using rb::test::FakeByteSource;
using rb::test::FakeDriveMailbox;
using rb::test::kAtrHeaderSize;
using rb::test::make_atr_header;
using rb::test::sd_data_size;

namespace {

FakeByteSource sd_source()
{
    return FakeByteSource{make_atr_header(sd_data_size(720), 128), kAtrHeaderSize + sd_data_size(720)};
}

DriveNumber d1()
{
    return DriveNumber::from(1).value();
}

DriveRequest mount_request(FakeByteSource& source)
{
    return DriveRequest::mount(d1(), DiskImage::open(source).value(), ImageLocation::from("sd.atr").value());
}

DriveReply ok_reply(DriveRequestKind kind)
{
    return DriveReply{kind, d1(), std::nullopt, nullptr, nullptr};
}

}  // namespace

void setUp(void) {}
void tearDown(void) {}

static void test_idle_mailbox_has_nothing_to_take(void)
{
    FakeDriveMailbox mailbox;
    TEST_ASSERT_FALSE(mailbox.takeRequest().has_value());
    TEST_ASSERT_FALSE(mailbox.takeReply().has_value());
}

static void test_full_exchange(void)
{
    FakeByteSource source = sd_source();
    FakeDriveMailbox mailbox;
    DriveRequest request = mount_request(source);
    TEST_ASSERT_TRUE(mailbox.submit(request).ok());

    std::optional<DriveRequest> taken = mailbox.takeRequest();
    TEST_ASSERT_TRUE(taken.has_value());
    TEST_ASSERT_TRUE(taken->kind() == DriveRequestKind::Mount);
    TEST_ASSERT_TRUE(taken->image().has_value());
    TEST_ASSERT_EQUAL_PTR(&source, taken->image()->source());
    TEST_ASSERT_EQUAL_STRING("sd.atr", taken->location()->c_str());

    mailbox.putReply(ok_reply(DriveRequestKind::Mount));
    std::optional<DriveReply> reply = mailbox.takeReply();
    TEST_ASSERT_TRUE(reply.has_value());
    TEST_ASSERT_FALSE(reply->error.has_value());
    TEST_ASSERT_TRUE(mailbox.idle());
}

static void test_busy_mailbox_refuses_without_consuming_the_request(void)
{
    FakeByteSource first = sd_source();
    FakeByteSource second = sd_source();
    FakeDriveMailbox mailbox;
    DriveRequest a = mount_request(first);
    TEST_ASSERT_TRUE(mailbox.submit(a).ok());

    DriveRequest b = mount_request(second);
    TEST_ASSERT_TRUE(mailbox.submit(b).error() == ErrorCode::MailboxBusy);
    TEST_ASSERT_TRUE(b.image()->isValid());  // still owned by the caller
    TEST_ASSERT_TRUE(second.isAttached());
}

static void test_mailbox_stays_busy_until_the_reply_is_collected(void)
{
    FakeByteSource source = sd_source();
    FakeDriveMailbox mailbox;
    DriveRequest request = DriveRequest::unmount(d1());
    TEST_ASSERT_TRUE(mailbox.submit(request).ok());
    std::optional<DriveRequest> taken = mailbox.takeRequest();

    DriveRequest during_processing = mount_request(source);
    TEST_ASSERT_TRUE(mailbox.submit(during_processing).error() == ErrorCode::MailboxBusy);

    mailbox.putReply(ok_reply(DriveRequestKind::Unmount));
    TEST_ASSERT_TRUE(mailbox.submit(during_processing).error() == ErrorCode::MailboxBusy);
    TEST_ASSERT_FALSE(mailbox.takeRequest().has_value());

    TEST_ASSERT_TRUE(mailbox.takeReply().has_value());
    TEST_ASSERT_TRUE(mailbox.submit(during_processing).ok());
}

// Death test: a reply without a taken request is a programming error.
static int reply_without_request(void)
{
    FakeDriveMailbox mailbox;
    mailbox.putReply(ok_reply(DriveRequestKind::Unmount));  // must not return
    return 0;
}

int main(int argc, char** argv)
{
    if (argc > 1 && std::strcmp(argv[1], "--reply-without-request") == 0) {
        return reply_without_request();
    }

    UNITY_BEGIN();
    RUN_TEST(test_idle_mailbox_has_nothing_to_take);
    RUN_TEST(test_full_exchange);
    RUN_TEST(test_busy_mailbox_refuses_without_consuming_the_request);
    RUN_TEST(test_mailbox_stays_busy_until_the_reply_is_collected);
    return UNITY_END();
}
