#!/usr/bin/env python3
"""Generate the 6502 .xex loader of retrobridge_host (spec menu-xex) as a C++ include.

When the menu boots a .xex, the host serves in D1 a virtual disk: the loader sectors (this block, with the
.xex length patched in at kXexLoaderLengthOffset) and then the raw .xex bytes, 128 bytes per sector. The OS
boots the loader at LOADER_ORG; the loader is an original 6502 program written for RetroBridge (ADR-012; no code
from FujiNet, emulators or other loaders). Sources: docs/fuentes.md (menu-xex).

What it does:
  1. turns the XL/XE BASIC off: PORTB ($D301) bit 1 set to 1, other bits kept (Altirra HRM 2.6);
     RAMTOP and the screen editor are left alone (plan menu-xex, deviation 1);
  2. points RUNAD at its own stop loop, so a .xex without RUNAD stops there;
  3. reads the data sectors one by one with SIOV (command 'R' to D1) into its buffer and walks the .xex as a
     stream: an optional $FFFF header, start, end (inclusive) and the data of each segment, copied to its
     address through a store whose operand it rewrites;
  4. before each segment points INITAD at its own RTS and after it does JSR (INITAD), so only a routine the
     segment wrote runs, before the next segment is read;
  5. after the last byte, JMP (RUNAD);
  6. on a read error shows READ_ERROR_TEXT with its own display list and stops.

The host rejects a .xex whose segments touch kXexLoaderLow..kXexLoaderHigh (the loader and its buffer).

Usage: python tools/make_xex_loader.py --out host/src/xex_loader.inc
       python tools/make_xex_loader.py --check host/src/xex_loader.inc   (exit 1 if stale)
"""

import argparse
import sys
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))
from make_atr import BOOT_SECTOR_SIZE, RTCLOK_LOW, SDLSTL, assemble  # noqa: E402
from make_menu import (DAUX1, DAUX2, DBUFHI, DBUFLO, DBYTHI, DBYTLO, DCOMND, DDEVIC, DSTATS,  # noqa: E402
                       DTIMLO, DUNIT, SIOV, screen_codes)

LOADER_ORG = 0x0700
PORTB = 0xD301
BASIC_OFF = 0x02             # PORTB bit 1: a 1 disables the BASIC ROM (Altirra HRM 2.6)
RUNAD, INITAD = 0x02E0, 0x02E2
DISK_DEVICE = 0x31
READ_COMMAND = 0x52          # 'R'
DISK_TIMEOUT_S = 7
READ_ERROR_TEXT = " ERROR DE LECTURA XEX"


def loader_program():
    store = lambda value, address: [bytes([0xA9, value, 0x8D, address & 0xFF, address >> 8])]  # noqa: E731
    absolute = lambda opcode, address: bytes([opcode, address & 0xFF, address >> 8])  # noqa: E731
    return [
        # Boot record: flags, sectors to load (patched), load address, init address (an RTS).
        bytes([0x00, 0x00, LOADER_ORG & 0xFF, LOADER_ORG >> 8]), ("abs", "noinit"),
        absolute(0xAD, PORTB),                       # LDA PORTB
        bytes([0x09, BASIC_OFF]),                    # ORA #2
        absolute(0x8D, PORTB),                       # STA PORTB (BASIC off)
        b"\xA9", ("lo", "norun"), absolute(0x8D, RUNAD),
        b"\xA9", ("hi", "norun"), absolute(0x8D, RUNAD + 1),
        # One segment.
        ("label", "seg"),
        b"\xA9", ("lo", "noinit"), absolute(0x8D, INITAD),
        b"\xA9", ("hi", "noinit"), absolute(0x8D, INITAD + 1),
        b"\x20", ("abs", "getb"), b"\xB0", ("rel", "done"),  # JSR getb / BCS done
        b"\x8D", ("abs", "dest"),
        b"\x20", ("abs", "getb"), b"\xB0", ("rel", "done"),
        b"\x8D", ("abs", "dest_hi"),
        b"\x2D", ("abs", "dest"),                    # AND dest
        b"\xC9\xFF",                                 # CMP #$FF
        b"\xD0", ("rel", "start"),                   # BNE start ($FFFF: a header, the start follows)
        b"\x20", ("abs", "getb"), b"\xB0", ("rel", "done"),
        b"\x8D", ("abs", "dest"),
        b"\x20", ("abs", "getb"), b"\xB0", ("rel", "done"),
        b"\x8D", ("abs", "dest_hi"),
        ("label", "start"),
        b"\x20", ("abs", "getb"), b"\xB0", ("rel", "done"),
        b"\x8D", ("abs", "end_lo"),
        b"\x20", ("abs", "getb"), b"\xB0", ("rel", "done"),
        b"\x8D", ("abs", "end_hi"),
        ("label", "copy"),
        b"\x20", ("abs", "getb"), b"\xB0", ("rel", "done"),
        b"\x8D", ("label", "dest"), b"\x00", ("label", "dest_hi"), b"\x00",  # STA (rewritten)
        b"\xAD", ("abs", "dest"), b"\xCD", ("abs", "end_lo"),
        b"\xD0", ("rel", "next"),
        b"\xAD", ("abs", "dest_hi"), b"\xCD", ("abs", "end_hi"),
        b"\xF0", ("rel", "seg_end"),
        ("label", "next"),
        b"\xEE", ("abs", "dest"),
        b"\xD0", ("rel", "copy"),
        b"\xEE", ("abs", "dest_hi"),
        b"\x4C", ("abs", "copy"),
        ("label", "seg_end"),
        b"\x20", ("abs", "callinit"),
        b"\x4C", ("abs", "seg"),
        ("label", "done"),
        absolute(0x6C, RUNAD),                       # JMP (RUNAD)
        ("label", "callinit"),
        absolute(0x6C, INITAD),                      # JMP (INITAD): its RTS returns to seg_end
        ("label", "noinit"),
        b"\x60",                                     # RTS
        ("label", "norun"),
        b"\x4C", ("abs", "norun"),                   # stop: no RUNAD
        # getb: next .xex byte in A, carry clear; carry set after the last byte.
        ("label", "getb"),
        b"\xAD", ("abs", "remain"), b"\x0D", ("abs", "remain1"), b"\x0D", ("abs", "remain2"),
        b"\xD0", ("rel", "more"),
        b"\x38\x60",                                 # SEC / RTS
        ("label", "more"),
        b"\xAD", ("abs", "remain"), b"\xD0", ("rel", "dec0"),
        b"\xAD", ("abs", "remain1"), b"\xD0", ("rel", "dec1"),
        b"\xCE", ("abs", "remain2"),
        ("label", "dec1"),
        b"\xCE", ("abs", "remain1"),
        ("label", "dec0"),
        b"\xCE", ("abs", "remain"),
        b"\xAE", ("abs", "pos"),                     # LDX pos
        bytes([0xE0, BOOT_SECTOR_SIZE]),             # CPX #128
        b"\x90", ("rel", "have"),                    # BCC have
        b"\x20", ("abs", "readsec"),
        ("label", "have"),
        b"\xBD", ("abs", "buffer"),                  # LDA buffer,X
        b"\xE8",                                     # INX
        b"\x8E", ("abs", "pos"),                     # STX pos
        b"\x18\x60",                                 # CLC / RTS
        # readsec: the next data sector into the buffer; X = 0 after it.
        ("label", "readsec"),
        *store(DISK_DEVICE, DDEVIC), *store(0x01, DUNIT), *store(READ_COMMAND, DCOMND),
        *store(0x40, DSTATS), *store(DISK_TIMEOUT_S, DTIMLO),
        *store(BOOT_SECTOR_SIZE, DBYTLO), *store(0x00, DBYTHI),
        b"\xA9", ("lo", "buffer"), absolute(0x8D, DBUFLO),
        b"\xA9", ("hi", "buffer"), absolute(0x8D, DBUFHI),
        b"\xAD", ("abs", "sector"), absolute(0x8D, DAUX1),
        b"\xAD", ("abs", "sector_hi"), absolute(0x8D, DAUX2),
        absolute(0x20, SIOV),
        b"\x98",                                     # TYA (SIO status)
        b"\x30", ("rel", "rerr"),                    # BMI rerr
        b"\xEE", ("abs", "sector"),
        b"\xD0", ("rel", "read_ok"),
        b"\xEE", ("abs", "sector_hi"),
        ("label", "read_ok"),
        b"\xA2\x00",                                 # LDX #0
        b"\x60",
        # rerr: show the message with the loader's own display list, during a vertical blank.
        ("label", "rerr"),
        bytes([0xA5, RTCLOK_LOW]),
        ("label", "rerr_vbl"),
        bytes([0xC5, RTCLOK_LOW]),
        b"\xF0", ("rel", "rerr_vbl"),
        b"\xA9", ("lo", "error_dl"), absolute(0x8D, SDLSTL),
        b"\xA9", ("hi", "error_dl"), absolute(0x8D, SDLSTL + 1),
        ("label", "stop"),
        b"\x4C", ("abs", "stop"),
        ("label", "error_dl"),
        bytes([0x70, 0x70, 0x70, 0x42]),             # 24 blank lines, mode 2 with LMS
        ("label", "error_lms"), ("abs", "error_text"),
        b"\x41", ("abs", "error_dl"),                # JVB
        ("label", "error_text"), screen_codes(READ_ERROR_TEXT),
        # Variables (loaded with the block).
        ("label", "pos"), bytes([BOOT_SECTOR_SIZE]),  # 128: the buffer is empty
        ("label", "sector"), b"\x00",                # first data sector (patched)
        ("label", "sector_hi"), b"\x00",
        ("label", "remain"), b"\x00",                # .xex bytes left, 3 bytes (patched by the host)
        ("label", "remain1"), b"\x00",
        ("label", "remain2"), b"\x00",
        ("label", "end_lo"), b"\x00",
        ("label", "end_hi"), b"\x00",
        ("label", "end"),
    ]


def loader_block():
    """Returns (block, labels): the boot sectors and the label addresses."""
    program = loader_program()
    labels, pc = {}, LOADER_ORG
    sizes = {"lo": 1, "hi": 1, "abs": 2, "rel": 1}
    for item in program:
        if isinstance(item, tuple) and item[0] == "label":
            labels[item[1]] = pc
        else:
            pc += sizes[item[0]] if isinstance(item, tuple) else len(item)
    sectors = -(-(labels["end"] - LOADER_ORG) // BOOT_SECTOR_SIZE)
    # The buffer is RAM after the loaded sectors; it is never read from the disk.
    labels["buffer"] = LOADER_ORG + sectors * BOOT_SECTOR_SIZE
    code = assemble(program, LOADER_ORG, extra_labels={"buffer": labels["buffer"]})
    block = bytearray(code + bytes(sectors * BOOT_SECTOR_SIZE - len(code)))
    block[1] = sectors
    block[labels["sector"] - LOADER_ORG] = sectors + 1
    if labels["error_dl"] >> 10 != (labels["error_lms"] + 4) >> 10:
        raise ValueError("the error display list crosses a 1 KB boundary")
    return bytes(block), labels


def loader_low() -> int:
    return LOADER_ORG


def loader_high() -> int:
    return loader_block()[1]["buffer"] + BOOT_SECTOR_SIZE - 1


LENGTH_OFFSET_IN_BLOCK = loader_block()[1]["remain"] - LOADER_ORG


def render_include() -> str:
    block, _ = loader_block()
    lines = [
        "// Generated by tools/make_xex_loader.py (spec menu-xex). Do not edit: regenerate with",
        "//   python tools/make_xex_loader.py --out host/src/xex_loader.inc",
        "// The tools tests fail if this file differs from the generator's output.",
        "// Original 6502 .xex loader for RetroBridge (ADR-012); sources in docs/fuentes.md.",
        "",
        f"constexpr unsigned kXexLoaderSectors = {block[1]};",
        f"constexpr unsigned kXexLoaderLengthOffset = {LENGTH_OFFSET_IN_BLOCK};  // 3 bytes, little-endian",
        f"constexpr unsigned kXexLoaderLow = 0x{loader_low():04X};",
        f"constexpr unsigned kXexLoaderHigh = 0x{loader_high():04X};  // inclusive: loader and its buffer",
        f"constexpr unsigned char kXexLoaderBlock[{len(block)}] = {{",
    ]
    for i in range(0, len(block), 16):
        lines.append("    " + ", ".join(f"0x{b:02X}" for b in block[i:i + 16]) + ",")
    lines.append("};")
    return "\n".join(lines) + "\n"


def main(argv: list) -> int:
    parser = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    group = parser.add_mutually_exclusive_group(required=True)
    group.add_argument("--out", type=Path, help="write the C++ include here")
    group.add_argument("--check", type=Path, help="exit 1 if this include differs from the generated one")
    args = parser.parse_args(argv)
    text = render_include()
    if args.check:
        current = args.check.read_bytes().decode("ascii").replace("\r\n", "\n") if args.check.exists() else ""
        if current != text:
            print(f"make_xex_loader: {args.check} is stale; regenerate it with --out", file=sys.stderr)
            return 1
        print(f"make_xex_loader: {args.check} is up to date")
        return 0
    with args.out.open("w", encoding="ascii", newline="\n") as f:
        f.write(text)
    print(f"make_xex_loader: wrote {args.out}")
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
