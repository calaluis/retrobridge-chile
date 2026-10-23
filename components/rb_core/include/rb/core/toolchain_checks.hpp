#pragma once

// Compile-time guards for ADR-001 (C++17) and ADR-004 (no exceptions, no RTTI).
// Every RetroBridge component includes this from its build_checks.cpp, so a
// target built with the wrong flags fails to compile instead of drifting silently.

#if !defined(__cplusplus) || __cplusplus != 201703L
#error "RetroBridge must be compiled as C++17 (ADR-001)"
#endif

#if defined(__cpp_exceptions) || defined(__EXCEPTIONS)
#error "C++ exceptions must be disabled (ADR-004)"
#endif

#if defined(__cpp_rtti) || defined(__GXX_RTTI)
#error "RTTI must be disabled (ADR-004)"
#endif
