#pragma once

// The host tests use the production single-task mailbox (H3-7), so the mailbox
// contract tests, the lifecycle tests and the session tests all exercise it.

#include "rb/app/inline_drive_mailbox.hpp"

namespace rb::test {

using FakeDriveMailbox = rb::app::InlineDriveMailbox;

}  // namespace rb::test
