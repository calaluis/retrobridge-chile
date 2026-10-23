#!/usr/bin/env python3
"""Generate deterministic ATR test images and their per-sector CRC32 manifest.

Used for Meta A (H3): the same images are read by the firmware (microSD on the
ESP32, files on the host) and every sector's CRC32 is compared with the one
computed here. This script is an independent oracle: it implements the ATR
layout and uses zlib.crc32 without sharing any code with the C++ firmware.

The CRC32 is a verification tool only. It is not the SIO checksum and it is
never written into the images.

Outputs, in --out (outside the repository; build/ is git-ignored):
  <name>.atr     the images
  manifest.csv   image,sector,offset,length,crc32   (one row per sector)
  images.csv     image,sectors,sector_size,file_size,header_mismatch,trailing_bytes

Usage: python tools/make_atr.py --out build/test-images [--seed N]
"""

import argparse
import csv
import sys
import zlib
from pathlib import Path

HEADER_SIZE = 16
BOOT_SECTORS = 3
BOOT_SECTOR_SIZE = 128

# name, sectors, sector size, extra trailing bytes (D-8), declare the nominal size in the header
IMAGES = (
    ("sd-720", 720, 128, 0),
    ("ed-1040", 1040, 128, 0),
    ("dd-720", 720, 256, 0),
    ("dd-1440", 1440, 256, 0),       # non-standard double density
    ("sd-trailing", 720, 128, 50),   # partial trailing sector: header/file mismatch
)


def sector_length(sector: int, sector_size: int) -> int:
    return BOOT_SECTOR_SIZE if sector <= BOOT_SECTORS else sector_size


def data_size(sectors: int, sector_size: int) -> int:
    return sum(sector_length(n, sector_size) for n in range(1, sectors + 1))


def header(declared_data_size: int, sector_size: int) -> bytes:
    paragraphs = declared_data_size // 16
    return bytes([
        0x96, 0x02,
        paragraphs & 0xFF, (paragraphs >> 8) & 0xFF,
        sector_size & 0xFF, (sector_size >> 8) & 0xFF,
        (paragraphs >> 16) & 0xFF,
    ]) + bytes(9)


def sector_content(image_seed: int, sector: int, length: int) -> bytes:
    """Deterministic content; the first two bytes are the sector number (LSB first)."""
    body = bytearray((sector * 7 + i * 13 + image_seed) & 0xFF for i in range(length))
    body[0] = sector & 0xFF
    body[1] = (sector >> 8) & 0xFF
    return bytes(body)


def build_image(name: str, sectors: int, sector_size: int, trailing: int, seed: int):
    image_seed = (seed + zlib.crc32(name.encode())) & 0xFF
    nominal = data_size(sectors, sector_size)
    out = bytearray(header(nominal, sector_size))
    rows = []
    for sector in range(1, sectors + 1):
        length = sector_length(sector, sector_size)
        content = sector_content(image_seed, sector, length)
        rows.append((name, sector, len(out), length, zlib.crc32(content) & 0xFFFFFFFF))
        out += content
    out += bytes([0xEE]) * trailing
    info = (name, sectors, sector_size, len(out), int(trailing != 0), trailing)
    return bytes(out), rows, info


def generate(out_dir: Path, seed: int) -> None:
    out_dir.mkdir(parents=True, exist_ok=True)
    manifest_rows = []
    image_rows = []
    for name, sectors, sector_size, trailing in IMAGES:
        data, rows, info = build_image(name, sectors, sector_size, trailing, seed)
        (out_dir / f"{name}.atr").write_bytes(data)
        manifest_rows.extend(rows)
        image_rows.append(info)

    with (out_dir / "manifest.csv").open("w", newline="", encoding="ascii") as f:
        writer = csv.writer(f)
        writer.writerow(("image", "sector", "offset", "length", "crc32"))
        for name, sector, offset, length, crc in manifest_rows:
            writer.writerow((name, sector, offset, length, f"{crc:08x}"))

    with (out_dir / "images.csv").open("w", newline="", encoding="ascii") as f:
        writer = csv.writer(f)
        writer.writerow(("image", "sectors", "sector_size", "file_size", "header_mismatch", "trailing_bytes"))
        writer.writerows(image_rows)


def main(argv: list) -> int:
    parser = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    parser.add_argument("--out", required=True, type=Path, help="output directory (outside the repository)")
    parser.add_argument("--seed", type=int, default=0, help="content seed (default 0)")
    args = parser.parse_args(argv)
    generate(args.out, args.seed)
    print(f"make_atr: {len(IMAGES)} images written to {args.out}")
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
