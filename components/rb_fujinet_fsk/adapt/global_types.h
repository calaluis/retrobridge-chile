// RetroBridge adapter (written for RetroBridge, not taken from FujiNet): the two names the owner's FSK
// code uses from FujiNet's global_types.h. A success flag that the caller tests with is_error().
#pragma once

class success_is_true {
public:
    constexpr explicit success_is_true(bool ok) noexcept : ok_(ok) {}
    constexpr bool is_error() const noexcept { return !ok_; }

private:
    bool ok_;
};

#define RETURN_SUCCESS_AS_TRUE() return success_is_true(true)
#define RETURN_ERROR_AS_FALSE() return success_is_true(false)
