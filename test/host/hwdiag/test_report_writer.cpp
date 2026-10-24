// ReportWriter: stable "DIAG ..." lines and a CRC-framed report that
// tools/check_diag_report.py can verify. Also writes a sample report for the
// cross-check test (RB_DIAG_SAMPLE_FILE).

#include <cstdio>
#include <cstring>
#include <string>
#include <vector>

#include "rb/hwdiag/report_writer.hpp"
#include "support/crc32.hpp"
#include "unity.h"

using rb::hwdiag::BootRecord;
using rb::hwdiag::Crc32Accumulator;
using rb::hwdiag::DiagnosticsRecorder;
using rb::hwdiag::LatencyFigures;
using rb::hwdiag::LineSink;
using rb::hwdiag::ReportWriter;
using rb::hwdiag::ResetReason;
using rb::hwdiag::SystemSnapshot;

namespace {

class CapturingSink final : public LineSink {
public:
    void line(const char* text) override { lines.emplace_back(text); }
    std::vector<std::string> lines;
};

const char* test_error_name(uint8_t code)
{
    return code == 9 ? "SectorOutOfRange" : "OtherError";
}

SystemSnapshot sample_system()
{
    SystemSnapshot s{};
    s.uptimeUs = 123456789;
    s.firmware = {"retrobridge", "1b1527d", "v6.1", "fujinet-atari-v1.3/1.5"};
    s.boot = BootRecord{ResetReason::PowerOn, 1, 0, 0, 0};
    s.memory = {280880, 270000, 110000, 280880};
    s.watchdog = {true, 5000, false, true, 300};
    return s;
}

DiagnosticsRecorder sample_recorder()
{
    DiagnosticsRecorder r{5000};
    r.recordBoot(BootRecord{ResetReason::PowerOn, 1, 0, 0, 0}, 1000);
    r.recordMount(true, 0, 0, 2000);
    r.recordVerify(4640, 0, 0, 3000);
    r.recordSectorReadError(721, 9, 4000);
    r.recordBenchmark(LatencyFigures{1000, 0, 1062, 1845, 2048, 3024, 3110, 3953}, 1000, 0, 5000);
    return r;
}

bool contains(const std::vector<std::string>& lines, const char* text)
{
    for (const auto& l : lines) {
        if (l.find(text) != std::string::npos) {
            return true;
        }
    }
    return false;
}

}  // namespace

void setUp(void) {}
void tearDown(void) {}

static void test_crc_accumulator_matches_reference(void)
{
    Crc32Accumulator crc;
    crc.add(reinterpret_cast<const uint8_t*>("1234"), 4);
    crc.add(reinterpret_cast<const uint8_t*>("56789"), 5);  // incremental
    TEST_ASSERT_EQUAL_HEX32(0xCBF43926u, crc.value());
}

static void test_memory_line(void)
{
    CapturingSink sink;
    ReportWriter{sink, test_error_name}.writeMemory(sample_system());
    TEST_ASSERT_EQUAL_UINT32(1, sink.lines.size());
    TEST_ASSERT_EQUAL_STRING("DIAG memory free8=280880 min8=270000 largest8=110000 internal=280880",
                             sink.lines[0].c_str());
}

static void test_status_lines(void)
{
    CapturingSink sink;
    const DiagnosticsRecorder r = sample_recorder();
    ReportWriter{sink, test_error_name}.writeStatus(sample_system(), r);
    TEST_ASSERT_TRUE(contains(sink.lines,
                              "DIAG firmware project=retrobridge version=1b1527d idf=v6.1 board=fujinet-atari-v1.3/1.5"));
    TEST_ASSERT_TRUE(contains(sink.lines, "DIAG uptime us=123456789"));
    TEST_ASSERT_TRUE(contains(sink.lines, "DIAG boot reason=PowerOn boots_since_power_on=1 watchdog_resets=0"));
    TEST_ASSERT_TRUE(contains(sink.lines, "DIAG watchdog task=on task_timeout_ms=5000 task_panic=no interrupt=on"));
    TEST_ASSERT_TRUE(contains(sink.lines, "DIAG summary storage_mounted=yes storage_errors=1 events=5 events_lost=0"));
}

static void test_storage_lines_name_errors_and_latency(void)
{
    CapturingSink sink;
    ReportWriter{sink, test_error_name}.writeStorage(sample_recorder());
    TEST_ASSERT_TRUE(contains(sink.lines, "DIAG storage mounted=yes mount_attempts=1 mount_failures=0"));
    TEST_ASSERT_TRUE(contains(sink.lines, "last_error=SectorOutOfRange"));
    TEST_ASSERT_TRUE(contains(sink.lines, "DIAG reads verify_runs=1 sectors=4640 verify_errors=0 sector_read_errors=1"));
    TEST_ASSERT_TRUE(contains(sink.lines, "budget_us=5000 budget_exceeded=0"));
    TEST_ASSERT_TRUE(contains(sink.lines, "p99=3110 max=3953"));
}

static void test_storage_without_benchmark_says_none(void)
{
    CapturingSink sink;
    ReportWriter{sink, nullptr}.writeStorage(DiagnosticsRecorder{5000});
    TEST_ASSERT_TRUE(contains(sink.lines, "last_error=none"));
    TEST_ASSERT_TRUE(contains(sink.lines, "last=none"));
}

static void test_events_section(void)
{
    CapturingSink sink;
    ReportWriter{sink, test_error_name}.writeEvents(sample_recorder());
    TEST_ASSERT_TRUE(contains(sink.lines, "DIAG events total=5 kept=5 lost=0 capacity=32"));
    TEST_ASSERT_TRUE(contains(sink.lines, "DIAG event seq=4 t_us=4000 type=SectorReadError value=0x0002d109"));
}

static void test_report_is_framed_and_crc_checked(void)
{
    CapturingSink sink;
    ReportWriter{sink, test_error_name}.writeReport(sample_system(), sample_recorder());
    TEST_ASSERT_TRUE(sink.lines.size() > 3);
    TEST_ASSERT_EQUAL_STRING("DIAG BEGIN format=1 source=retrobridge-hw-diag", sink.lines.front().c_str());

    std::string framed;
    for (std::size_t i = 0; i + 1 < sink.lines.size(); ++i) {
        framed += sink.lines[i];
        framed += '\n';
    }
    char end[64];
    std::snprintf(end, sizeof end, "DIAG END lines=%u crc32=%08x", static_cast<unsigned>(sink.lines.size() - 1),
                  static_cast<unsigned>(rb::test::crc32(reinterpret_cast<const uint8_t*>(framed.data()), framed.size())));
    TEST_ASSERT_EQUAL_STRING(end, sink.lines.back().c_str());

#ifdef RB_DIAG_SAMPLE_FILE
    std::FILE* out = std::fopen(RB_DIAG_SAMPLE_FILE, "wb");
    TEST_ASSERT_NOT_NULL(out);
    std::fputs("retrobridge> diag report\r\n", out);  // console noise around the report is ignored
    for (const auto& l : sink.lines) {
        std::fprintf(out, "%s\r\n", l.c_str());
    }
    std::fputs("retrobridge>\r\n", out);
    std::fclose(out);
#endif
}

static void test_a_second_report_restarts_the_frame(void)
{
    CapturingSink first;
    CapturingSink second;
    const auto system = sample_system();
    const auto recorder = sample_recorder();
    ReportWriter writer{first, test_error_name};
    writer.writeReport(system, recorder);
    ReportWriter{second, test_error_name}.writeReport(system, recorder);
    TEST_ASSERT_EQUAL_STRING(first.lines.back().c_str(), second.lines.back().c_str());
}

static void test_largest_numbers_fit_in_one_line(void)
{
    DiagnosticsRecorder r{UINT32_MAX};
    const uint32_t m = UINT32_MAX;
    r.recordBenchmark(LatencyFigures{m, m, m, m, m, m, m, m}, m, m, UINT64_MAX);
    CapturingSink sink;
    ReportWriter{sink, nullptr}.writeStorage(r);
    TEST_ASSERT_TRUE(contains(sink.lines, " max=4294967295"));  // last field present: not truncated
}

static void test_long_values_are_truncated_safely(void)
{
    static char longBoard[400];
    std::memset(longBoard, 'x', sizeof longBoard - 1);
    longBoard[sizeof longBoard - 1] = '\0';
    SystemSnapshot s = sample_system();
    s.firmware.board = longBoard;
    CapturingSink sink;
    ReportWriter{sink, nullptr}.writeStatus(s, DiagnosticsRecorder{5000});
    for (const auto& l : sink.lines) {
        TEST_ASSERT_TRUE(l.size() < ReportWriter::kLineCapacity);
    }
}

int main(void)
{
    UNITY_BEGIN();
    RUN_TEST(test_crc_accumulator_matches_reference);
    RUN_TEST(test_memory_line);
    RUN_TEST(test_status_lines);
    RUN_TEST(test_storage_lines_name_errors_and_latency);
    RUN_TEST(test_storage_without_benchmark_says_none);
    RUN_TEST(test_events_section);
    RUN_TEST(test_report_is_framed_and_crc_checked);
    RUN_TEST(test_a_second_report_restarts_the_frame);
    RUN_TEST(test_largest_numbers_fit_in_one_line);
    RUN_TEST(test_long_values_are_truncated_safely);
    return UNITY_END();
}
