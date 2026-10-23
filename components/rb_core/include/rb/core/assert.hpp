#pragma once

#include <cstdlib>

namespace rb::core {

// Called when an internal invariant is violated, i.e. a programming error (ADR-003).
// Aborts on every platform: CTest reports the test as failed and ESP-IDF prints a
// backtrace. Never used for external input, which always yields a Result.
[[noreturn]] inline void assertion_failed() noexcept
{
    std::abort();
}

}  // namespace rb::core

#define RB_ASSERT(condition)                   \
    do {                                       \
        if (!(condition)) {                    \
            ::rb::core::assertion_failed();    \
        }                                      \
    } while (0)
