"""Self-tests for tools/check_diag_report.py.

Run with: python -m unittest discover -s tools/tests
"""

import sys
import unittest
import zlib
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent.parent))

import check_diag_report as cdr  # noqa: E402


def make_report(body, declared_lines=None, crc_override=None):
    lines = ["DIAG BEGIN format=1 source=retrobridge-hw-diag"] + body
    crc = zlib.crc32("".join(l + "\n" for l in lines).encode()) & 0xFFFFFFFF
    n = len(lines) if declared_lines is None else declared_lines
    end = f"DIAG END lines={n} crc32={crc_override or f'{crc:08x}'}"
    return lines + [end]


class CheckDiagReportTest(unittest.TestCase):
    BODY = ["DIAG memory free8=280880 min8=270000 largest8=110000 internal=280880",
            "DIAG storage mounted=yes mount_attempts=1 mount_failures=0 last_esp_err=0x0 last_error=none"]

    def test_valid_report_inside_console_noise(self):
        text = "\r\n".join(["retrobridge> diag report"] + make_report(self.BODY) + ["retrobridge>"]) + "\r\n"
        lines, problems = cdr.check_text(text)
        self.assertEqual(problems, [])
        self.assertEqual(len(lines), 3)

    def test_altered_line_fails_crc(self):
        report = make_report(self.BODY)
        report[1] = report[1].replace("280880", "280881", 1)
        _, problems = cdr.check_text("\n".join(report))
        self.assertTrue(any("crc32" in p for p in problems))

    def test_missing_line_fails_count(self):
        report = make_report(self.BODY)
        del report[2]
        _, problems = cdr.check_text("\n".join(report))
        self.assertTrue(any("line count" in p for p in problems))

    def test_truncated_capture_without_end(self):
        _, problems = cdr.check_text("\n".join(make_report(self.BODY)[:-1]))
        self.assertEqual(problems, ["no complete DIAG BEGIN ... DIAG END report found"])

    def test_last_report_wins(self):
        bad = make_report(self.BODY, crc_override="00000000")
        good = make_report(self.BODY)
        _, problems = cdr.check_text("\n".join(bad + good))
        self.assertEqual(problems, [])

    def test_unknown_format_is_rejected(self):
        report = make_report(self.BODY)
        report[0] = "DIAG BEGIN format=2 source=retrobridge-hw-diag"
        crc = zlib.crc32("".join(l + "\n" for l in report[:-1]).encode()) & 0xFFFFFFFF
        report[-1] = f"DIAG END lines={len(report) - 1} crc32={crc:08x}"
        _, problems = cdr.check_text("\n".join(report))
        self.assertTrue(any("format" in p for p in problems))


if __name__ == "__main__":
    unittest.main()
