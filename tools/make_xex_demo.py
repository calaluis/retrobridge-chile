#!/usr/bin/env python3
"""Generate RetroBridge's own test .xex for the demo of spec menu-xex (X005).

An original 6502 program (ADR-012), in three segments:
  1. the program at DEMO_ORG: a display list of two mode 2 lines, its texts and a marker byte;
  2. INITAD ($02E2) = `init`, which sets the marker; the loader calls it after this segment;
  3. RUNAD ($02E0) = `main`.
`main` shows OK_TEXT if `init` ran (NO_INIT_TEXT if not) and, on the second line, BASIC_OFF_TEXT if
PORTB bit 1 is 1 (BASIC_ON_TEXT if not), then stays there. On the Atari this proves AC5, AC6 and AC7 at
a glance. Format source: docs/fuentes.md (menu-xex).

Usage: python tools/make_xex_demo.py --out D:/rb-e0/m48-xex/images/RBPRUEBA.XEX
"""

import argparse
import sys
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))
from make_atr import RTCLOK_LOW, SDLSTL, assemble  # noqa: E402
from make_menu import screen_codes  # noqa: E402
from make_xex_loader import BASIC_OFF, INITAD, PORTB, RUNAD  # noqa: E402

DEMO_ORG = 0x2000
OK_TEXT = " RETROBRIDGE XEX OK"
NO_INIT_TEXT = " RETROBRIDGE XEX: INITAD NO CORRIO"
BASIC_OFF_TEXT = " BASIC APAGADO"
BASIC_ON_TEXT = " BASIC ENCENDIDO"


def program():
    absolute = lambda opcode, address: bytes([opcode, address & 0xFF, address >> 8])  # noqa: E731
    return [
        ("label", "main"),
        b"\xAD", ("abs", "marker"),                # LDA marker
        b"\xD0", ("rel", "init_ok"),               # BNE init_ok
        b"\xA9", ("lo", "no_init_text"), b"\x8D", ("abs", "lms1"),
        b"\xA9", ("hi", "no_init_text"), b"\x8D", ("abs", "lms1_hi"),
        ("label", "init_ok"),
        absolute(0xAD, PORTB),                     # LDA PORTB
        bytes([0x29, BASIC_OFF]),                  # AND #2
        b"\xD0", ("rel", "basic_off"),             # BNE basic_off
        b"\xA9", ("lo", "basic_on_text"), b"\x8D", ("abs", "lms2"),
        b"\xA9", ("hi", "basic_on_text"), b"\x8D", ("abs", "lms2_hi"),
        ("label", "basic_off"),
        bytes([0xA5, RTCLOK_LOW]),                 # LDA RTCLOK
        ("label", "vbl"),
        bytes([0xC5, RTCLOK_LOW]),                 # CMP RTCLOK
        b"\xF0", ("rel", "vbl"),                   # BEQ vbl (switch during a vertical blank)
        b"\xA9", ("lo", "dl"), absolute(0x8D, SDLSTL),
        b"\xA9", ("hi", "dl"), absolute(0x8D, SDLSTL + 1),
        ("label", "halt"),
        b"\x4C", ("abs", "halt"),
        ("label", "init"),                         # INITAD: called by the loader after segment 1
        b"\xA9\x01", b"\x8D", ("abs", "marker"),
        b"\x60",
        ("label", "dl"),
        bytes([0x70, 0x70, 0x70, 0x42]),           # 24 blank lines, mode 2 with LMS
        ("label", "lms1"), ("lo", "ok_text"), ("label", "lms1_hi"), ("hi", "ok_text"),
        b"\x42",
        ("label", "lms2"), ("lo", "basic_off_text"), ("label", "lms2_hi"), ("hi", "basic_off_text"),
        b"\x41", ("abs", "dl"),                    # JVB
        ("label", "ok_text"), screen_codes(OK_TEXT),
        ("label", "no_init_text"), screen_codes(NO_INIT_TEXT),
        ("label", "basic_off_text"), screen_codes(BASIC_OFF_TEXT),
        ("label", "basic_on_text"), screen_codes(BASIC_ON_TEXT),
        ("label", "marker"), b"\x00",
        ("label", "end"),
    ]


def labels() -> dict:
    found, pc = {}, DEMO_ORG
    sizes = {"lo": 1, "hi": 1, "abs": 2, "rel": 1}
    for item in program():
        if isinstance(item, tuple) and item[0] == "label":
            found[item[1]] = pc
        else:
            pc += sizes[item[0]] if isinstance(item, tuple) else len(item)
    return found


def program_block() -> bytes:
    block = assemble(program(), DEMO_ORG)
    found = labels()
    if found["dl"] >> 10 != (found["end"] - 1) >> 10:
        raise ValueError("the display list and its texts cross a 1 KB boundary")
    return block


def segment(start: int, data: bytes) -> bytes:
    end = start + len(data) - 1
    return b"\xFF\xFF" + bytes([start & 0xFF, start >> 8, end & 0xFF, end >> 8]) + data


def demo_xex() -> bytes:
    found = labels()
    word = lambda value: bytes([value & 0xFF, value >> 8])  # noqa: E731
    return (segment(DEMO_ORG, program_block()) + segment(INITAD, word(found["init"]))
            + segment(RUNAD, word(found["main"])))


def main(argv: list) -> int:
    parser = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    parser.add_argument("--out", type=Path, required=True, help="write the .xex here")
    args = parser.parse_args(argv)
    args.out.write_bytes(demo_xex())
    print(f"make_xex_demo: wrote {args.out}")
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
