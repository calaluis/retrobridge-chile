#!/usr/bin/env python3
"""Generate SECTOR-PROBE.ATR, a boot disk that reads sector 65535 of D2 (menu-tnfs, AC14, M41-7).

The demo of the menu-tnfs feature mounts a large image from a TNFS server on D2
and must show that D2 serves its last sector. This disk, booted from D1, asks D2
for that sector through SIOV with a full DCB (no FMS): device $31, unit 2,
command 'R', AUX1 = AUX2 = $FF (sector 65535), 256 bytes into $0600-$06FF. Then
it shows the SIO status and the 256 bytes in hexadecimal, 16 per line, on a
narrow playfield (32 characters per line), and stops. The bytes stay at
$0600-$06FF for Altirra's debugger.

The program is original (ADR-012): it uses only documented Atari OS entry
points and registers. The disk is generated in the lab's --images folder and is
not versioned.

Usage: python tools/make_sector_probe.py --out <folder>   (writes SECTOR-PROBE.ATR)
"""

import argparse
import sys
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))
from make_atr import BOOT_SECTOR_SIZE, RTCLOK_LOW, SDLSTL, assemble, header  # noqa: E402
from make_menu import (DAUX1, DAUX2, DBUFHI, DBUFLO, DBYTHI, DBYTLO, DCOMND, DDEVIC,  # noqa: E402
                       DSTATS, DTIMLO, DUNIT, SIOV, screen_codes)

PROBE_ORG = 0x0700
IMAGE_NAME = "SECTOR-PROBE.ATR"
IMAGE_SECTORS = 720                 # single density, 128-byte sectors
BUFFER = 0x0600                     # the sector read from D2
SECTOR_BYTES = 256
DRIVE_DEVICE, DRIVE_UNIT, READ_COMMAND = 0x31, 2, 0x52
SECTOR_NUMBER = 65535
READ_TIMEOUT_S = 7                  # DTIMLO
SDMCTL = 0x022F
SDMCTL_NARROW = 0x21                # narrow playfield, display list DMA on
LINE_WIDTH = 32                     # characters per line on a narrow playfield
HEX_LINE_BYTES = 16
HEX_SCREEN = 0x1000                 # 16 lines x 32: not loaded, filled by the program
TITLE_TEXT = " SECTOR 65535 DE D2"
STATUS_TEXT = " ESTADO SIO $"
DIGIT_0, LETTER_A = 0x10, 0x21      # screen codes of '0' and 'A'


def store(value: int, address: int) -> list:
    return [bytes([0xA9, value]), bytes([0x8D, address & 0xFF, address >> 8])]


def nibble_to_screen_code() -> list:
    """A = 0..15 -> its hexadecimal digit as a screen code ('0'..'9', 'A'..'F')."""
    return [
        b"\xC9\x0A",                                  # CMP #10
        b"\x90\x02",                                  # BCC +2 (C = 0)
        bytes([0x69, LETTER_A - DIGIT_0 - 10 - 1]),   # ADC #6 (C = 1: +7)
        bytes([0x69, DIGIT_0]),                       # ADC #$10 (C = 0 here)
    ]


def hex_half(half: int) -> list:
    """Writes bytes 128*half .. 128*half+127 of BUFFER as hex digits into HEX_SCREEN."""
    source = BUFFER + 128 * half
    target = HEX_SCREEN + 256 * half
    loop = f"hex{half}"
    return [
        b"\xA2\x00",                                  # LDX #0
        ("label", loop),
        b"\x8A", b"\x0A", b"\xA8",                    # TXA / ASL A / TAY (Y = 2X)
        bytes([0xBD, source & 0xFF, source >> 8]),    # LDA source,X
        b"\x4A\x4A\x4A\x4A",                          # LSR A x4 (high nibble)
        *nibble_to_screen_code(),
        bytes([0x99, target & 0xFF, target >> 8]),    # STA target,Y
        bytes([0xBD, source & 0xFF, source >> 8]),    # LDA source,X
        b"\x29\x0F",                                  # AND #$0F (low nibble)
        *nibble_to_screen_code(),
        bytes([0x99, (target + 1) & 0xFF, (target + 1) >> 8]),  # STA target+1,Y
        b"\xE8",                                      # INX
        b"\xE0\x80",                                  # CPX #128
        b"\xD0", ("rel", loop),                       # BNE loop
    ]


def probe_program():
    """Returns (block, labels): the bytes loaded at PROBE_ORG and the label addresses."""
    program = [
        # Boot record: flags, sectors to load (patched below), load address, init address.
        bytes([0x00, 0x00, PROBE_ORG & 0xFF, PROBE_ORG >> 8]), ("abs", "init"),
        # Read sector 65535 of D2 through SIOV with the whole DCB.
        *store(DRIVE_DEVICE, DDEVIC),
        *store(DRIVE_UNIT, DUNIT),
        *store(READ_COMMAND, DCOMND),
        *store(0x40, DSTATS),                         # data from the device to the Atari
        *store(BUFFER & 0xFF, DBUFLO),
        *store(BUFFER >> 8, DBUFHI),
        *store(READ_TIMEOUT_S, DTIMLO),
        *store(SECTOR_BYTES & 0xFF, DBYTLO),
        *store(SECTOR_BYTES >> 8, DBYTHI),
        *store(SECTOR_NUMBER & 0xFF, DAUX1),
        *store(SECTOR_NUMBER >> 8, DAUX2),
        bytes([0x20, SIOV & 0xFF, SIOV >> 8]),        # JSR SIOV
        b"\x8C", ("abs", "status"),                   # STY status
        # The status in hexadecimal.
        b"\xAD", ("abs", "status"),                   # LDA status
        b"\x4A\x4A\x4A\x4A",                          # LSR A x4
        *nibble_to_screen_code(),
        b"\x8D", ("abs", "status_digits"),            # STA status_digits
        b"\xAD", ("abs", "status"),                   # LDA status
        b"\x29\x0F",                                  # AND #$0F
        *nibble_to_screen_code(),
        b"\x8D", ("abs", "status_digits_lo"),         # STA status_digits+1
        # The 256 bytes, in two halves of 128 (Y = 2X stays below 256).
        *hex_half(0),
        *hex_half(1),
        # Show it: switch the display list and the playfield width during a vertical blank.
        bytes([0xA5, RTCLOK_LOW]),                    # LDA RTCLOK
        ("label", "vbl"),
        bytes([0xC5, RTCLOK_LOW]),                    # CMP RTCLOK
        b"\xF0", ("rel", "vbl"),                      # BEQ vbl
        *store(SDMCTL_NARROW, SDMCTL),
        b"\xA9", ("lo", "dl"), bytes([0x8D, SDLSTL & 0xFF, SDLSTL >> 8]),
        b"\xA9", ("hi", "dl"), bytes([0x8D, (SDLSTL + 1) & 0xFF, (SDLSTL + 1) >> 8]),
        ("label", "halt"),
        b"\x4C", ("abs", "halt"),                     # JMP halt
        ("label", "init"),
        b"\x60",                                      # RTS (DOSINI)
        # Display list: 24 blank lines, title and status (mode 2), 8 blank, 16 hex lines.
        ("label", "dl"),
        b"\x70\x70\x70\x42", ("abs", "text"), b"\x02",
        b"\x70\x42", ("abs", "hex_screen"), b"\x02" * 15,
        b"\x41", ("abs", "dl"),
        ("label", "text"), screen_codes(TITLE_TEXT, LINE_WIDTH),
        ("label", "status_line"), screen_codes(STATUS_TEXT, LINE_WIDTH),
        ("label", "status"), b"\x00",
    ]
    status_at = len(STATUS_TEXT)
    labels, pc = {}, PROBE_ORG
    sizes = {"lo": 1, "hi": 1, "abs": 2, "rel": 1}
    for item in program:
        if isinstance(item, tuple) and item[0] == "label":
            labels[item[1]] = pc
        else:
            pc += sizes[item[0]] if isinstance(item, tuple) else len(item)
    extra = {"hex_screen": HEX_SCREEN,
             "status_digits": labels["status_line"] + status_at,
             "status_digits_lo": labels["status_line"] + status_at + 1}
    labels.update(extra)
    code = assemble(program, PROBE_ORG, extra_labels=extra)
    sectors = -(-len(code) // BOOT_SECTOR_SIZE)
    block = bytearray(code + bytes(sectors * BOOT_SECTOR_SIZE - len(code)))
    block[1] = sectors
    if PROBE_ORG + len(block) > HEX_SCREEN:
        raise ValueError("the program reaches the hex screen")
    if labels["dl"] >> 10 != (labels["text"] - 1) >> 10:
        raise ValueError("the display list crosses a 1 KB boundary")
    if labels["text"] >> 12 != (labels["text"] + 2 * LINE_WIDTH - 1) >> 12:
        raise ValueError("the text lines cross a 4 KB boundary")
    return bytes(block), labels


def probe_image() -> bytes:
    """The whole ATR: single density, the probe in its first sectors, the rest zero."""
    block = probe_program()[0]
    data = bytearray(IMAGE_SECTORS * BOOT_SECTOR_SIZE)
    data[0:len(block)] = block
    return header(len(data), BOOT_SECTOR_SIZE) + bytes(data)


def main(argv: list) -> int:
    parser = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    parser.add_argument("--out", type=Path, required=True, help="folder to write SECTOR-PROBE.ATR into")
    args = parser.parse_args(argv)
    args.out.mkdir(parents=True, exist_ok=True)
    target = args.out / IMAGE_NAME
    target.write_bytes(probe_image())
    print(f"make_sector_probe: wrote {target}")
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
