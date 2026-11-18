#!/usr/bin/env python3
"""Generate the 6502 menu of retrobridge_host (Issues #38 and #40, menu-tnfs) as a C++ include.

The menu is the D1 disk the Atari boots after a cold reset when retrobridge_host
runs with --images. Its first sectors hold a disk boot record, an original 6502
program (written for RetroBridge; no code from FujiNet or emulators, ADR-012),
two display lists and the screen buffer. The host serves this block as it is.

The program is a thin terminal (ADR-017, amended for Issue #40):
  1. sets the colors of the main screen and its display list;
  2. sends command 'K' to the RetroBridge menu device ($5A) with AUX1 = $FF
     (no key: draw the current screen) and AUX2 = a sequence number that
     changes with every key (SIOV's own retries repeat it, so the host can
     answer a retry without acting twice), and reads REPLY_SIZE bytes into the
     reply buffer: an action byte, a layout byte and SCREEN_SIZE bytes of
     screen codes, which the display lists show directly;
  3. layout 0 shows the main screen, layout 1 the list of a source;
  4. action 1 jumps to the OS cold start (COLDSV), which boots the image the
     host has just put on D1; action 0 waits for a key in CH ($02FC) and sends
     its keyboard code with 'K' (the host decides what it means); action 2
     ("wait", menu-tnfs) watches CH for about WAIT_TICKS frames and then asks
     again with the poll code POLL and a new AUX2; a key pressed meanwhile is
     sent instead of the poll. Every key is sent as CH AND KEY_MASK, without
     the Shift ($40) and Control ($80) bits, so a key is $00-$3F and can never
     be POLL ($FE) or NO_KEY ($FF) (menu-tnfs, M41-3);
  5. if SIOV fails, writes NO_HOST_TEXT into the message line of the main
     screen, shows it and waits for a key.

The main screen follows the reference approved for Issue #40 (Display List Lab
mock-up): mode 7 title, 8 source lines, mode 6 subtitle, 11 status lines and a
two-row footer with the keys in inverse video between the graphic characters
$42 and $56. Addresses are the program's own, not the mock-up's.

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
MENU_COMMAND = 0x4B          # 'K': a key (AUX1), answered with the screen to show
NO_KEY = 0xFF                # AUX1 when no key was pressed: just draw
POLL = 0xFE                  # AUX1 after action 2: no key, how is the job going?
KEY_MASK = 0x3F              # a key is sent without the Shift and Control bits
WAIT_TICKS = 15              # action 2: frames (RTCLOK) to watch CH before polling
MENU_TIMEOUT_S = 7           # DTIMLO: seconds the OS waits for the device
LINE_WIDTH = 40
TITLE_WIDTH = 20             # ANTIC modes 6 and 7: 20 characters per line
SCREEN_SIZE = 960            # the largest layout (900 bytes) rounded to 24 lines
REPLY_SIZE = 2 + SCREEN_SIZE  # action, layout, screen

ACTION_SHOW, ACTION_BOOT, ACTION_WAIT = 0, 1, 2
LAYOUT_MAIN, LAYOUT_LIST = 0, 1

# Main screen (layout 0): offsets in the screen buffer.
MAIN_TITLE = 0
MAIN_SOURCES = MAIN_TITLE + TITLE_WIDTH            # 8 lines
MAIN_SUBTITLE = MAIN_SOURCES + 8 * LINE_WIDTH
MAIN_STATUS = MAIN_SUBTITLE + TITLE_WIDTH          # 11 lines: blank, D1..D8, C:, blank
MAIN_DRIVES = MAIN_STATUS + LINE_WIDTH
MAIN_TAPE = MAIN_DRIVES + 8 * LINE_WIDTH
MAIN_FOOTER = MAIN_STATUS + 11 * LINE_WIDTH        # 2 lines
MAIN_MESSAGE = MAIN_FOOTER + 2 * LINE_WIDTH        # below the footer (asked in the review of Issue #40)
MAIN_END = MAIN_MESSAGE + LINE_WIDTH

# List of a source (layout 1).
LIST_TITLE = 0
LIST_ENTRIES = LIST_TITLE + TITLE_WIDTH            # 20 lines
LIST_MAX_ENTRIES = 20
LIST_FOOTER = LIST_ENTRIES + LIST_MAX_ENTRIES * LINE_WIDTH  # 1 line
LIST_MESSAGE = LIST_FOOTER + LINE_WIDTH            # below the footer, as on the main screen
LIST_END = LIST_MESSAGE + LINE_WIDTH

MAIN_TITLE_TEXT = " RETROBRIDGE CHILE  "
MAIN_SUBTITLE_TEXT = " ESTADO DE UNIDADES "
NO_HOST_TEXT = " ERROR: EL HOST NO RESPONDE"

# Colors of the reference (OS shadows COLOR0..COLOR4) and CHACTL.
COLORS = {0x02C4: 0x0E, 0x02C5: 0x0E, 0x02C6: 0xA0, 0x02C7: 0x46, 0x02C8: 0xA0}
CHACT, CHACTL_VALUE = 0x02F3, 0x02

# Graphic characters around each key of the footer (screen codes).
KEY_LEFT, KEY_RIGHT = 0x42, 0x56

# OS addresses (Atari 400/800/XL OS, documented entry points).
CH = 0x02FC                  # last key code, 0xFF when none
DDEVIC, DUNIT, DCOMND, DSTATS = 0x0300, 0x0301, 0x0302, 0x0303
DBUFLO, DBUFHI, DTIMLO = 0x0304, 0x0305, 0x0306
DBYTLO, DBYTHI, DAUX1, DAUX2 = 0x0308, 0x0309, 0x030A, 0x030B
SIOV = 0xE459
COLDSV = 0xE477


def screen_codes(text: str, width: int = LINE_WIDTH) -> bytes:
    """ATASCII 0x20-0x7E to ANTIC screen codes, left-aligned in `width` columns."""
    if len(text) > width or any(not 0x20 <= ord(c) <= 0x7E for c in text):
        raise ValueError(f"text not representable in {width} columns: {text!r}")
    line = text.ljust(width)
    return bytes(ord(c) - 0x20 if ord(c) < 0x60 else ord(c) for c in line)


def screen_line(text: str) -> bytes:
    return screen_codes(text, LINE_WIDTH)


def inverse(text: str) -> bytes:
    return bytes(b | 0x80 for b in screen_codes(text, len(text)))


def footer_key(key: str, label: str) -> bytes:
    return bytes([KEY_LEFT]) + inverse(key) + bytes([KEY_RIGHT]) + screen_codes(label, len(label))


def main_footer() -> bytes:
    rows = (footer_key("1-8", " ABRIR FUENTE") + footer_key("E", " EDITAR SERVIDOR"),
            footer_key("A", "   ARRANCAR D1 ") + footer_key("U", " GESTIONAR UNIDADES"))
    return b"".join(row + bytes(LINE_WIDTH - len(row)) for row in rows)


def list_footer() -> bytes:
    row = footer_key("A-T", " ELEGIR IMAGEN  ") + footer_key("ESC", " VOLVER")
    return row + bytes(LINE_WIDTH - len(row))


def main_template() -> bytes:
    """The main screen with its fixed parts: title, subtitle and footer."""
    screen = bytearray(SCREEN_SIZE)
    screen[MAIN_TITLE:MAIN_TITLE + TITLE_WIDTH] = screen_codes(MAIN_TITLE_TEXT, TITLE_WIDTH)
    screen[MAIN_SUBTITLE:MAIN_SUBTITLE + TITLE_WIDTH] = screen_codes(MAIN_SUBTITLE_TEXT, TITLE_WIDTH)
    screen[MAIN_FOOTER:MAIN_MESSAGE] = main_footer()
    return bytes(screen)


def list_template() -> bytes:
    screen = bytearray(SCREEN_SIZE)
    screen[LIST_FOOTER:LIST_MESSAGE] = list_footer()
    return bytes(screen)


def abs_op(opcode: int, address: int) -> bytes:
    return bytes([opcode, address & 0xFF, address >> 8])


def menu_program():
    """Returns (block, labels): the bytes loaded at MENU_ORG and the label addresses."""
    store = lambda value, address: [bytes([0xA9, value]), abs_op(0x8D, address)]  # noqa: E731
    colors = []
    for address, value in COLORS.items():
        colors += store(value, address)
    program = [
        # Boot record: flags, sectors to load (patched below), load address, init address.
        bytes([0x00, 0x00, MENU_ORG & 0xFF, MENU_ORG >> 8]), ("abs", "init"),
        # Switch to the menu during a vertical blank.
        bytes([0xA5, RTCLOK_LOW]),                    # LDA RTCLOK
        ("label", "vbl"),
        bytes([0xC5, RTCLOK_LOW]),                    # CMP RTCLOK
        b"\xF0", ("rel", "vbl"),                      # BEQ vbl
        *colors,
        *store(CHACTL_VALUE, CHACT),
        b"\xA9", ("lo", "dl_main"), abs_op(0x8D, SDLSTL),  # both display lists share this page
        b"\xA9", ("hi", "dl_main"), abs_op(0x8D, SDLSTL + 1),
        *store(NO_KEY, CH),                           # forget any key pressed during boot; A = NO_KEY
        ("label", "send"),                            # A = key code (NO_KEY: just draw)
        abs_op(0x8D, DAUX1),                          # STA DAUX1
        *store(MENU_DEVICE, DDEVIC),
        *store(0x01, DUNIT),
        *store(MENU_COMMAND, DCOMND),
        *store(0x40, DSTATS),                         # data from the device to the Atari
        b"\xA9", ("lo", "reply"), abs_op(0x8D, DBUFLO),
        b"\xA9", ("hi", "reply"), abs_op(0x8D, DBUFHI),
        *store(MENU_TIMEOUT_S, DTIMLO),
        *store(REPLY_SIZE & 0xFF, DBYTLO),
        *store(REPLY_SIZE >> 8, DBYTHI),
        b"\xEE", ("abs", "seq"),                      # INC seq (one number per key; SIOV's retries keep it)
        b"\xAD", ("abs", "seq"),                      # LDA seq
        abs_op(0x8D, DAUX2),                          # STA DAUX2
        abs_op(0x20, SIOV),                           # JSR SIOV
        b"\x98",                                      # TYA (SIO status)
        b"\x30", ("rel", "nohost"),                   # BMI nohost (negative status: error)
        b"\xAD", ("abs", "layout"),                   # LDA layout
        b"\x29\x01",                                  # AND #1 (two layouts)
        b"\xAA",                                      # TAX
        b"\xBD", ("abs", "dltab"),                    # LDA dltab,X (low byte of its display list)
        abs_op(0x8D, SDLSTL),                         # STA SDLSTL
        b"\xAD", ("abs", "action"),                   # LDA action
        b"\xF0", ("rel", "key"),                      # BEQ key (show)
        bytes([0xC9, ACTION_WAIT]),                   # CMP #2
        b"\xD0", ("rel", "boot"),                     # BNE boot (1, or any other: boot, as before)
        # Action 2: watch CH for WAIT_TICKS frames, then poll.
        bytes([0xA2, WAIT_TICKS]),                    # LDX #WAIT_TICKS
        ("label", "tick"),
        bytes([0xA5, RTCLOK_LOW]),                    # LDA RTCLOK
        ("label", "wait_ch"),
        abs_op(0xAC, CH),                             # LDY CH
        b"\xC0\xFF",                                  # CPY #$FF
        b"\xD0", ("rel", "waitkey"),                  # BNE waitkey (a key: send it now)
        bytes([0xC5, RTCLOK_LOW]),                    # CMP RTCLOK
        b"\xF0", ("rel", "wait_ch"),                  # BEQ wait_ch (same frame)
        b"\xCA",                                      # DEX
        b"\xD0", ("rel", "tick"),                     # BNE tick
        bytes([0xA9, POLL]),                          # LDA #POLL
        b"\xD0", ("rel", "send"),                     # BNE send (always: POLL is not 0)
        ("label", "waitkey"),
        b"\x98",                                      # TYA (the key)
        ("label", "got"),                             # A = CH, not $FF
        bytes([0x29, KEY_MASK]),                      # AND #KEY_MASK (no Shift, no Control)
        b"\xA2\xFF", abs_op(0x8E, CH),                # LDX #$FF / STX CH (consume the key)
        b"\xD0", ("rel", "send"),                     # BNE send (always: LDX set Z = 0)
        ("label", "key"),
        abs_op(0xAD, CH),                             # LDA CH
        b"\xC9\xFF",                                  # CMP #$FF
        b"\xF0", ("rel", "key"),                      # BEQ key
        b"\xD0", ("rel", "got"),                      # BNE got
        ("label", "boot"),
        abs_op(0x4C, COLDSV),                         # JMP COLDSV: boot what the host put on D1
        ("label", "nohost"),
        b"\xA2", bytes([LINE_WIDTH - 1]),             # LDX #39
        ("label", "copy"),
        b"\xBD", ("abs", "nohost_text"),              # LDA nohost_text,X
        b"\x9D", ("abs", "main_message"),             # STA main_message,X
        b"\xCA",                                      # DEX
        b"\x10", ("rel", "copy"),                     # BPL copy
        b"\xA9", ("lo", "dl_main"), abs_op(0x8D, SDLSTL),
        b"\x4C", ("abs", "key"),                      # JMP key
        ("label", "init"),
        b"\x60",                                      # RTS (DOSINI)
        ("label", "dltab"), ("lo", "dl_main"), ("lo", "dl_list"),
        ("pad", "dl_main"),                           # padding so both display lists share a page
        # Main display list, as the reference: 8 blank, title (mode 7), 8 blank, 8 sources,
        # 8 blank, subtitle (mode 6), 11 status lines, 2 footer lines; then the message line.
        ("label", "dl_main"),
        b"\x70\x47", ("abs", "main_title"),
        b"\x70\x42", ("abs", "main_sources"), bytes([0x02] * 7),
        b"\x70\x46", ("abs", "main_subtitle"),
        b"\x42", ("abs", "main_status"), bytes([0x02] * 10),
        b"\x42", ("abs", "main_footer"), b"\x02",
        b"\x42", ("abs", "main_message"),
        b"\x41", ("abs", "dl_main"),
        # List of a source: 8 blank, title (mode 7), 8 blank, 20 entries, footer, message.
        ("label", "dl_list"),
        b"\x70\x47", ("abs", "list_title"),
        b"\x70\x42", ("abs", "list_entries"), bytes([0x02] * (LIST_MAX_ENTRIES - 1)),
        b"\x42", ("abs", "list_footer"),
        b"\x42", ("abs", "list_message"),
        b"\x41", ("abs", "dl_list"),
        ("label", "dl_end"),
        ("label", "nohost_text"), screen_line(NO_HOST_TEXT),
        ("label", "seq"), b"\x00",
        # Reply buffer: action, layout and the screen the display lists show. It starts
        # with the main template, so the screen is complete even before the host answers.
        ("label", "reply"),
        ("label", "action"), bytes([ACTION_SHOW]),
        ("label", "layout"), bytes([LAYOUT_MAIN]),
        ("label", "screen"), main_template(),
        ("label", "end"),
    ]
    screen_labels = {
        "main_title": MAIN_TITLE, "main_sources": MAIN_SOURCES, "main_subtitle": MAIN_SUBTITLE,
        "main_status": MAIN_STATUS, "main_message": MAIN_MESSAGE, "main_footer": MAIN_FOOTER,
        "list_title": LIST_TITLE, "list_entries": LIST_ENTRIES, "list_footer": LIST_FOOTER,
        "list_message": LIST_MESSAGE,
    }
    sizes = {"lo": 1, "hi": 1, "abs": 2, "rel": 1}

    def addresses(items):
        """Label addresses, computed the same way as the assembler's first pass."""
        found, pc = {}, MENU_ORG
        for item in items:
            if isinstance(item, tuple) and item[0] == "label":
                found[item[1]] = pc
            else:
                pc += sizes[item[0]] if isinstance(item, tuple) else len(item)
        return found

    # The display lists start on the next page when they would cross one where they are.
    pad_at = program.index(("pad", "dl_main"))
    program[pad_at] = b""
    first = addresses(program)
    if first["dl_main"] >> 8 != (first["dl_end"] - 1) >> 8:
        program[pad_at] = bytes(0x100 - (first["dl_main"] & 0xFF))
    labels = addresses(program)
    for name, offset in screen_labels.items():
        labels[name] = labels["screen"] + offset
    code = assemble(program, MENU_ORG, extra_labels={k: labels[k] for k in screen_labels})
    sectors = -(-len(code) // BOOT_SECTOR_SIZE)
    block = bytearray(code + bytes(sectors * BOOT_SECTOR_SIZE - len(code)))
    block[1] = sectors
    if labels["dl_main"] >> 8 != (labels["dl_end"] - 1) >> 8:
        raise ValueError("the display lists must share one page (only SDLSTL's low byte changes)")
    if labels["screen"] >> 12 != (labels["screen"] + SCREEN_SIZE - 1) >> 12:
        raise ValueError("the screen crosses a 4 KB boundary")
    return bytes(block), labels


def c_array(name: str, data: bytes) -> list:
    lines = [f"constexpr unsigned char {name}[{len(data)}] = {{"]
    for i in range(0, len(data), 16):
        lines.append("    " + ", ".join(f"0x{b:02X}" for b in data[i:i + 16]) + ",")
    lines.append("};")
    return lines


def render_include(block: bytes, labels: dict) -> str:
    constants = {
        "kMenuLoadSectors": block[1], "kMenuLineWidth": LINE_WIDTH, "kMenuTitleWidth": TITLE_WIDTH,
        "kMenuScreenSize": SCREEN_SIZE, "kMenuReplySize": REPLY_SIZE, "kMenuNoKey": NO_KEY,
        "kMenuPollKey": POLL, "kMenuKeyMask": KEY_MASK,
        "kMenuActionShow": ACTION_SHOW, "kMenuActionBoot": ACTION_BOOT, "kMenuActionWait": ACTION_WAIT,
        "kMenuLayoutMain": LAYOUT_MAIN, "kMenuLayoutList": LAYOUT_LIST,
        "kMainTitle": MAIN_TITLE, "kMainSources": MAIN_SOURCES, "kMainDrives": MAIN_DRIVES,
        "kMainTape": MAIN_TAPE, "kMainFooter": MAIN_FOOTER, "kMainMessage": MAIN_MESSAGE,
        "kListTitle": LIST_TITLE, "kListEntries": LIST_ENTRIES, "kListMaxEntries": LIST_MAX_ENTRIES,
        "kListFooter": LIST_FOOTER, "kListMessage": LIST_MESSAGE,
    }
    lines = [
        "// Generated by tools/make_menu.py (Issues #38 and #40, menu-tnfs). Do not edit: regenerate with",
        "//   python tools/make_menu.py --out host/src/menu_program.inc",
        "// The tools tests fail if this file differs from the generator's output.",
        "// Original 6502 program for RetroBridge (ADR-012); protocol in ADR-017.",
        "",
    ]
    lines += [f"constexpr unsigned {name} = {value};" for name, value in constants.items()]
    lines += [f"constexpr unsigned char kMenuDevice = 0x{MENU_DEVICE:02X};",
              f"constexpr unsigned char kMenuCommand = 0x{MENU_COMMAND:02X};"]
    lines += c_array("kMenuMainTemplate", main_template())
    lines += c_array("kMenuListTemplate", list_template())
    lines += c_array("kMenuBlock", block)
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
