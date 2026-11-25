#pragma once
// Host stand-in: the ESP32 output signals main/bench_c8.cpp compares against (values as fake_idf models them).
#include "fake_idf.hpp"
#define U2TXD_OUT_IDX (fake_idf::kU2TxdOutIdx)
#define RMT_SIG_OUT0_IDX (fake_idf::kRmtOut0Idx)
#define RMT_SIG_OUT7_IDX (fake_idf::kRmtOut0Idx + 7)
