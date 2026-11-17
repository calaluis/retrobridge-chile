#!/usr/bin/env python3
"""Generate the 6502 boot menu of retrobridge_host (Issue #38) as a C++ include.

The menu is the D1 disk the Atari boots after a cold reset when retrobridge_host
runs with --images. Its first MENU_LOAD_SECTORS sectors hold a disk boot record,
an original 6502 program (written for RetroBridge; no code from FujiNet or
emulators, ADR-012), its display list and the screen text. The host copies this
block into an in-memory ATR and writes the catalog into the entry lines and the
entry count before serving it.

The program:
  1. shows its own display list: a title, 20 entry lines, a prompt and a status
     line, all ANTIC mode 2 (40 columns);
  2. waits for a key in CH ($02FC) and maps it to an entry with a table of the
     keyboard codes of A..T (Shift and Control ignored); keys outside the list
     do nothing;
  3. sends command 'M' to the RetroBridge menu device ($5A, ADR-017) with AUX1 =
     entry index, reading 41 bytes: a status byte (0 mounted, 1 refused) and a
     40-column line in screen codes;
  4. status 0: jumps to the OS cold start (COLDSV), which boots the image the
     host has just mounted as D1. Status 1: shows the host's line and waits for
     another key. SIOV failure: shows MENU_NO_HOST_TEXT and waits for a key.

Usage: python tools/make_menu.py --out host/src/menu_program.inc
       python tools/make_menu.py --check host/src/menu_program.inc   (exit 1 if stale)
"""

import argparse
import sys
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))
from make_atr import BOOT_SECTOR_SIZE, RTCLOK_LOW, SDLSTL, assemble  # noqa: E402

MENU_ORG = 0x0700
MENU_DEVICE = 0x5A           # SIO device ID of the menu service (ADR-017)
MENU_COMMAND = 0x4D          # 'M': mount entry AUX1 as D1
MENU_REPLY_SIZE = 41         # status byte + one 40-column line
MENU_TIMEOUT_S = 7           # DTIMLO: seconds the OS waits for the device
MENU_MAX_ENTRIES = 20
LINE_WIDTH = 40

MENU_TITLE_TEXT = " RETROBRIDGE D1 - ELIGE UNA IMAGEN"
MENU_PROMPT_TEXT = " PULSA LA LETRA DE LA IMAGEN"
MENU_NO_HOST_TEXT = " ERROR: EL HOST NO RESPONDE"

# OS addresses (Atari 400/800/XL OS, documented entry points).
CH = 0x02FC                  # last key code, 0xFF when none
DDEVIC, DUNIT, DCOMND, DSTATS = 0x0300, 0x0301, 0x0302, 0x0303
DBUFLO, DBUFHI, DTIMLO = 0x0304, 0x0305, 0x0306
DBYTLO, DBYTHI, DAUX1, DAUX2 = 0x0308, 0x0309, 0x030A, 0x030B
SIOV = 0xE459
COLDSV = 0xE477

# Keyboard codes of A..T, the entry letters.
KEY_CODES = bytes([
    0x3F, 0x15, 0x12, 0x3A, 0x2A, 0x38, 0x3D, 0x39, 0x0D, 0x01,   # A B C D E F G H I J
    0x05, 0x00, 0x25, 0x23, 0x08, 0x0A, 0x2F, 0x28, 0x3E, 0x2D,   # K L M N O P Q R S T
])


def screen_line(text: str) -> bytes:
    """ATASCII 0x20-0x7E to ANTIC screen codes, left-aligned in LINE_WIDTH columns."""
    if len(text) > LINE_WIDTH or any(not 0x20 <= ord(c) <= 0x7E for c in text):
        raise ValueError(f"text not representable in one mode 2 line: {text!r}")
    line = text.ljust(LINE_WIDTH)
    return bytes(ord(c) - 0x20 if ord(c) < 0x60 else ord(c) for c in line)


def abs_op(opcode: int, address: int) -> bytes:
    return bytes([opcode, address & 0xFF, address >> 8])


def menu_program():
    """Returns (block, labels): the bytes loaded at MENU_ORG and the label addresses."""
    store = lambda value, address: [bytes([0xA9, value]), abs_op(0x8D, address)]  # noqa: E731
    program = [
        # Boot record: flags, sectors to load (patched below), load address, init address.
        bytes([0x00, 0x00, MENU_ORG & 0xFF, MENU_ORG >> 8]), ("abs", "init"),
        # Switch to the menu display list during a vertical blank.
        bytes([0xA5, RTCLOK_LOW]),                    # LDA RTCLOK
        ("label", "vbl"),
        bytes([0xC5, RTCLOK_LOW]),                    # CMP RTCLOK
        b"\xF0", ("rel", "vbl"),                      # BEQ vbl
        b"\xA9", ("lo", "dl"), abs_op(0x8D, SDLSTL),  # LDA #<dl / STA SDLSTL
        b"\xA9", ("hi", "dl"), abs_op(0x8D, SDLSTL + 1),
        *store(0xFF, CH),                             # forget any key pressed during boot
        ("label", "key"),
        abs_op(0xAD, CH),                             # LDA CH
        b"\xC9\xFF",                                  # CMP #$FF
        b"\xF0", ("rel", "key"),                      # BEQ key
        b"\xA2\xFF", abs_op(0x8E, CH),                # LDX #$FF / STX CH (consume the key)
        b"\x29\x3F",                                  # AND #$3F (drop Shift and Control)
        b"\xA2\x00",                                  # LDX #0
        ("label", "find"),
        b"\xEC", ("abs", "count"),                    # CPX count
        b"\xF0", ("rel", "key"),                      # BEQ key (not an entry letter)
        b"\xDD", ("abs", "keytab"),                   # CMP keytab,X
        b"\xF0", ("rel", "found"),                    # BEQ found
        b"\xE8",                                      # INX
        b"\xD0", ("rel", "find"),                     # BNE find (always)
        ("label", "found"),
        abs_op(0x8E, DAUX1),                          # STX DAUX1 (entry index)
        *store(MENU_DEVICE, DDEVIC),
        *store(0x01, DUNIT),
        *store(MENU_COMMAND, DCOMND),
        *store(0x40, DSTATS),                         # data from the device to the Atari
        b"\xA9", ("lo", "reply"), abs_op(0x8D, DBUFLO),
        b"\xA9", ("hi", "reply"), abs_op(0x8D, DBUFHI),
        *store(MENU_TIMEOUT_S, DTIMLO),
        *store(MENU_REPLY_SIZE, DBYTLO),
        *store(0x00, DBYTHI),
        abs_op(0x8D, DAUX2),                          # STA DAUX2 (A is still 0)
        abs_op(0x20, SIOV),                           # JSR SIOV
        b"\x98",                                      # TYA (SIO status)
        b"\x30", ("rel", "nohost"),                   # BMI nohost (negative status: error)
        b"\xAD", ("abs", "reply"),                    # LDA reply (host status byte)
        b"\xD0", ("rel", "refused"),                  # BNE refused
        abs_op(0x4C, COLDSV),                         # JMP COLDSV: boot the new D1
        ("label", "nohost"),
        b"\xA9", ("lo", "nohost_text"),
        b"\xA2", ("hi", "nohost_text"),
        b"\xD0", ("rel", "show"),                     # BNE show (high byte is never 0)
        ("label", "refused"),
        b"\xA9", ("lo", "reply_text"),
        b"\xA2", ("hi", "reply_text"),
        ("label", "show"),
        b"\x8D", ("abs", "dl_status"),                # STA dl_status
        b"\x8E", ("abs", "dl_status_hi"),             # STX dl_status+1
        b"\x4C", ("abs", "key"),                      # JMP key
        ("label", "init"),
        b"\x60",                                      # RTS (DOSINI)
        # Display list: 16 blank lines, title, 4 blank, 20 entries, 4 blank, prompt, status, jump.
        ("label", "dl"),
        b"\x70\x70\x42", ("abs", "screen"),
        b"\x30", bytes([0x02] * MENU_MAX_ENTRIES),
        b"\x30\x02",
        b"\x42",
        ("label", "dl_status"), ("lo", "reply_text"),
        ("label", "dl_status_hi"), ("hi", "reply_text"),
        b"\x41", ("abs", "dl"),
        ("label", "dl_end"),
        ("label", "keytab"), KEY_CODES,
        ("label", "count"), b"\x00",
        # Title, MENU_MAX_ENTRIES entry lines (filled by the host) and the prompt, contiguous.
        ("label", "screen"), screen_line(MENU_TITLE_TEXT),
        ("label", "entries"), bytes(LINE_WIDTH * MENU_MAX_ENTRIES),
        screen_line(MENU_PROMPT_TEXT),
        ("label", "reply"), b"\x00",
        ("label", "reply_text"), bytes(LINE_WIDTH),
        ("label", "nohost_text"), screen_line(MENU_NO_HOST_TEXT),
        ("label", "end"),
    ]
    code = assemble(program, MENU_ORG)
    # Label addresses, computed the same way as the assembler's first pass.
    labels, pc = {}, MENU_ORG
    sizes = {"lo": 1, "hi": 1, "abs": 2, "rel": 1}
    for item in program:
        if isinstance(item, tuple) and item[0] == "label":
            labels[item[1]] = pc
        else:
            pc += sizes[item[0]] if isinstance(item, tuple) else len(item)
    sectors = -(-len(code) // BOOT_SECTOR_SIZE)
    block = bytearray(code + bytes(sectors * BOOT_SECTOR_SIZE - len(code)))
    block[1] = sectors
    if labels["dl"] >> 10 != (labels["dl_end"] - 1) >> 10:
        raise ValueError("the display list crosses a 1 KB boundary")
    if labels["screen"] >> 12 != (labels["nohost_text"] + LINE_WIDTH - 1) >> 12:
        raise ValueError("the screen text crosses a 4 KB boundary")
    return bytes(block), labels


def render_include(block: bytes, labels: dict) -> str:
    offset = lambda name: labels[name] - MENU_ORG  # noqa: E731
    lines = [
        "// Generated by tools/make_menu.py (Issue #38). Do not edit: regenerate with",
        "//   python tools/make_menu.py --out host/src/menu_program.inc",
        "// The tools tests fail if this file differs from the generator's output.",
        "// Original 6502 program for RetroBridge (ADR-012); protocol in ADR-017.",
        "",
        f"constexpr unsigned kMenuLoadSectors = {block[1]};",
        f"constexpr unsigned kMenuMaxEntries = {MENU_MAX_ENTRIES};",
        f"constexpr unsigned kMenuLineWidth = {LINE_WIDTH};",
        f"constexpr unsigned kMenuCountOffset = {offset('count')};",
        f"constexpr unsigned kMenuEntriesOffset = {offset('entries')};",
        f"constexpr unsigned char kMenuDevice = 0x{MENU_DEVICE:02X};",
        f"constexpr unsigned char kMenuCommand = 0x{MENU_COMMAND:02X};",
        f"constexpr unsigned kMenuReplySize = {MENU_REPLY_SIZE};",
        f"constexpr unsigned char kMenuBlock[{len(block)}] = {{",
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
    text = render_include(*menu_program())
    if args.check:
        # A Windows checkout may turn LF into CRLF (core.autocrlf); compare the text, not the line endings.
        current = args.check.read_bytes().decode("ascii").replace("\r\n", "\n") if args.check.exists() else ""
        if current != text:
            print(f"make_menu: {args.check} is stale; regenerate it with --out", file=sys.stderr)
            return 1
        print(f"make_menu: {args.check} is up to date")
        return 0
    with args.out.open("w", encoding="ascii", newline="\n") as f:
        f.write(text)
    print(f"make_menu: wrote {args.out}")
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
