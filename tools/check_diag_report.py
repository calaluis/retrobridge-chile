#!/usr/bin/env python3
"""Check a `diag report` captured from the RetroBridge console (UART0/USB).

The firmware frames the report between "DIAG BEGIN ..." and
"DIAG END lines=<n> crc32=<hex>". The CRC-32 (zlib) covers every line from
BEGIN up to the line before END, each followed by '\\n'. Any other line of the
capture (prompts, commands, logs) is ignored, so a raw serial log can be passed.
Line endings (CR/LF) are normalised.

Prints the fields of the last complete report and exits 0 only if it verifies.

Usage: python tools/check_diag_report.py capture.txt
"""

import sys
import zlib
from pathlib import Path

BEGIN = "DIAG BEGIN "
END = "DIAG END "


def parse_fields(line: str) -> dict:
    fields = {}
    for token in line.split()[2:]:
        if "=" in token:
            key, value = token.split("=", 1)
            fields[key] = value
    return fields


def find_reports(lines):
    """Yields (report_lines, end_line) for every BEGIN..END block."""
    current = None
    for line in lines:
        if line.startswith(BEGIN):
            current = [line]
        elif current is not None and line.startswith(END):
            yield current, line
            current = None
        elif current is not None and line.startswith("DIAG "):
            current.append(line)


def verify(report_lines, end_line):
    """Returns a list of problems (empty when the report verifies)."""
    problems = []
    end = parse_fields(end_line)
    if "lines" not in end or "crc32" not in end:
        return ["END line without lines= or crc32="]
    if int(end["lines"]) != len(report_lines):
        problems.append(f"line count {len(report_lines)} != declared {end['lines']}")
    payload = "".join(line + "\n" for line in report_lines).encode("ascii", "replace")
    crc = f"{zlib.crc32(payload) & 0xFFFFFFFF:08x}"
    if crc != end["crc32"].lower():
        problems.append(f"crc32 {crc} != declared {end['crc32']}")
    begin = parse_fields(report_lines[0])
    if begin.get("format") != "1":
        problems.append(f"unsupported format {begin.get('format')!r}")
    return problems


def check_text(text: str):
    lines = [line.rstrip("\r") for line in text.splitlines()]
    reports = list(find_reports(lines))
    if not reports:
        return None, ["no complete DIAG BEGIN ... DIAG END report found"]
    report_lines, end_line = reports[-1]
    return report_lines, verify(report_lines, end_line)


def main(argv=None) -> int:
    argv = sys.argv[1:] if argv is None else argv
    if len(argv) != 1:
        print(__doc__)
        return 2
    report_lines, problems = check_text(Path(argv[0]).read_text(encoding="utf-8", errors="replace"))
    if report_lines:
        for line in report_lines[1:]:
            print(line)
    if problems:
        for problem in problems:
            print(f"FAIL: {problem}")
        return 1
    print(f"OK: report verified ({len(report_lines)} lines)")
    return 0


if __name__ == "__main__":
    sys.exit(main())
