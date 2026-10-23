#!/usr/bin/env python3
"""Compare per-sector CRC32 values reported by the firmware with manifest.csv.

The firmware's diagnostic console (H3b) prints, for each verified image:
    IMAGE <name>
    CRC <sector> <crc32 as 8 hex digits>
Any other line in the log is ignored, so a raw serial capture can be passed.

Every difference, missing sector or unexpected sector is reported. Exit status
is 0 only if every sector of every image present in the log matches.

Usage: python tools/compare_crc.py manifest.csv firmware-log.txt
"""

import csv
import re
import sys
from pathlib import Path

IMAGE_LINE = re.compile(r"^\s*IMAGE\s+(\S+)\s*$")
CRC_LINE = re.compile(r"^\s*CRC\s+(\d+)\s+([0-9A-Fa-f]{8})\s*$")


def load_manifest(path: Path) -> dict:
    expected = {}
    with path.open(newline="", encoding="ascii") as f:
        for row in csv.DictReader(f):
            expected.setdefault(row["image"], {})[int(row["sector"])] = int(row["crc32"], 16)
    return expected


def parse_log(lines) -> dict:
    reported = {}
    image = None
    for line in lines:
        match = IMAGE_LINE.match(line)
        if match:
            image = match.group(1)
            reported.setdefault(image, {})
            continue
        match = CRC_LINE.match(line)
        if match and image is not None:
            reported[image][int(match.group(1))] = int(match.group(2), 16)
    return reported


def compare(expected: dict, reported: dict) -> list:
    problems = []
    if not reported:
        problems.append("no IMAGE blocks found in the log")
    for image, sectors in reported.items():
        if image not in expected:
            problems.append(f"{image}: not in the manifest")
            continue
        wanted = expected[image]
        for sector in sorted(wanted):
            if sector not in sectors:
                problems.append(f"{image}: sector {sector} missing from the log")
            elif sectors[sector] != wanted[sector]:
                problems.append(f"{image}: sector {sector} crc {sectors[sector]:08x}, expected {wanted[sector]:08x}")
        for sector in sorted(set(sectors) - set(wanted)):
            problems.append(f"{image}: unexpected sector {sector}")
    return problems


def main(argv: list) -> int:
    if len(argv) != 3:
        print(__doc__)
        return 2
    expected = load_manifest(Path(argv[1]))
    reported = parse_log(Path(argv[2]).read_text(encoding="utf-8", errors="replace").splitlines())
    problems = compare(expected, reported)
    for problem in problems:
        print(f"compare_crc: {problem}")
    checked = sum(len(s) for s in reported.values())
    if problems:
        print(f"compare_crc: FAILED ({len(problems)} problem(s), {checked} sector(s) reported)")
        return 1
    print(f"compare_crc: OK ({len(reported)} image(s), {checked} sector(s))")
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv))
