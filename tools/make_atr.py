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
       python tools/make_atr.py --out build/boot-image --boot   (only rb-boot.atr, E1c)
       python tools/make_atr.py --out build/menu-demo --demo     (boot menu demo, Issue #38)
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

# --boot: the bootable image alone (E1c, Issue #32), with its own manifest. It is
# kept out of IMAGES so the Meta A set (H3, test_meta_a and the firmware
# diagnostics) stays exactly as it is.
BOOT_IMAGES = (
    ("rb-boot", 720, 128, 0),        # sector 1 holds the boot program below
)

# --demo: the boot menu demonstration (Issue #38). Two bootable images that show
# different messages, so the screen tells which one booted, and one image whose
# header is not an ATR header, which the host must refuse when it is chosen.
DEMO_IMAGES = (
    ("demo-uno", 720, 128, 0),
    ("demo-dos", 720, 128, 0),
    ("rota", 720, 128, 0),
)

# Bootable images and the message their program shows when sectors 2 and 3 match.
BOOT_TEXTS = {
    "rb-boot": "RETROBRIDGE D1 OK",
    "demo-uno": "DEMO UNO OK",
    "demo-dos": "DEMO DOS OK",
}
# Images whose ATR magic is overwritten with zeros: not valid ATR files.
INVALID_IMAGES = frozenset({"rota"})

# --- Bootable image (E1c, decision D4) ---------------------------------------
#
# Sector 1 of "rb-boot" is a disk boot record followed by an original 6502
# program (written for RetroBridge; no code from FujiNet or emulators, ADR-012).
# The OS loads BOOT_LOAD_SECTORS sectors at BOOT_ORG and calls BOOT_ORG + 6.
# The program XORs the 256 bytes of sectors 2 and 3, compares the result with the
# value this generator computed, and shows its own one-line display list:
# "RETROBRIDGE D1 OK" if they match, "RETROBRIDGE D1 ERR" otherwise. Then it
# loops forever. A message on screen therefore means sectors 1 to 3 reached the
# Atari's memory intact.

BOOT_IMAGE = "rb-boot"
BOOT_ORG = 0x0700
BOOT_LOAD_SECTORS = 3
BOOT_OK_TEXT = "RETROBRIDGE D1 OK"
BOOT_ERR_TEXT = "RETROBRIDGE D1 ERR"
BOOT_TEXT_WIDTH = 20        # ANTIC mode 7: 20 characters per line
RTCLOK_LOW = 0x14           # OS frame counter, advanced by the vertical blank
SDLSTL = 0x0230             # OS shadow of the display list pointer


def screen_codes(text: str) -> bytes:
    """ATASCII 0x20-0x5F to ANTIC screen codes, centred in BOOT_TEXT_WIDTH columns."""
    if len(text) > BOOT_TEXT_WIDTH or any(not 0x20 <= ord(c) <= 0x5F for c in text):
        raise ValueError(f"text not representable in one mode 7 line: {text!r}")
    left = (BOOT_TEXT_WIDTH - len(text)) // 2
    line = " " * left + text + " " * (BOOT_TEXT_WIDTH - left - len(text))
    return bytes(ord(c) - 0x20 for c in line)


def assemble(program: list, org: int, extra_labels: dict = None) -> bytes:
    """Two-pass assembly of a list of byte strings, ("label", name) marks and
    operand placeholders: ("lo", name), ("hi", name), ("abs", name) (2 bytes, LSB
    first) and ("rel", name) (1-byte branch offset from the next instruction).
    extra_labels adds addresses that are not marks in the program (for example,
    offsets inside a data block)."""
    sizes = {"lo": 1, "hi": 1, "abs": 2, "rel": 1}
    labels, pc = {}, org
    for item in program:
        if isinstance(item, tuple) and item[0] == "label":
            labels[item[1]] = pc
        else:
            pc += sizes[item[0]] if isinstance(item, tuple) else len(item)
    labels.update(extra_labels or {})
    out, pc = bytearray(), org
    for item in program:
        if isinstance(item, tuple):
            kind, name = item
            if kind == "label":
                continue
            value = labels[name]
            if kind == "lo":
                out.append(value & 0xFF)
            elif kind == "hi":
                out.append(value >> 8)
            elif kind == "abs":
                out += bytes([value & 0xFF, value >> 8])
            else:
                offset = value - (pc + 1)
                if not -128 <= offset <= 127:
                    raise ValueError(f"branch to {name} out of range")
                out.append(offset & 0xFF)
            pc += sizes[kind]
        else:
            out += item
            pc += len(item)
    return bytes(out)


def boot_program(expected_xor: int, ok_text: str = BOOT_OK_TEXT, err_text: str = BOOT_ERR_TEXT) -> bytes:
    """Sector 1 of a bootable image: boot record plus program, padded to 128 bytes."""
    data_start = BOOT_ORG + BOOT_SECTOR_SIZE          # sectors 2 and 3: 256 bytes
    program = [
        # Boot record: flags, sectors to load, load address, init address (DOSINI).
        bytes([0x00, BOOT_LOAD_SECTORS, BOOT_ORG & 0xFF, BOOT_ORG >> 8]), ("abs", "init"),
        # XOR of the 256 bytes loaded from sectors 2 and 3.
        b"\xA2\x00",                                  # LDX #0
        b"\x8A",                                      # TXA
        ("label", "sum"),
        bytes([0x5D, data_start & 0xFF, data_start >> 8]),  # EOR data_start,X
        b"\xE8",                                      # INX
        b"\xD0", ("rel", "sum"),                      # BNE sum
        bytes([0xC9, expected_xor]),                  # CMP #expected
        b"\xF0", ("rel", "show"),                     # BEQ show
        b"\xA9", ("lo", "err_text"),                  # LDA #<err_text
        b"\x8D", ("abs", "dl_lms"),                   # STA dl_lms
        b"\xA9", ("hi", "err_text"),                  # LDA #>err_text
        b"\x8D", ("abs", "dl_lms_hi"),                # STA dl_lms+1
        ("label", "show"),
        # Wait for a vertical blank so both pointer bytes change within one frame.
        bytes([0xA5, RTCLOK_LOW]),                    # LDA RTCLOK
        ("label", "wait"),
        bytes([0xC5, RTCLOK_LOW]),                    # CMP RTCLOK
        b"\xF0", ("rel", "wait"),                     # BEQ wait
        b"\xA9", ("lo", "dl"),                        # LDA #<dl
        bytes([0x8D, SDLSTL & 0xFF, SDLSTL >> 8]),    # STA SDLSTL
        b"\xA9", ("hi", "dl"),                        # LDA #>dl
        bytes([0x8D, (SDLSTL + 1) & 0xFF, (SDLSTL + 1) >> 8]),  # STA SDLSTL+1
        ("label", "halt"),
        b"\x4C", ("abs", "halt"),                     # JMP halt
        ("label", "init"),
        b"\x60",                                      # RTS (DOSINI on RESET)
        # Display list: 80 blank lines, one mode 7 line with LMS, jump and wait for VBL.
        ("label", "dl"),
        bytes([0x70] * 10),
        b"\x47",
        ("label", "dl_lms"), ("lo", "ok_text"),
        ("label", "dl_lms_hi"), ("hi", "ok_text"),
        b"\x41", ("abs", "dl"),
        ("label", "ok_text"), screen_codes(ok_text),
        ("label", "err_text"), screen_codes(err_text),
    ]
    code = assemble(program, BOOT_ORG)
    if len(code) > BOOT_SECTOR_SIZE:
        raise ValueError(f"boot program is {len(code)} bytes, sector 1 holds {BOOT_SECTOR_SIZE}")
    return code + bytes(BOOT_SECTOR_SIZE - len(code))


def xor_bytes(data: bytes) -> int:
    value = 0
    for byte in data:
        value ^= byte
    return value


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
    contents = {n: sector_content(image_seed, n, sector_length(n, sector_size)) for n in range(1, sectors + 1)}
    if name in BOOT_TEXTS:
        loaded = b"".join(contents[n] for n in range(2, BOOT_LOAD_SECTORS + 1))
        contents[1] = boot_program(xor_bytes(loaded), BOOT_TEXTS[name])
    if name in INVALID_IMAGES:
        out[0:2] = bytes(2)
    for sector in range(1, sectors + 1):
        length = sector_length(sector, sector_size)
        content = contents[sector]
        rows.append((name, sector, len(out), length, zlib.crc32(content) & 0xFFFFFFFF))
        out += content
    out += bytes([0xEE]) * trailing
    info = (name, sectors, sector_size, len(out), int(trailing != 0), trailing)
    return bytes(out), rows, info


def generate(out_dir: Path, seed: int, images=IMAGES) -> None:
    out_dir.mkdir(parents=True, exist_ok=True)
    manifest_rows = []
    image_rows = []
    for name, sectors, sector_size, trailing in images:
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
    group = parser.add_mutually_exclusive_group()
    group.add_argument("--boot", action="store_true",
                       help="write only the bootable rb-boot image (E1c), not the Meta A set")
    group.add_argument("--demo", action="store_true",
                       help="write the boot menu demo set (Issue #38), not the Meta A set")
    args = parser.parse_args(argv)
    images = BOOT_IMAGES if args.boot else DEMO_IMAGES if args.demo else IMAGES
    generate(args.out, args.seed, images)
    print(f"make_atr: {len(images)} image(s) written to {args.out}")
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
