#!/usr/bin/env python3
"""Compare per-sector CRC32 values reported by the firmware with manifest.csv.

The firmware's diagnostic console (H3b) prints, for each verified image:
    IMAGE <name>
    CRC <sector> <crc32 as 8 hex digits>
Any other line in the log is ignored, so a raw serial capture can be passed.

Every difference, missing sector or unexpected sector is reported. Exit status
is 0 only if every sector of every image present in the log matches.

The log may instead be the JSONL written by retrobridge_host (--log, E1c). Then
the image is the one named in its "start" event, and every READ SECTOR the Atari
sent (02 31 52 lo hi cs) is paired with the next DATA_BLOCK RetroBridge sent
(02 43 <data> <checksum>). Each read is checked on its own: COMPLETE, a valid SIO
checksum, and a CRC32 equal to the manifest's. Only the sectors actually read
are checked; the log must contain at least one.

Usage: python tools/compare_crc.py manifest.csv firmware-log.txt
       python tools/compare_crc.py manifest.csv retrobridge_host.jsonl
"""

import csv
import json
import re
import sys
import zlib
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


SIO_DEVICE_D1 = 0x31
SIO_READ = 0x52
SIO_COMPLETE = 0x43
NETSIO_DATA_BLOCK = 0x02


def sio_checksum(data: bytes) -> int:
    """SIO checksum: 8-bit sum with end-around carry."""
    total = 0
    for byte in data:
        total += byte
        total = (total & 0xFF) + (total >> 8)
    return total


def is_jsonl(lines) -> bool:
    first = next((line.strip() for line in lines if line.strip()), "")
    return first.startswith("{")


def parse_host_jsonl(lines):
    """Returns (reads, problems). reads: [(image, sector, crc32)] in log order.
    Each read belongs to the image of the latest "start" event, so a log that
    holds several runs (--log appends) checks each run against its own image."""
    image, reads, problems = None, [], []
    pending = None   # sector of a READ still waiting for its data
    for number, line in enumerate(lines, 1):
        if not line.strip():
            continue
        try:
            entry = json.loads(line)
        except json.JSONDecodeError:
            entry = None
        if not isinstance(entry, dict):
            problems.append(f"line {number}: not a JSON object")
            continue
        if entry.get("kind") == "event" and entry.get("name") == "start":
            if pending is not None:
                problems.append(f"sector {pending}: READ without data before a new run")
                pending = None
            match = re.search(r"image=(\S+)", str(entry.get("detail", "")))
            image = re.sub(r"\.atr$", "", match.group(1), flags=re.IGNORECASE) if match else None
            continue
        if entry.get("kind") != "datagram" or not entry.get("hex"):
            continue
        try:
            data = bytes.fromhex(str(entry["hex"]))
        except ValueError:
            problems.append(f"line {number}: hex is not valid")
            continue
        if not data or data[0] != NETSIO_DATA_BLOCK:
            continue
        if entry.get("dir") == "in":
            # Command frame from the Atari; a hub trailing byte may follow it.
            frame = data[1:6]
            if (len(frame) == 5 and frame[0] == SIO_DEVICE_D1 and frame[1] == SIO_READ
                    and sio_checksum(frame[:4]) == frame[4]):
                if pending is not None:
                    problems.append(f"sector {pending}: READ without data before the next READ")
                pending = frame[2] | (frame[3] << 8)
            continue
        if pending is None or len(data) < 3:
            continue
        status, payload, checksum = data[1], data[2:-1], data[-1]
        if status != SIO_COMPLETE:
            problems.append(f"sector {pending}: answered {status:02x}, not COMPLETE")
        elif sio_checksum(payload) != checksum:
            problems.append(f"sector {pending}: SIO checksum {checksum:02x}, expected {sio_checksum(payload):02x}")
        else:
            reads.append((image, pending, zlib.crc32(payload) & 0xFFFFFFFF))
        pending = None
    if pending is not None:
        problems.append(f"sector {pending}: READ without data at the end of the log")
    return reads, problems


def compare_reads(expected: dict, reads) -> list:
    if not reads:
        return ["no sector read in the log"]
    problems = []
    for image, sector, crc in reads:
        if image is None:
            problems.append(f"sector {sector}: no start event naming the image")
        elif image not in expected:
            problems.append(f"{image}: not in the manifest")
        elif sector not in expected[image]:
            problems.append(f"{image}: sector {sector} is not in the image")
        elif crc != expected[image][sector]:
            problems.append(f"{image}: sector {sector} crc {crc:08x}, expected {expected[image][sector]:08x}")
    return sorted(set(problems), key=problems.index)


def main(argv: list) -> int:
    if len(argv) != 3:
        print(__doc__)
        return 2
    expected = load_manifest(Path(argv[1]))
    lines = Path(argv[2]).read_text(encoding="utf-8", errors="replace").splitlines()
    if is_jsonl(lines):
        reads, problems = parse_host_jsonl(lines)
        problems += compare_reads(expected, reads)
        for problem in problems:
            print(f"compare_crc: {problem}")
        if problems:
            print(f"compare_crc: FAILED ({len(problems)} problem(s), {len(reads)} read(s))")
            return 1
        images = sorted({image for image, _, _ in reads})
        sectors = sorted({sector for _, sector, _ in reads})
        print(f"compare_crc: OK ({', '.join(images)}, {len(reads)} read(s) of sector(s) "
              f"{', '.join(map(str, sectors))})")
        return 0
    reported = parse_log(lines)
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
