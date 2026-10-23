# Shared compiler flags for every RetroBridge-owned target, used by both the
# ESP-IDF build and the host test build so they compile identically.
#   ADR-001: C++17 with GNU extensions (ESP-IDF defaults to a newer standard).
#   ADR-004: no exceptions, no RTTI.
include_guard(GLOBAL)

function(rb_apply_build_flags target)
    target_compile_options(${target} PRIVATE
        "$<$<COMPILE_LANGUAGE:CXX>:-std=gnu++17;-fno-exceptions;-fno-rtti>"
        -Wall
        -Wextra
        -Werror)
endfunction()
