#pragma once

// Contract suite for the ImageRepository port (architecture §3.4 and §10.3):
// NotFound / NoFreeSlot, detached sources only, no reuse of attached slots,
// idempotent close. The abort on closing an attached source is checked by a
// separate death test, since Unity cannot observe an abort in-process.

#include <array>
#include <cstddef>
#include <optional>
#include <utility>

#include "rb/app/ports/image_repository.hpp"
#include "rb/domain/disk_image.hpp"
#include "unity.h"

namespace rb::test {

struct RepositoryFixture {
    rb::app::ImageRepository& repository;
    rb::app::ImageLocation valid;    // a valid ATR image
    rb::app::ImageLocation missing;  // not in the repository
    std::size_t capacity;            // number of slots (2..4 for this suite)
};

inline rb::domain::ImageByteSource* open_ok(rb::app::ImageRepository& repository, const rb::app::ImageLocation& location)
{
    rb::core::Result<rb::domain::ImageByteSource*> result = repository.open(location);
    TEST_ASSERT_TRUE_MESSAGE(result.ok(), "expected open() to succeed");
    return result.value();
}

inline void contract_missing_location_is_not_found(RepositoryFixture& f)
{
    TEST_ASSERT_TRUE(f.repository.open(f.missing).error() == rb::core::ErrorCode::NotFound);
}

inline void contract_full_repository_reports_no_free_slot(RepositoryFixture& f)
{
    std::array<rb::domain::ImageByteSource*, 4> sources{};
    for (std::size_t i = 0; i < f.capacity; ++i) {
        sources[i] = open_ok(f.repository, f.valid);
    }
    TEST_ASSERT_TRUE(f.repository.open(f.valid).error() == rb::core::ErrorCode::NoFreeSlot);
    for (std::size_t i = 0; i < f.capacity; ++i) {
        f.repository.close(sources[i]);
    }
}

inline void contract_open_returns_distinct_detached_sources(RepositoryFixture& f)
{
    rb::domain::ImageByteSource* a = open_ok(f.repository, f.valid);
    rb::domain::ImageByteSource* b = open_ok(f.repository, f.valid);
    TEST_ASSERT_TRUE(a != b);
    TEST_ASSERT_FALSE(a->isAttached());
    TEST_ASSERT_FALSE(b->isAttached());
    f.repository.close(a);
    f.repository.close(b);
}

inline void contract_attached_slot_is_never_reused(RepositoryFixture& f)
{
    rb::domain::ImageByteSource* attached = open_ok(f.repository, f.valid);
    std::optional<rb::domain::DiskImage> image;
    image.emplace(rb::domain::DiskImage::open(*attached).value());

    // Fill every other slot, freeing and reopening repeatedly: the attached
    // source must never be handed out again.
    for (int round = 0; round < 3; ++round) {
        std::array<rb::domain::ImageByteSource*, 4> others{};
        for (std::size_t i = 0; i + 1 < f.capacity; ++i) {
            others[i] = open_ok(f.repository, f.valid);
            TEST_ASSERT_TRUE(others[i] != attached);
        }
        TEST_ASSERT_TRUE(f.repository.open(f.valid).error() == rb::core::ErrorCode::NoFreeSlot);
        for (std::size_t i = 0; i + 1 < f.capacity; ++i) {
            f.repository.close(others[i]);
        }
    }
    TEST_ASSERT_TRUE(attached->isAttached());

    image.reset();  // detach, then the slot may be closed and reused
    f.repository.close(attached);
}

inline void contract_close_is_idempotent(RepositoryFixture& f)
{
    rb::domain::ImageByteSource* source = open_ok(f.repository, f.valid);
    f.repository.close(source);
    f.repository.close(source);   // second close: no effect
    f.repository.close(nullptr);  // null: no effect

    // Capacity is intact: every slot can still be opened exactly once.
    std::array<rb::domain::ImageByteSource*, 4> sources{};
    for (std::size_t i = 0; i < f.capacity; ++i) {
        sources[i] = open_ok(f.repository, f.valid);
    }
    TEST_ASSERT_TRUE(f.repository.open(f.valid).error() == rb::core::ErrorCode::NoFreeSlot);
    for (std::size_t i = 0; i < f.capacity; ++i) {
        f.repository.close(sources[i]);
    }
}

inline void contract_closed_slot_is_reusable(RepositoryFixture& f)
{
    rb::domain::ImageByteSource* first = open_ok(f.repository, f.valid);
    {
        rb::domain::DiskImage image = rb::domain::DiskImage::open(*first).value();
        TEST_ASSERT_TRUE(image.isValid());
    }  // image destroyed: source detached
    f.repository.close(first);
    rb::domain::ImageByteSource* again = open_ok(f.repository, f.valid);
    TEST_ASSERT_FALSE(again->isAttached());
    TEST_ASSERT_TRUE(rb::domain::DiskImage::open(*again).ok());
    f.repository.close(again);
}

}  // namespace rb::test
