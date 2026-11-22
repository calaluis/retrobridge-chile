#!/usr/bin/env python3
"""Generate the 6502 menu of retrobridge_host (Issues #38 and #40, menu-tnfs) as a C++ include.

The menu is the D1 disk the Atari boots after a cold reset when retrobridge_host
runs with --images. Its first sectors hold a disk boot record, an original 6502
program (written for RetroBridge; no code from FujiNet or emulators, ADR-012),
two display lists and the screen buffer. The host serves this block as it is.

The program is a thin terminal (ADR-017, amended for Issue #40):
  1. sets the colors of the main screen and its display list;
  2. sends command 'K' to the RetroBridge menu device ($5A) with AUX1 = $FF
     (no key: draw the current screen) and AUX2 = a sequence number in bits
     7-5 that changes with every key (SIOV's own retries repeat it, so the host
     can answer a retry without acting twice) and, in bits 4-0, the row the
     local bar shows (NO_ROW without one), and reads REPLY_SIZE bytes into the
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
     screen, shows it and waits for a key;
  6. Break (menu-unidades-persistentes T5, C7 and C11): pressed while the program
     waits for a key, it is sent as BREAK_KEY with a new sequence number. Pressed
     during a command, SIOV returns BREAK_STATUS: the program puts BRKKEY back,
     notes a pending Break and calls SIOV again from `resend`, after the sequence
     number and AUX2 were set, so AUX1 and the whole AUX2 are the same and the host
     answers the retry without acting twice. Only when that command has its reply
     is the pending Break sent as a new key. A Break status never shows
     NO_HOST_TEXT.

The main screen follows the reference approved for Issue #40 (Display List Lab
mock-up): mode 7 title, 8 source lines, mode 6 subtitle, 11 status lines and a
two-row footer with the keys in inverse video between the graphic characters
$42 and $56. Addresses are the program's own, not the mock-up's.

Usage: python tools/make_menu.py --out components/rb_menu/include/rb/menu/menu_program.inc
       python tools/make_menu.py --check components/rb_menu/include/rb/menu/menu_program.inc   (exit 1 if stale)
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
# Navigation codes (menu-navegacion): above KEY_MASK, so no real key can produce them, and
# apart from POLL and NO_KEY. Arrows are Control + - = + *; the joystick is port 1.
NAV_UP, NAV_DOWN, NAV_LEFT, NAV_RIGHT, NAV_FIRE = 0x40, 0x41, 0x42, 0x43, 0x44
CONTROL = 0x80               # CH bit of the Control key
ARROW_UP, ARROW_DOWN, ARROW_LEFT, ARROW_RIGHT = 0x0E, 0x0F, 0x06, 0x07  # codes of - = + *
REPEAT_FIRST = 30            # frames a held direction waits before it repeats
REPEAT_NEXT = 9              # frames between repeats after that
STICK_CENTRED = 0x0F
MENU_TIMEOUT_S = 7           # DTIMLO: seconds the OS waits for the device
LINE_WIDTH = 40
TITLE_WIDTH = 20             # ANTIC modes 6 and 7: 20 characters per line
SCREEN_SIZE = 960            # the largest layout (900 bytes) rounded to 24 lines
REPLY_SIZE = 2 + SCREEN_SIZE  # action, layout, screen
# Local cursor (menu-navegacion N007): five bytes at the end of the screen buffer, past every
# layout, that no display list shows. Base of row 0 (screen offset, low and high), rows
# (0: no local cursor), marked row, pages: PAGE_BEFORE if there is a page before (up at
# the top row and left ask the host), PAGE_AFTER if there is one after (down at the bottom
# row and right ask it). Without that bit the move does nothing and sends nothing.
CURSOR_AT, CURSOR_SIZE = SCREEN_SIZE - 5, 5
PAGE_BEFORE, PAGE_AFTER = 0x01, 0x02
CONFIRM_ROW = 0xC0           # RETURN or fire on a local cursor: CONFIRM_ROW + marked row (Control+Shift
                             # range, so Shift+digit from an older program stays a key)
KEY_RETURN = 0x0C
ROW_MASK = 0x1F              # AUX2 bits 4-0: the row the local bar shows (bits 7-5: sequence)
NO_ROW = 0x1F                # in those bits: no local cursor on the screen shown
# Text input (menu-editar-servidor, ADR-017 amendment 6). Action 3 edits a field of the screen
# without SIO: the host puts three bytes at EDIT_AT (field offset low and high, text length).
# RETURN sends the EDIT_WIDTH characters of the field as ASCII, one TEXT_COMMAND each (AUX1
# position, AUX2 character; no data either way), then 'K' with EDIT_SAVE. Esc sends 'K' with
# Esc. Writing a position twice gives the same text, so a retry of SIOV changes nothing.
TEXT_COMMAND = 0x54          # 'T'
EDIT_SAVE = 0xE0             # 'K' code after the text: away from CONFIRM_ROW + 20, POLL and NO_KEY
# Break (menu-unidades-persistentes T5): a reserved AUX1 that no key of CH gives (keys are sent
# AND KEY_MASK), away from the navigation, confirm and edit codes, POLL and NO_KEY.
BREAK_KEY = 0xF0
BREAK_STATUS = 0x80          # SIOV status when Break aborts a command (BRKABT)
EDIT_AT, EDIT_SIZE = CURSOR_AT - 3, 3
EDIT_WIDTH = 32
KEY_ESC, KEY_DELETE = 0x1C, 0x34
SHIFT = 0x40                 # CH bit of the Shift key

ACTION_SHOW, ACTION_BOOT, ACTION_WAIT, ACTION_EDIT = 0, 1, 2, 3
# No host: the screen below the title is cleared in four strides of CLEAR_STEP bytes.
CLEAR_STEP = (SCREEN_SIZE - TITLE_WIDTH) // 4
assert 4 * CLEAR_STEP == SCREEN_SIZE - TITLE_WIDTH and CLEAR_STEP <= 0x100
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
LIST_TITLE = 0                                     # fixed: the title of the main screen (N008)
LIST_PATH = LIST_TITLE + TITLE_WIDTH               # source and folder, filled by the host (N008)
LIST_ENTRIES = LIST_PATH + LINE_WIDTH              # 20 lines
LIST_MAX_ENTRIES = 20
LIST_FOOTER = LIST_ENTRIES + LIST_MAX_ENTRIES * LINE_WIDTH  # 1 line
LIST_MESSAGE = LIST_FOOTER + LINE_WIDTH            # below the footer, as on the main screen
LIST_END = LIST_MESSAGE + LINE_WIDTH

MAIN_TITLE_TEXT = " RETROBRIDGE CHILE  "
MAIN_SUBTITLE_TEXT = " ESTADO DE UNIDADES "
NO_HOST_TEXT = " ERROR: EL HOST NO RESPONDE"
# menu-presentacion: shown in the message row of the screen in view while the reply arrives.
LOADING_TEXT = " CARGANDO..."
DLISTL = 0xD402              # ANTIC display list pointer, low byte (the OS copies SDLSTL there each frame)
HIDDEN_LIMIT = 0x4000        # the hidden reply buffer ends below this (a 16 KB Atari)

# Colors of the reference (OS shadows COLOR0..COLOR4) and CHACTL.
COLORS = {0x02C4: 0x0E, 0x02C5: 0x0E, 0x02C6: 0xA0, 0x02C7: 0x46, 0x02C8: 0xA0}
CHACT, CHACTL_VALUE = 0x02F3, 0x02

# Graphic characters around each key of the footer (screen codes).
KEY_LEFT, KEY_RIGHT = 0x42, 0x56

# OS addresses (Atari 400/800/XL OS, documented entry points).
CH = 0x02FC                  # last key code, 0xFF when none
BRKKEY = 0x0011              # 0 once Break is pressed; SIOV aborts while it stays 0
STICK0 = 0x0278              # joystick 0 (VBI shadow): 14 up, 13 down, 11 left, 7 right, 15 none
STRIG0 = 0x0284              # trigger of joystick 0 (VBI shadow): 0 pressed
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
    # menu-presentacion: every key of the main screen in two rows, N included (AC3).
    rows = (footer_key("1-8", " FUENTE  ") + footer_key("E", " SERVIDOR ") + footer_key("N", " NUEVA ATR"),
            footer_key("A", " ARRANCAR D1  ") + footer_key("U", " DESMONTAR UNIDAD"))
    return b"".join(row + bytes(LINE_WIDTH - len(row)) for row in rows)


def list_footer() -> bytes:
    # menu-navegacion: RETURN confirms the marked row; left and right change the page.
    row = (footer_key("A-T", " ELEGIR") + footer_key("RET", " OK") + footer_key("<>", " PAG")
           + footer_key("ESC", " VOLVER"))
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
    screen[LIST_TITLE:LIST_TITLE + TITLE_WIDTH] = screen_codes(MAIN_TITLE_TEXT, TITLE_WIDTH)
    screen[LIST_FOOTER:LIST_MESSAGE] = list_footer()
    return bytes(screen)


# Keyboard codes (CH without Shift and Control) of the characters a server name can have.
KEYBOARD = {
    0x3F: "a", 0x15: "b", 0x12: "c", 0x3A: "d", 0x2A: "e", 0x38: "f", 0x3D: "g", 0x39: "h", 0x0D: "i",
    0x01: "j", 0x05: "k", 0x00: "l", 0x25: "m", 0x23: "n", 0x08: "o", 0x0A: "p", 0x2F: "q", 0x28: "r",
    0x3E: "s", 0x2D: "t", 0x0B: "u", 0x10: "v", 0x2E: "w", 0x16: "x", 0x2B: "y", 0x17: "z",
    0x1F: "1", 0x1E: "2", 0x1A: "3", 0x18: "4", 0x1D: "5", 0x1B: "6", 0x33: "7", 0x35: "8", 0x30: "9",
    0x32: "0", 0x22: ".", 0x0E: "-",
}


def char_table() -> bytes:
    table = bytearray(KEY_MASK + 1)
    for code, char in KEYBOARD.items():
        table[code] = screen_codes(char, 1)[0]
    return bytes(table)


def abs_op(opcode: int, address: int) -> bytes:
    return bytes([opcode, address & 0xFF, address >> 8])


def copy_pages() -> list:
    """hidden -> reply, one page after another from the top (menu-presentacion): the rows the
    beam draws first are written first."""
    code = []
    for page in range(-(-REPLY_SIZE // 0x100)):
        count = min(0x100, REPLY_SIZE - page * 0x100)
        code += [
            b"\xA2\x00",                              # LDX #0
            ("label", f"copy{page}"),
            b"\xBD", ("abs", f"hid{page}"),           # LDA hidden+page,X
            b"\x9D", ("abs", f"rep{page}"),           # STA reply+page,X
            b"\xE8",                                  # INX
        ]
        if count < 0x100:
            code.append(bytes([0xE0, count]))         # CPX #count (the last, partial page)
        code += [b"\xD0", ("rel", f"copy{page}")]     # BNE copy (a whole page: until X wraps)
    return code


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
        b"\xA9\x01", b"\x8D", ("abs", "fire_down"),  # a trigger already held at start is not a press
        b"\xA9", bytes([STICK_CENTRED]), b"\x8D", ("abs", "stick_prev"),
        *store(NO_KEY, BRKKEY),                       # forget a Break pressed during boot
        *store(NO_KEY, CH),                           # forget any key pressed during boot; A = NO_KEY
        ("label", "send"),                            # A = key code (NO_KEY: just draw)
        abs_op(0x8D, DAUX1),                          # STA DAUX1
        bytes([0xC9, POLL]),                          # CMP #POLL (the host's own progress is on screen)
        b"\xF0", ("rel", "no_notice"),                # BEQ no_notice
        b"\x20", ("abs", "notice"),                   # JSR notice (menu-presentacion)
        ("label", "no_notice"),
        *store(MENU_DEVICE, DDEVIC),
        *store(0x01, DUNIT),
        *store(MENU_COMMAND, DCOMND),
        b"\xA9", ("lo", "hidden"), abs_op(0x8D, DBUFLO),  # out of sight: no sweep (menu-presentacion)
        b"\xA9", ("hi", "hidden"), abs_op(0x8D, DBUFHI),
        *store(REPLY_SIZE & 0xFF, DBYTLO),
        *store(REPLY_SIZE >> 8, DBYTHI),
        b"\xEE", ("abs", "seq"),                      # INC seq (one number per key; SIOV's retries keep it)
        b"\xAD", ("abs", "seq"),                      # LDA seq
        b"\x0A\x0A\x0A\x0A\x0A",                      # ASL x5 (bits 7-5: the sequence)
        b"\xAC", ("abs", "cur_count"),                # LDY cur_count
        b"\xD0", ("rel", "with_row"),                 # BNE with_row (a local cursor is shown)
        bytes([0x09, NO_ROW]),                        # ORA #NO_ROW
        b"\xD0", ("rel", "aux2"),                     # BNE aux2 (always: NO_ROW is not 0)
        ("label", "with_row"),
        b"\x0D", ("abs", "cur_row"),                  # ORA cur_row (the row the bar shows)
        ("label", "aux2"),
        abs_op(0x8D, DAUX2),                          # STA DAUX2
        # resend (T5, C11): after a Break status the same command goes again from here, so
        # DAUX1 and the whole DAUX2 (sequence and row) stay as they were. Only DSTATS, which
        # SIOV overwrites with the status, and DTIMLO are set again.
        ("label", "resend"),
        *store(0x40, DSTATS),                         # data from the device to the Atari
        *store(MENU_TIMEOUT_S, DTIMLO),
        abs_op(0x20, SIOV),                           # JSR SIOV
        b"\x98",                                      # TYA (SIO status)
        b"\x10", ("rel", "answered"),                 # BPL answered
        bytes([0xC9, BREAK_STATUS]),                  # CMP #BREAK_STATUS
        b"\xD0", ("rel", "gone"),                     # BNE gone (any other error: no host)
        b"\x20", ("abs", "brk_hold"),                 # JSR brk_hold (BRKKEY back, Break pending)
        b"\x4C", ("abs", "resend"),                   # JMP resend (the same command, unchanged)
        ("label", "gone"),
        b"\x4C", ("abs", "nohost"),                   # JMP nohost (negative status: error)
        ("label", "answered"),
        bytes([0xA5, RTCLOK_LOW]),                    # LDA RTCLOK
        ("label", "blank"),
        bytes([0xC5, RTCLOK_LOW]),                    # CMP RTCLOK
        b"\xF0", ("rel", "blank"),                    # BEQ blank (wait for the vertical blank)
        b"\x20", ("abs", "show"),                     # JSR show (display list, then the whole screen)
        b"\xAD", ("abs", "action"),                   # LDA action
        b"\xF0", ("rel", "key"),                      # BEQ key (show)
        bytes([0xC9, ACTION_WAIT]),                   # CMP #2
        b"\xF0", ("rel", "wait"),                     # BEQ wait
        bytes([0xC9, ACTION_EDIT]),                   # CMP #3
        b"\xD0", ("rel", "boot"),                     # BNE boot (1, or any other: boot, as before)
        b"\x4C", ("abs", "edit"),                     # JMP edit (menu-editar-servidor)
        # Action 2: watch CH for WAIT_TICKS frames, then poll.
        ("label", "wait"),
        bytes([0xA2, WAIT_TICKS]),                    # LDX #WAIT_TICKS
        ("label", "tick"),
        bytes([0xA5, RTCLOK_LOW]),                    # LDA RTCLOK
        b"\x8D", ("abs", "frame"),                    # STA frame
        ("label", "wait_ch"),
        abs_op(0xAC, CH),                             # LDY CH
        b"\xC0\xFF",                                  # CPY #$FF
        b"\xD0", ("rel", "waitkey"),                  # BNE waitkey (a key: send it now)
        b"\x20", ("abs", "brk"),                      # JSR brk (keeps X)
        b"\xC9\xFF",                                  # CMP #$FF
        b"\xD0", ("rel", "brk_send"),                 # BNE brk_send (Break: send it now)
        b"\x20", ("abs", "joy"),                      # JSR joy (keeps X)
        b"\xC9\xFF",                                  # CMP #$FF
        b"\xD0", ("rel", "send_nav"),                 # BNE send_nav (joystick: send it now)
        b"\xAD", ("abs", "frame"),                    # LDA frame
        bytes([0xC5, RTCLOK_LOW]),                    # CMP RTCLOK
        b"\xF0", ("rel", "wait_ch"),                  # BEQ wait_ch (same frame)
        b"\xCA",                                      # DEX
        b"\xD0", ("rel", "tick"),                     # BNE tick
        bytes([0xA9, POLL]),                          # LDA #POLL
        ("label", "brk_send"),                        # A = POLL or BREAK_KEY
        b"\x4C", ("abs", "send"),                     # JMP send (too far for a branch)
        ("label", "waitkey"),
        b"\x98",                                      # TYA (the key)
        ("label", "got"),                             # A = CH, not $FF
        b"\xA8",                                      # TAY (keep CH)
        bytes([0x29, KEY_MASK]),                      # AND #KEY_MASK (no Shift, no Control)
        bytes([0xC0, CONTROL]),                       # CPY #$80
        b"\x90", ("rel", "masked"),                   # BCC masked (no Control: a plain key)
        bytes([0xC9, ARROW_UP]), b"\xD0", ("rel", "arrow_down"),
        bytes([0xA9, NAV_UP]), b"\xD0", ("rel", "masked"),
        ("label", "arrow_down"),
        bytes([0xC9, ARROW_DOWN]), b"\xD0", ("rel", "arrow_left"),
        bytes([0xA9, NAV_DOWN]), b"\xD0", ("rel", "masked"),
        ("label", "arrow_left"),
        bytes([0xC9, ARROW_LEFT]), b"\xD0", ("rel", "arrow_right"),
        bytes([0xA9, NAV_LEFT]), b"\xD0", ("rel", "masked"),
        ("label", "arrow_right"),
        bytes([0xC9, ARROW_RIGHT]), b"\xD0", ("rel", "masked"),
        bytes([0xA9, NAV_RIGHT]),
        ("label", "masked"),
        b"\xA2\xFF", abs_op(0x8E, CH),                # LDX #$FF / STX CH (consume the key)
        b"\xD0", ("rel", "send_nav"),                 # BNE send_nav (always: LDX set Z = 0)
        ("label", "key"),
        abs_op(0xAD, CH),                             # LDA CH
        b"\xC9\xFF",                                  # CMP #$FF
        b"\xD0", ("rel", "got"),                      # BNE got (a key)
        b"\x20", ("abs", "brk"),                      # JSR brk
        b"\xC9\xFF",                                  # CMP #$FF
        b"\xD0", ("rel", "brk_send"),                 # BNE brk_send (Break, or one pending)
        b"\x20", ("abs", "joy"),                      # JSR joy
        b"\xC9\xFF",                                  # CMP #$FF
        b"\xF0", ("rel", "key"),                      # BEQ key (nothing from the joystick)
        ("label", "send_nav"),
        b"\x4C", ("abs", "local"),                    # JMP local (the local cursor, N007)
        ("label", "boot"),
        abs_op(0x4C, COLDSV),                         # JMP COLDSV: boot what the host put on D1
        ("label", "nohost"),
        b"\xA9\x00", b"\x8D", ("abs", "cur_count"),   # no local bar on the stale screen (N007)
        b"\x8D", ("abs", "layout"),                   # STA layout: the main list shows it (menu-presentacion)
        b"\x8D", ("abs", "brk_pending"),              # STA brk_pending: no command ended, no Break to send (T5)
        # The buffer may hold a list, or a reply cut or mixed (two hosts on the hub, seen in the
        # demo of menu-editar-servidor): clear everything below the title, which both layouts
        # share, so the main display list shows only the title and the message.
        b"\xA2", bytes([CLEAR_STEP - 1]),             # LDX #CLEAR_STEP-1 (A = 0)
        ("label", "clear"),
        b"\x9D", ("abs", "clear0"), b"\x9D", ("abs", "clear1"),  # STA clear0,X / STA clear1,X
        b"\x9D", ("abs", "clear2"), b"\x9D", ("abs", "clear3"),  # STA clear2,X / STA clear3,X
        b"\xCA",                                      # DEX
        b"\xE0\xFF",                                  # CPX #$FF
        b"\xD0", ("rel", "clear"),                    # BNE clear (X = 0 included)
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
        # send_nav: A = code. While a screen is shown (action 0) with a local cursor, up and
        # down move the bar here, with no SIO; RETURN and fire send CONFIRM_ROW + row. At an
        # edge, and for left and right, the host is asked only when the list has more pages.
        ("label", "local"),
        b"\xAC", ("abs", "action"),                   # LDY action
        b"\xD0", ("rel", "ln_send"),                  # BNE ln_send (the wait: the host owns it)
        b"\xAC", ("abs", "cur_count"),                # LDY cur_count
        b"\xF0", ("rel", "ln_send"),                  # BEQ ln_send (no local cursor)
        bytes([0xC9, NAV_UP]), b"\xD0", ("rel", "ln_down"),
        b"\xAC", ("abs", "cur_row"),                  # LDY cur_row
        b"\xF0", ("rel", "ln_edge"),                  # BEQ ln_edge (top row)
        b"\x20", ("abs", "flip"),                     # JSR flip (unmark)
        b"\xCE", ("abs", "cur_row"),                  # DEC cur_row
        b"\x4C", ("abs", "ln_mark"),                  # JMP ln_mark
        ("label", "ln_down"),
        bytes([0xC9, NAV_DOWN]), b"\xD0", ("rel", "ln_side"),
        b"\xAD", ("abs", "cur_row"),                  # LDA cur_row
        b"\x18", b"\x69\x01",                         # CLC / ADC #1
        b"\xCD", ("abs", "cur_count"),                # CMP cur_count
        b"\x90", ("rel", "ln_next"),                  # BCC ln_next (a row below)
        bytes([0xA9, NAV_DOWN]),                      # LDA #NAV_DOWN (bottom row)
        b"\xD0", ("rel", "ln_edge"),                  # BNE ln_edge (always)
        ("label", "ln_next"),
        b"\x20", ("abs", "flip"),                     # JSR flip (unmark)
        b"\xEE", ("abs", "cur_row"),                  # INC cur_row
        ("label", "ln_mark"),
        b"\x20", ("abs", "flip"),                     # JSR flip (mark)
        b"\x4C", ("abs", "key"),                      # JMP key (moved: nothing to send)
        ("label", "ln_side"),
        bytes([0xC9, NAV_LEFT]), b"\xF0", ("rel", "ln_edge"),
        bytes([0xC9, NAV_RIGHT]), b"\xF0", ("rel", "ln_edge"),
        bytes([0xC9, NAV_FIRE]), b"\xF0", ("rel", "ln_ok"),
        bytes([0xC9, KEY_RETURN]), b"\xD0", ("rel", "ln_send"),
        ("label", "ln_ok"),
        b"\xAD", ("abs", "cur_row"),                  # LDA cur_row
        b"\x18", bytes([0x69, CONFIRM_ROW]),          # CLC / ADC #CONFIRM_ROW
        b"\xD0", ("rel", "ln_send"),                  # BNE ln_send (always: not 0)
        # An edge, or left/right: ask the host only if there is a page that way (A = code).
        ("label", "ln_edge"),
        b"\x8D", ("abs", "ln_code"),                  # STA ln_code
        bytes([0xA2, PAGE_BEFORE]),                   # LDX #PAGE_BEFORE
        bytes([0xC9, NAV_UP]), b"\xF0", ("rel", "ln_way"),
        bytes([0xC9, NAV_LEFT]), b"\xF0", ("rel", "ln_way"),
        bytes([0xA2, PAGE_AFTER]),                    # LDX #PAGE_AFTER (down, right)
        ("label", "ln_way"),
        b"\x8A",                                      # TXA
        b"\x2D", ("abs", "cur_paged"),                # AND cur_paged
        b"\xF0", ("rel", "ln_stay"),                  # BEQ ln_stay (no page that way)
        b"\xAD", ("abs", "ln_code"),                  # LDA ln_code
        b"\xD0", ("rel", "ln_send"),                  # BNE ln_send (always: a move code)
        ("label", "ln_stay"),
        b"\x4C", ("abs", "key"),                      # JMP key (bar and row stay)
        ("label", "ln_send"),
        b"\x4C", ("abs", "send"),                     # JMP send (A = code)
        # flip: inverts the 40 bytes of the marked row (screen + base + row * 40).
        ("label", "flip"),
        b"\x18",                                      # CLC
        b"\xAD", ("abs", "cur_base"),                 # LDA cur_base
        b"\x69", ("lo", "screen"),                    # ADC #<screen
        b"\x8D", ("abs", "fl_ld"), b"\x8D", ("abs", "fl_st"),
        b"\xAD", ("abs", "cur_base_hi"),              # LDA cur_base+1
        b"\x69", ("hi", "screen"),                    # ADC #>screen
        b"\x8D", ("abs", "fl_ld_hi"), b"\x8D", ("abs", "fl_st_hi"),
        b"\xAD", ("abs", "cur_row"),                  # LDA cur_row
        b"\xAA",                                      # TAX
        b"\xF0", ("rel", "fl_go"),                    # BEQ fl_go (row 0)
        ("label", "fl_mul"),
        b"\x18", b"\xAD", ("abs", "fl_ld"),            # CLC / LDA fl_ld
        bytes([0x69, LINE_WIDTH]),                    # ADC #40
        b"\x8D", ("abs", "fl_ld"), b"\x8D", ("abs", "fl_st"),
        b"\xAD", ("abs", "fl_ld_hi"), b"\x69\x00",     # LDA fl_ld+1 / ADC #0
        b"\x8D", ("abs", "fl_ld_hi"), b"\x8D", ("abs", "fl_st_hi"),
        b"\xCA",                                      # DEX
        b"\xD0", ("rel", "fl_mul"),                   # BNE fl_mul
        ("label", "fl_go"),
        b"\xA2", bytes([LINE_WIDTH - 1]),             # LDX #39
        ("label", "fl_loop"),
        b"\xBD", ("label", "fl_ld"), ("lo", "screen"), ("label", "fl_ld_hi"), ("hi", "screen"),  # LDA row,X
        b"\x49\x80",                                  # EOR #$80
        b"\x9D", ("label", "fl_st"), ("lo", "screen"), ("label", "fl_st_hi"), ("hi", "screen"),  # STA row,X
        b"\xCA",                                      # DEX
        b"\x10", ("rel", "fl_loop"),                  # BPL fl_loop
        b"\x60",                                      # RTS
        # joy: A = the code the joystick asks to send now, or $FF. Keeps X.
        ("label", "joy"),
        abs_op(0xAD, STRIG0),                         # LDA STRIG0 (0: pressed)
        b"\xD0", ("rel", "joy_up"),                   # BNE joy_up
        b"\xAD", ("abs", "fire_down"),                # LDA fire_down
        b"\xD0", ("rel", "joy_stick"),                # BNE joy_stick (held: no repeat)
        b"\xEE", ("abs", "fire_down"),                # INC fire_down
        bytes([0xA9, NAV_FIRE]), b"\x60",             # LDA #NAV_FIRE / RTS
        ("label", "joy_up"),
        b"\xA9\x00", b"\x8D", ("abs", "fire_down"),  # released: the next press counts
        ("label", "joy_stick"),
        abs_op(0xAD, STICK0),                         # LDA STICK0
        b"\x29\x0F", b"\xA8",                         # AND #$0F / TAY
        b"\xB9", ("abs", "dirtab"),                   # LDA dirtab,Y
        b"\xC9\xFF",                                  # CMP #$FF
        b"\xF0", ("rel", "joy_reset"),                # BEQ joy_reset (centred or diagonal)
        b"\xCC", ("abs", "stick_prev"),               # CPY stick_prev
        b"\xD0", ("rel", "joy_new"),                  # BNE joy_new (a new direction: at once)
        # Held: count down once per frame seen here. Frames spent inside SIOV are not seen,
        # so a slow answer only pauses the count (no RTCLOK arithmetic that could wrap).
        bytes([0xA5, RTCLOK_LOW]),                    # LDA RTCLOK
        b"\xCD", ("abs", "joy_frame"),                # CMP joy_frame
        b"\xF0", ("rel", "joy_none"),                 # BEQ joy_none (same frame)
        b"\x8D", ("abs", "joy_frame"),                # STA joy_frame
        b"\xCE", ("abs", "countdown"),                # DEC countdown
        b"\xD0", ("rel", "joy_none"),                 # BNE joy_none (not time yet)
        bytes([0xA9, REPEAT_NEXT]),                   # LDA #REPEAT_NEXT
        b"\xD0", ("rel", "joy_send"),                 # BNE joy_send (always)
        ("label", "joy_new"),
        b"\x8C", ("abs", "stick_prev"),               # STY stick_prev
        bytes([0xA5, RTCLOK_LOW]),                    # LDA RTCLOK
        b"\x8D", ("abs", "joy_frame"),                # STA joy_frame
        bytes([0xA9, REPEAT_FIRST]),                  # LDA #REPEAT_FIRST
        ("label", "joy_send"),
        b"\x8D", ("abs", "countdown"),                # STA countdown
        b"\xB9", ("abs", "dirtab"),                   # LDA dirtab,Y
        b"\x60",                                      # RTS
        ("label", "joy_reset"),
        b"\x8C", ("abs", "stick_prev"),               # STY stick_prev
        ("label", "joy_none"),
        b"\xA9\xFF", b"\x60",                         # LDA #$FF / RTS
        # edit (action 3, menu-editar-servidor): the field is EDIT_WIDTH screen bytes at
        # screen + the offset the host wrote at EDIT_AT, with ed_len characters typed. The
        # character after the text is shown in inverse video as the cursor. No SIO until
        # RETURN or Esc; the joystick is not read.
        ("label", "edit"),
        b"\x18",                                      # CLC
        b"\xAD", ("abs", "edit_lo"),                  # LDA edit_lo
        b"\x69", ("lo", "screen"),                    # ADC #<screen
        b"\x8D", ("abs", "eg_lo"), b"\x8D", ("abs", "ep_lo"),
        b"\xAD", ("abs", "edit_hi"),                  # LDA edit_hi
        b"\x69", ("hi", "screen"),                    # ADC #>screen
        b"\x8D", ("abs", "eg_hi"), b"\x8D", ("abs", "ep_hi"),
        b"\xAD", ("abs", "edit_len"),                 # LDA edit_len
        bytes([0xC9, EDIT_WIDTH + 1]),                # CMP #33
        b"\x90", ("rel", "ed_len_ok"),                # BCC ed_len_ok
        bytes([0xA9, EDIT_WIDTH]),                    # LDA #32 (never past the field)
        ("label", "ed_len_ok"),
        b"\x8D", ("abs", "ed_len"),                   # STA ed_len
        b"\x20", ("abs", "ed_flip"),                  # JSR ed_flip (show the cursor)
        ("label", "ed_key"),
        abs_op(0xAC, CH),                             # LDY CH
        b"\xC0\xFF",                                  # CPY #$FF
        b"\xF0", ("rel", "ed_key"),                   # BEQ ed_key (no key yet)
        b"\xA2\xFF", abs_op(0x8E, CH),                # LDX #$FF / STX CH (consume the key)
        b"\x98",                                      # TYA
        b"\x30", ("rel", "ed_key"),                   # BMI ed_key (Control: not text)
        bytes([0x29, KEY_MASK]),                      # AND #KEY_MASK
        bytes([0xC9, KEY_ESC]),                       # CMP #Esc
        b"\xD0", ("rel", "ed_ret"),                   # BNE ed_ret
        b"\x4C", ("abs", "send"),                     # JMP send (A = Esc: the host cancels)
        ("label", "ed_ret"),
        bytes([0xC9, KEY_RETURN]),                    # CMP #RETURN
        b"\xD0", ("rel", "ed_del"),                   # BNE ed_del
        b"\x4C", ("abs", "ed_save"),                  # JMP ed_save
        ("label", "ed_del"),
        bytes([0xC9, KEY_DELETE]),                    # CMP #Delete (Backspace)
        b"\xD0", ("rel", "ed_char"),                  # BNE ed_char
        b"\xAD", ("abs", "ed_len"),                   # LDA ed_len
        b"\xF0", ("rel", "ed_key"),                   # BEQ ed_key (nothing to delete)
        b"\x20", ("abs", "ed_flip"),                  # JSR ed_flip (hide the cursor)
        b"\xCE", ("abs", "ed_len"),                   # DEC ed_len
        b"\xAC", ("abs", "ed_len"),                   # LDY ed_len
        b"\xA9\x00",                                  # LDA #0 (a space)
        b"\x20", ("abs", "ed_put"),                   # JSR ed_put
        b"\x20", ("abs", "ed_flip"),                  # JSR ed_flip (the cursor goes back one)
        b"\x4C", ("abs", "ed_key"),                   # JMP ed_key
        ("label", "ed_char"),
        b"\xAA",                                      # TAX
        b"\xBD", ("abs", "chartab"),                  # LDA chartab,X (screen code, 0: not allowed)
        b"\xF0", ("rel", "ed_key"),                   # BEQ ed_key
        b"\x8D", ("abs", "ed_ch"),                    # STA ed_ch
        b"\x98",                                      # TYA (the key as read)
        bytes([0x29, SHIFT]),                         # AND #Shift
        b"\xF0", ("rel", "ed_plain"),                 # BEQ ed_plain
        b"\xAD", ("abs", "ed_ch"),                    # LDA ed_ch
        b"\xC9\x61",                                  # CMP #'a' (screen code of a lowercase letter)
        b"\x90", ("rel", "ed_key"),                   # BCC ed_key (Shift + digit, '.', '-': not text)
        b"\x29\xBF",                                  # AND #$BF (the capital letter)
        b"\x8D", ("abs", "ed_ch"),                    # STA ed_ch
        ("label", "ed_plain"),
        b"\xAC", ("abs", "ed_len"),                   # LDY ed_len
        bytes([0xC0, EDIT_WIDTH]),                    # CPY #32
        b"\xB0", ("rel", "ed_key"),                   # BCS ed_key (the field is full)
        b"\xAD", ("abs", "ed_ch"),                    # LDA ed_ch
        b"\x20", ("abs", "ed_put"),                   # JSR ed_put (over the cursor)
        b"\xEE", ("abs", "ed_len"),                   # INC ed_len
        b"\x20", ("abs", "ed_flip"),                  # JSR ed_flip (the cursor moves on)
        b"\x4C", ("abs", "ed_key"),                   # JMP ed_key
        # RETURN: every position of the field goes to the host as ASCII, then 'K' EDIT_SAVE.
        ("label", "ed_save"),
        b"\x20", ("abs", "ed_flip"),                  # JSR ed_flip (hide the cursor)
        b"\xA2\x00",                                  # LDX #0
        ("label", "ed_tx"),
        b"\x8E", ("abs", "ed_x"),                     # STX ed_x (SIOV does not keep X)
        abs_op(0x8E, DAUX1),                          # STX DAUX1 (the position)
        b"\x8A", b"\xA8",                             # TXA / TAY
        b"\x20", ("abs", "ed_get"),                   # JSR ed_get
        b"\x29\x7F",                                  # AND #$7F
        b"\xC9\x40",                                  # CMP #$40
        b"\xB0", ("rel", "ed_ascii"),                 # BCS ed_ascii ($60-$7F: the same in ASCII)
        b"\x69\x20",                                  # ADC #$20 (C = 0: $00-$3F -> $20-$5F)
        ("label", "ed_ascii"),
        abs_op(0x8D, DAUX2),                          # STA DAUX2 (the character)
        *store(MENU_DEVICE, DDEVIC),
        *store(0x01, DUNIT),
        *store(TEXT_COMMAND, DCOMND),
        *store(0x00, DSTATS),                         # no data either way
        *store(MENU_TIMEOUT_S, DTIMLO),
        abs_op(0x20, SIOV),                           # JSR SIOV
        b"\x98",                                      # TYA (SIO status)
        b"\x10", ("rel", "ed_next"),                  # BPL ed_next
        bytes([0xC9, BREAK_STATUS]),                  # CMP #BREAK_STATUS
        b"\xD0", ("rel", "ed_gone"),                  # BNE ed_gone
        b"\x20", ("abs", "brk_hold"),                 # JSR brk_hold (the Break waits for the save)
        b"\xAE", ("abs", "ed_x"),                     # LDX ed_x
        b"\x4C", ("abs", "ed_tx"),                    # JMP ed_tx (the same position: same text)
        ("label", "ed_gone"),
        b"\x4C", ("abs", "nohost"),                   # JMP nohost
        ("label", "ed_next"),
        b"\xAE", ("abs", "ed_x"),                     # LDX ed_x
        b"\xE8",                                      # INX
        bytes([0xE0, EDIT_WIDTH]),                    # CPX #32
        b"\xD0", ("rel", "ed_tx"),                    # BNE ed_tx
        bytes([0xA9, EDIT_SAVE]),                     # LDA #EDIT_SAVE
        b"\x4C", ("abs", "send"),                     # JMP send
        # ed_flip: inverts the cursor cell (position ed_len) if it is inside the field.
        ("label", "ed_flip"),
        b"\xAC", ("abs", "ed_len"),                   # LDY ed_len
        bytes([0xC0, EDIT_WIDTH]),                    # CPY #32
        b"\xB0", ("rel", "ed_rts"),                   # BCS ed_rts
        b"\x20", ("abs", "ed_get"),                   # JSR ed_get
        b"\x49\x80",                                  # EOR #$80
        b"\x20", ("abs", "ed_put"),                   # JSR ed_put
        ("label", "ed_rts"),
        b"\x60",                                      # RTS
        # ed_get / ed_put: A = field[Y] / field[Y] = A (the field address is patched in).
        ("label", "ed_get"),
        b"\xB9", ("label", "eg_lo"), b"\x00", ("label", "eg_hi"), b"\x00",  # LDA field,Y
        b"\x60",                                      # RTS
        ("label", "ed_put"),
        b"\x99", ("label", "ep_lo"), b"\x00", ("label", "ep_hi"), b"\x00",  # STA field,Y
        b"\x60",                                      # RTS
        # brk_hold (T5): a command was aborted by Break. BRKKEY back to $FF (else the next SIOV
        # aborts at once) and the Break is kept for when the command has its reply.
        ("label", "brk_hold"),
        b"\xA9\x01", b"\x8D", ("abs", "brk_pending"),  # LDA #1 / STA brk_pending
        bytes([0xA9, NO_KEY]), abs_op(0x8D, BRKKEY),  # LDA #$FF / STA BRKKEY
        b"\x60",                                      # RTS
        # brk (T5): A = BREAK_KEY if Break was pressed or is pending (both are then cleared),
        # else $FF. Keeps X and Y.
        ("label", "brk"),
        b"\xAD", ("abs", "brk_pending"),              # LDA brk_pending
        b"\xD0", ("rel", "brk_yes"),                  # BNE brk_yes
        abs_op(0xAD, BRKKEY),                         # LDA BRKKEY
        b"\xD0", ("rel", "brk_none"),                 # BNE brk_none (not pressed)
        ("label", "brk_yes"),
        b"\xA9\x00", b"\x8D", ("abs", "brk_pending"),  # LDA #0 / STA brk_pending
        bytes([0xA9, NO_KEY]), abs_op(0x8D, BRKKEY),  # LDA #$FF / STA BRKKEY
        bytes([0xA9, BREAK_KEY]), b"\x60",            # LDA #BREAK_KEY / RTS
        ("label", "brk_none"),
        b"\xA9\xFF", b"\x60",                         # LDA #$FF / RTS
        # notice (menu-presentacion): LOADING_TEXT in the message row of the layout in view.
        ("label", "notice"),
        b"\xAD", ("abs", "layout"),                   # LDA layout
        b"\x29\x01",                                  # AND #1
        b"\xD0", ("rel", "notice_list"),              # BNE notice_list
        bytes([0xA2, LINE_WIDTH - 1]),                # LDX #39
        ("label", "notice_main"),
        b"\xBD", ("abs", "loading_text"),             # LDA loading_text,X
        b"\x9D", ("abs", "main_message"),             # STA main_message,X
        b"\xCA",                                      # DEX
        b"\x10", ("rel", "notice_main"),              # BPL notice_main
        b"\x60",                                      # RTS
        ("label", "notice_list"),
        bytes([0xA2, LINE_WIDTH - 1]),                # LDX #39
        ("label", "notice_row"),
        b"\xBD", ("abs", "loading_text"),             # LDA loading_text,X
        b"\x9D", ("abs", "list_message"),             # STA list_message,X
        b"\xCA",                                      # DEX
        b"\x10", ("rel", "notice_row"),               # BPL notice_row
        b"\x60",                                      # RTS
        # show (menu-presentacion), called in the vertical blank: the display list of the new
        # layout goes to the shadow and to ANTIC at once, then the reply is copied from the hidden
        # buffer top to bottom, ahead of the beam.
        ("label", "show"),
        b"\xAD", ("abs", "hid_layout"),               # LDA hidden layout
        b"\x29\x01",                                  # AND #1 (two layouts)
        b"\xAA",                                      # TAX
        b"\xBD", ("abs", "dltab"),                    # LDA dltab,X (low byte of its display list)
        abs_op(0x8D, SDLSTL),                         # STA SDLSTL
        abs_op(0x8D, DLISTL),                         # STA DLISTL
        *copy_pages(),
        b"\x60",                                      # RTS
        ("label", "show_end"),
        # Keyboard code (without Shift and Control) -> screen code of the character typed:
        # lowercase letters, digits, '.' and '-'; 0 for any other key.
        ("label", "chartab"), char_table(),
        ("label", "ed_len"), b"\x00",
        ("label", "ed_ch"), b"\x00",
        ("label", "ed_x"), b"\x00",
        # STICK0 -> code: 14 up, 13 down, 11 left, 7 right; anything else $FF.
        ("label", "dirtab"),
        bytes([0xFF] * 7 + [NAV_RIGHT] + [0xFF] * 3 + [NAV_LEFT, 0xFF, NAV_DOWN, NAV_UP, 0xFF]),
        ("label", "fire_down"), b"\x00",
        ("label", "stick_prev"), bytes([STICK_CENTRED]),
        ("label", "countdown"), b"\x00",
        ("label", "joy_frame"), b"\x00",
        ("label", "frame"), b"\x00",
        ("label", "ln_code"), b"\x00",                # the move code while its page is checked (N007)
        ("label", "brk_pending"), b"\x00",            # 1: a Break aborted a command (T5)
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
        # List of a source: 8 blank, title (mode 7), path line where the 8 blank lines were
        # (N008), 20 entries, footer, message.
        ("label", "dl_list"),
        b"\x70\x47", ("abs", "list_title"),
        b"\x42", ("abs", "list_path"),
        b"\x42", ("abs", "list_entries"), bytes([0x02] * (LIST_MAX_ENTRIES - 1)),
        b"\x42", ("abs", "list_footer"),
        b"\x42", ("abs", "list_message"),
        b"\x41", ("abs", "dl_list"),
        ("label", "dl_end"),
        ("label", "nohost_text"), screen_line(NO_HOST_TEXT),
        ("label", "loading_text"), screen_line(LOADING_TEXT),
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
        "list_title": LIST_TITLE, "list_path": LIST_PATH, "list_entries": LIST_ENTRIES, "list_footer": LIST_FOOTER,
        "list_message": LIST_MESSAGE,
        "cur_base": CURSOR_AT, "cur_base_hi": CURSOR_AT + 1, "cur_count": CURSOR_AT + 2,
        "cur_row": CURSOR_AT + 3, "cur_paged": CURSOR_AT + 4,
        "edit_lo": EDIT_AT, "edit_hi": EDIT_AT + 1, "edit_len": EDIT_AT + 2,
        **{f"clear{n}": TITLE_WIDTH + n * CLEAR_STEP for n in range(4)},
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
    # menu-presentacion: the hidden reply buffer is RAM on the first page after the block the
    # boot loads; it is never loaded from the disk, so the block does not grow.
    loaded = -(-(labels["end"] - MENU_ORG) // BOOT_SECTOR_SIZE) * BOOT_SECTOR_SIZE
    hidden = (MENU_ORG + loaded + 0xFF) & ~0xFF
    if hidden + REPLY_SIZE > HIDDEN_LIMIT:
        raise ValueError("the hidden reply buffer does not fit below HIDDEN_LIMIT")
    hidden_labels = {"hidden": hidden, "hid_layout": hidden + 1}
    for page in range(-(-REPLY_SIZE // 0x100)):
        hidden_labels[f"hid{page}"] = hidden + page * 0x100
        hidden_labels[f"rep{page}"] = labels["reply"] + page * 0x100
    labels.update(hidden_labels)
    code = assemble(program, MENU_ORG,
                    extra_labels={**{k: labels[k] for k in screen_labels}, **hidden_labels})
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
        "kMenuNavUp": NAV_UP, "kMenuNavDown": NAV_DOWN, "kMenuNavLeft": NAV_LEFT,
        "kMenuNavRight": NAV_RIGHT, "kMenuNavFire": NAV_FIRE,
        "kMenuActionShow": ACTION_SHOW, "kMenuActionBoot": ACTION_BOOT, "kMenuActionWait": ACTION_WAIT,
        "kMenuLayoutMain": LAYOUT_MAIN, "kMenuLayoutList": LAYOUT_LIST,
        "kMainTitle": MAIN_TITLE, "kMainSources": MAIN_SOURCES, "kMainDrives": MAIN_DRIVES,
        "kMainTape": MAIN_TAPE, "kMainFooter": MAIN_FOOTER, "kMainMessage": MAIN_MESSAGE,
        "kListTitle": LIST_TITLE, "kListPath": LIST_PATH, "kListEntries": LIST_ENTRIES, "kListMaxEntries": LIST_MAX_ENTRIES,
        "kListFooter": LIST_FOOTER, "kListMessage": LIST_MESSAGE,
        "kMenuCursor": CURSOR_AT, "kMenuConfirmRow": CONFIRM_ROW,
        "kMenuPageBefore": PAGE_BEFORE, "kMenuPageAfter": PAGE_AFTER,
        "kMenuRowMask": ROW_MASK, "kMenuNoRow": NO_ROW,
        "kMenuActionEdit": ACTION_EDIT, "kMenuEditSave": EDIT_SAVE, "kMenuEdit": EDIT_AT,
        "kMenuEditWidth": EDIT_WIDTH, "kMenuBreakKey": BREAK_KEY,
    }
    lines = [
        "// Generated by tools/make_menu.py (Issues #38 and #40, menu-tnfs). Do not edit: regenerate with",
        "//   python tools/make_menu.py --out components/rb_menu/include/rb/menu/menu_program.inc",
        "// The tools tests fail if this file differs from the generator's output.",
        "// Original 6502 program for RetroBridge (ADR-012); protocol in ADR-017.",
        "",
    ]
    lines += [f"constexpr unsigned {name} = {value};" for name, value in constants.items()]
    lines += [f"constexpr unsigned char kMenuDevice = 0x{MENU_DEVICE:02X};",
              f"constexpr unsigned char kMenuCommand = 0x{MENU_COMMAND:02X};",
              f"constexpr unsigned char kMenuTextCommand = 0x{TEXT_COMMAND:02X};"]
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
