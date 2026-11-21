"""Tests for tools/make_menu.py: the 6502 menu of retrobridge_host (Issues #38 and #40).

The program is run by a small interpreter of the instructions it uses. SIOV and
COLDSV are trapped: SIOV calls a Python function that plays the host, COLDSV
ends the run. RTCLOK advances once every STEPS_PER_FRAME instructions, as the
vertical blank does once per frame, so a wait in frames can be measured. Keys are fed through CH as the OS keyboard handler does. The
display lists are checked byte for byte against the reference of Issue #40
(the Display List Lab mock-up), with the program's own addresses.
"""

import subprocess
import sys
import unittest
from pathlib import Path

TOOLS = Path(__file__).resolve().parent.parent
sys.path.insert(0, str(TOOLS))

import make_menu  # noqa: E402

REPO = TOOLS.parent
ORG = make_menu.MENU_ORG
STEPS_PER_FRAME = 200  # instructions per frame; a real frame runs thousands, the wait loop needs more than 20 (menu-navegacion)
REAL_STEPS_PER_FRAME = 6000  # ~29,800 cycles per NTSC frame at ~5 cycles per instruction (menu-presentacion)


class Halt(Exception):
    pass


class Menu6502:
    """Interprets the menu program. Any opcode it does not know fails the test."""

    def __init__(self, block: bytes, keys, siov, labels=None):
        self.mem = bytearray(0x10000)
        self.mem[ORG:ORG + len(block)] = block
        # Another program loaded at the same address (the sector probe) passes its own labels.
        self.labels = make_menu.menu_program()[1] if labels is None else labels
        # Leftovers in the DCB, so a store the program forgets is seen by the tests.
        self.mem[make_menu.DDEVIC:make_menu.DAUX2 + 1] = bytes([0xAA]) * (make_menu.DAUX2 + 1 - make_menu.DDEVIC)
        self.mem[make_menu.CH] = 0x3F  # a key pressed during boot, to be forgotten
        self.keys = list(keys)
        self.siov = siov
        self.siov_calls = []
        self.a = self.x = self.y = 0
        self.z = self.n = self.c = False
        self.idle_key_reads = 0
        self.pc = 0
        # A key that arrives during the wait of action 2: (n, code) puts `code` in CH on the
        # n-th read of CH inside the wait loop.
        self.wait_key = None
        self.wait_ch_reads = 0
        self.ticks_waited = 0  # frames counted by the wait loop (visits of its "tick" label)
        # Joystick of port 1 (menu-navegacion): joy(frame) -> (STICK0, STRIG0); None = centred, no fire.
        # While frame < joy_until the idle key wait does not stop the run.
        self.joy = None
        self.joy_until = 0
        self.frames = 0
        self.sp = 0xFF
        # menu-presentacion: label name -> frames at each visit of that label.
        self.watch = {}
        self.visits = []

    def word(self, at):
        return self.mem[at] | (self.mem[at + 1] << 8)

    def read(self, at):
        if at in (make_menu.STICK0, make_menu.STRIG0):
            stick, trig = self.joy(self.frames) if self.joy is not None else (0x0F, 1)
            return stick if at == make_menu.STICK0 else trig
        if at == make_menu.CH and self.pc == self.labels.get("wait_ch"):
            self.wait_ch_reads += 1
            if self.wait_key is not None and self.wait_ch_reads == self.wait_key[0]:
                self.mem[at] = self.wait_key[1]
            return self.mem[at]
        if at == make_menu.CH and self.mem[at] == 0xFF:
            if self.keys:
                self.mem[at] = self.keys.pop(0)
            elif self.frames < self.joy_until:
                pass
            else:
                self.idle_key_reads += 1
                if self.idle_key_reads > 3:
                    raise Halt("waiting for a key")
        return self.mem[at]

    def flags(self, value):
        self.z = value == 0
        self.n = value >= 0x80

    def compare(self, register, value):
        self.c = register >= value
        self.flags((register - value) & 0xFF)

    def run(self, pc=None, max_steps=200000):
        pc = ORG + 6 if pc is None else pc
        show, show_end = self.labels.get("show", -1), self.labels.get("show_end", -1)
        clock = 0.0
        for step in range(max_steps):
            # The copy of menu-presentacion runs thousands of instructions; at STEPS_PER_FRAME
            # it would last ~19 frames instead of under one. Inside it, an instruction costs
            # what it costs on the Atari: about 1/REAL_STEPS_PER_FRAME of a frame.
            clock += STEPS_PER_FRAME / REAL_STEPS_PER_FRAME if show <= pc < show_end else 1
            if clock >= STEPS_PER_FRAME:
                clock -= STEPS_PER_FRAME
                self.mem[make_menu.RTCLOK_LOW] = (self.mem[make_menu.RTCLOK_LOW] + 1) & 0xFF
                self.frames += 1
            self.pc = pc
            if pc == self.labels.get("tick"):
                self.ticks_waited += 1
            for name in self.watch:
                if pc == self.labels[name]:
                    self.visits.append((name, self.frames))
            op = self.mem[pc]
            imm, zp, ab = self.mem[pc + 1], self.mem[pc + 1], self.word(pc + 1)
            if op == 0xA9:
                self.a = imm; self.flags(self.a); pc += 2
            elif op == 0xA2:
                self.x = imm; self.flags(self.x); pc += 2
            elif op == 0xA5:
                self.a = self.read(zp); self.flags(self.a); pc += 2
            elif op == 0xAD:
                self.a = self.read(ab); self.flags(self.a); pc += 3
            elif op == 0xAC:
                self.y = self.read(ab); self.flags(self.y); pc += 3
            elif op == 0xAE:
                self.x = self.read(ab); self.flags(self.x); pc += 3
            elif op == 0xC0:
                self.compare(self.y, imm); pc += 2
            elif op == 0xBD:
                self.a = self.read(ab + self.x); self.flags(self.a); pc += 3
            elif op == 0x8D:
                self.mem[ab] = self.a; pc += 3
            elif op == 0xEE:
                self.mem[ab] = (self.mem[ab] + 1) & 0xFF; self.flags(self.mem[ab]); pc += 3
            elif op == 0x9D:
                self.mem[ab + self.x] = self.a; pc += 3
            elif op == 0x8E:
                self.mem[ab] = self.x; pc += 3
            elif op == 0xC5:
                self.compare(self.a, self.read(zp)); pc += 2
            elif op == 0xC9:
                self.compare(self.a, imm); pc += 2
            elif op == 0xE0:
                self.compare(self.x, imm); pc += 2
            elif op == 0x69:
                total = self.a + imm + (1 if self.c else 0)
                self.c = total > 0xFF
                self.a = total & 0xFF; self.flags(self.a); pc += 2
            elif op == 0x4A:
                self.c = bool(self.a & 1)
                self.a >>= 1; self.flags(self.a); pc += 1
            elif op == 0x0A:
                self.c = bool(self.a & 0x80)
                self.a = (self.a << 1) & 0xFF; self.flags(self.a); pc += 1
            elif op == 0x8A:
                self.a = self.x; self.flags(self.a); pc += 1
            elif op == 0xA8:
                self.y = self.a; self.flags(self.y); pc += 1
            elif op == 0xE8:
                self.x = (self.x + 1) & 0xFF; self.flags(self.x); pc += 1
            elif op == 0x99:
                self.mem[ab + self.y] = self.a; pc += 3
            elif op == 0x8C:
                self.mem[ab] = self.y; pc += 3
            elif op == 0x29:
                self.a &= imm; self.flags(self.a); pc += 2
            elif op == 0x2D:
                self.a &= self.read(ab); self.flags(self.a); pc += 3
            elif op == 0x09:
                self.a |= imm; self.flags(self.a); pc += 2
            elif op == 0x0D:
                self.a |= self.read(ab); self.flags(self.a); pc += 3
            elif op == 0x49:
                self.a ^= imm; self.flags(self.a); pc += 2
            elif op == 0xAA:
                self.x = self.a; self.flags(self.x); pc += 1
            elif op == 0xCA:
                self.x = (self.x - 1) & 0xFF; self.flags(self.x); pc += 1
            elif op == 0x98:
                self.a = self.y; self.flags(self.a); pc += 1
            elif op in (0xF0, 0xD0, 0x30, 0x10, 0x90, 0xB0):
                offset = imm - 256 if imm > 127 else imm
                taken = {0xF0: self.z, 0xD0: not self.z, 0x30: self.n, 0x10: not self.n,
                         0x90: not self.c, 0xB0: self.c}[op]
                pc += 2 + (offset if taken else 0)
            elif op == 0x4C:
                if ab == make_menu.COLDSV:
                    return "coldstart"
                if ab == pc:
                    return "halt"  # JMP to itself: the program stopped there
                pc = ab
            elif op == 0x20 and ab == make_menu.SIOV:
                self.siov_call(); pc += 3
            elif op == 0x20:
                ret = pc + 2
                self.mem[0x100 + self.sp] = ret >> 8; self.sp = (self.sp - 1) & 0xFF
                self.mem[0x100 + self.sp] = ret & 0xFF; self.sp = (self.sp - 1) & 0xFF
                pc = ab
            elif op == 0x60:
                self.sp = (self.sp + 1) & 0xFF; low = self.mem[0x100 + self.sp]
                self.sp = (self.sp + 1) & 0xFF; high = self.mem[0x100 + self.sp]
                pc = ((high << 8) | low) + 1
            elif op == 0xCE:
                self.mem[ab] = (self.mem[ab] - 1) & 0xFF; self.flags(self.mem[ab]); pc += 3
            elif op == 0x18:
                self.c = False; pc += 1
            elif op == 0xCC:
                self.compare(self.y, self.read(ab)); pc += 3
            elif op == 0xCD:
                self.compare(self.a, self.read(ab)); pc += 3
            elif op == 0xB9:
                self.a = self.read(ab + self.y); self.flags(self.a); pc += 3
            else:
                raise AssertionError(f"opcode {op:02x} at {pc:04x} not expected in the menu program")
        raise AssertionError("menu program did not finish")

    def siov_call(self):
        m = self.mem
        dcb = {name: m[addr] for name, addr in (
            ("device", make_menu.DDEVIC), ("unit", make_menu.DUNIT), ("command", make_menu.DCOMND),
            ("dstats", make_menu.DSTATS), ("timeout", make_menu.DTIMLO), ("aux1", make_menu.DAUX1),
            ("aux2", make_menu.DAUX2))}
        dcb["buffer"] = self.word(make_menu.DBUFLO)
        dcb["rtclok"] = m[make_menu.RTCLOK_LOW]
        dcb["ticks_waited"] = self.ticks_waited
        dcb["frames"] = self.frames
        dcb["length"] = self.word(make_menu.DBYTLO)
        # What the Atari shows while the reply arrives (the sector probe has no menu screen).
        dcb["visible"] = self.screen() if "screen" in self.labels else None
        self.siov_calls.append(dcb)
        reply = self.siov(dcb)
        if reply is None:
            self.y = 0x8A      # SIO timeout
        elif isinstance(reply, tuple):
            # ("cut", data): the bytes arrived but the frame failed (checksum), as a reply cut
            # or mixed by two hosts on the hub: SIOV wrote them and returns an error.
            m[dcb["buffer"]:dcb["buffer"] + len(reply[1])] = reply[1]
            self.y = 0x8F
        else:
            m[dcb["buffer"]:dcb["buffer"] + len(reply)] = reply
            self.y = 0x01

    def screen(self):
        start = self.labels["screen"]
        return bytes(self.mem[start:start + make_menu.SCREEN_SIZE])

    def run_until_idle(self, pc=None):
        """Runs from the entry point (or pc) until a cold start or an idle key wait."""
        self.idle_key_reads = 0
        try:
            return self.run(pc)
        except Halt:
            return "waiting"


def host_reply(action: int, layout: int, marker: int = 0x21) -> bytes:
    screen = bytearray([marker]) * make_menu.SCREEN_SIZE
    screen[make_menu.CURSOR_AT:make_menu.CURSOR_AT + make_menu.CURSOR_SIZE] = bytes(make_menu.CURSOR_SIZE)  # no cursor
    return bytes([action, layout]) + bytes(screen)


def cursor_reply(count: int, row: int, paged: int, base: int = None, action: int = 0) -> bytes:
    """A list screen with the local cursor (menu-navegacion N007): `count` rows from `base`,
    `row` marked in inverse video as the host draws it."""
    base = make_menu.LIST_ENTRIES if base is None else base
    screen = bytearray(make_menu.SCREEN_SIZE)
    for n in range(count):
        screen[base + n * 40:base + n * 40 + 40] = bytes([0x21 + n]) * 40
    screen[base + row * 40:base + row * 40 + 40] = bytes([(0x21 + row) | 0x80]) * 40
    at = make_menu.CURSOR_AT
    screen[at:at + make_menu.CURSOR_SIZE] = bytes([base & 0xFF, base >> 8, count, row, paged])
    return bytes([action, 1]) + bytes(screen)


def marked_rows(screen: bytes, count: int, base: int = None):
    """The rows whose 40 bytes are all in inverse video; a half-marked row fails."""
    base = make_menu.LIST_ENTRIES if base is None else base
    rows = []
    for n in range(count):
        line = screen[base + n * 40:base + n * 40 + 40]
        inverse = sum(1 for b in line if b & 0x80)
        assert inverse in (0, 40), f"row {n} half in inverse video"
        if inverse == 40:
            assert all((b & 0x7F) == 0x21 + n for b in line), f"row {n} text changed"
            rows.append(n)
    return rows


KEY_1, KEY_A, KEY_ESC = 0x1F, 0x3F, 0x1C
KEY_RETURN, KEY_DELETE = 0x0C, 0x34
FIELD = make_menu.MAIN_SOURCES + 2 * 40 + 3  # source 3, after "3  "


def edit_reply(text: str, length: int = None, field: int = FIELD) -> bytes:
    """Action 3 (menu-editar-servidor): the main screen with `text` in the field and the
    edit block the host writes; no local cursor."""
    screen = bytearray(make_menu.SCREEN_SIZE)
    screen[field:field + len(text)] = make_menu.screen_codes(text, len(text))
    at = make_menu.EDIT_AT
    screen[at:at + 3] = bytes([field & 0xFF, field >> 8, len(text) if length is None else length])
    return bytes([make_menu.ACTION_EDIT, make_menu.LAYOUT_MAIN]) + bytes(screen)


def edit_host(first: bytes, after=None, fail_text_at=None):
    """A host that answers the first 'K' with `first`, every 'T' with no data (None from
    position `fail_text_at` on: no answer), and the next 'K's with `after`."""
    after = host_reply(0, 0) if after is None else after
    state = {"k": 0}

    def siov(dcb):
        if dcb["command"] == make_menu.TEXT_COMMAND:
            return None if fail_text_at is not None and dcb["aux1"] >= fail_text_at else b""
        state["k"] += 1
        return first if state["k"] == 1 else after
    return siov


def field_text(screen: bytes, width: int = make_menu.EDIT_WIDTH, field: int = FIELD) -> bytes:
    return screen[field:field + width]


class MenuProgramTest(unittest.TestCase):

    @classmethod
    def setUpClass(cls):
        cls.block, cls.labels = make_menu.menu_program()

    def at(self, label, length):
        start = self.labels[label] - ORG
        return self.block[start:start + length]

    def test_boot_record_loads_the_whole_block_at_0700(self):
        self.assertEqual(self.block[0], 0x00)
        self.assertEqual(self.block[1] * 128, len(self.block))
        self.assertEqual(self.block[2] | self.block[3] << 8, 0x0700)
        self.assertEqual(self.block[4] | self.block[5] << 8, self.labels["init"])
        self.assertEqual(self.block[self.labels["init"] - ORG], 0x60)

    def lms(self, label):
        address = self.labels[label]
        return bytes([address & 0xFF, address >> 8])

    def test_main_display_list_follows_the_reference(self):
        # Reference: blank 8 / mode 7 lms / blank 8 / mode 2 lms x8 / blank 8 / mode 6 lms /
        # mode 2 lms x11 / mode 2 lms x2 / then, asked in review, the message line below the footer / jvb.
        expected = (b"\x70\x47" + self.lms("main_title")
                    + b"\x70\x42" + self.lms("main_sources") + b"\x02" * 7
                    + b"\x70\x46" + self.lms("main_subtitle")
                    + b"\x42" + self.lms("main_status") + b"\x02" * 10
                    + b"\x42" + self.lms("main_footer") + b"\x02"
                    + b"\x42" + self.lms("main_message")
                    + b"\x41" + self.lms("dl_main"))
        self.assertEqual(self.at("dl_main", len(expected)), expected)

    def test_list_display_list_has_title_twenty_entries_footer_and_message(self):
        # N008: the path line takes the 8 blank lines under the title; same height as before.
        expected = (b"\x70\x47" + self.lms("list_title")
                    + b"\x42" + self.lms("list_path")
                    + b"\x42" + self.lms("list_entries") + b"\x02" * 19
                    + b"\x42" + self.lms("list_footer")
                    + b"\x42" + self.lms("list_message")
                    + b"\x41" + self.lms("dl_list"))
        self.assertEqual(self.at("dl_list", len(expected)), expected)
        # 20 entries are contiguous: the LMS line plus 19 more; the message goes under the footer.
        self.assertEqual(self.labels["list_footer"] - self.labels["list_entries"], 20 * 40)
        self.assertEqual(self.labels["list_message"] - self.labels["list_footer"], 40)
        self.assertEqual(self.labels["list_path"] - self.labels["list_title"], 20)
        self.assertEqual(self.labels["list_entries"] - self.labels["list_path"], 40)

    def test_screen_layout_matches_the_reference_offsets(self):
        self.assertEqual(self.labels["main_sources"] - self.labels["main_title"], 20)
        self.assertEqual(self.labels["main_subtitle"] - self.labels["main_sources"], 8 * 40)
        self.assertEqual(self.labels["main_status"] - self.labels["main_subtitle"], 20)
        self.assertEqual(self.labels["main_footer"] - self.labels["main_status"], 11 * 40)
        # The message goes under the two footer rows (asked in the review of Issue #40).
        self.assertEqual(self.labels["main_message"] - self.labels["main_footer"], 2 * 40)
        self.assertLessEqual(make_menu.MAIN_END, make_menu.SCREEN_SIZE)

    def test_display_lists_share_a_page_and_the_screen_stays_in_4k(self):
        self.assertEqual(self.labels["dl_main"] >> 8, (self.labels["dl_end"] - 1) >> 8)
        screen = self.labels["screen"]
        self.assertEqual(screen >> 12, (screen + make_menu.SCREEN_SIZE - 1) >> 12)

    def test_main_template_has_title_subtitle_and_the_reference_footer(self):
        screen = self.at("screen", make_menu.SCREEN_SIZE)
        self.assertEqual(screen, make_menu.main_template())
        self.assertEqual(screen[0:20], make_menu.screen_codes(" RETROBRIDGE CHILE  ", 20))
        sub = make_menu.MAIN_SUBTITLE
        self.assertEqual(screen[sub:sub + 20], make_menu.screen_codes(" ESTADO DE UNIDADES ", 20))
        inv = lambda text: bytes(b | 0x80 for b in make_menu.screen_codes(text, len(text)))  # noqa: E731
        txt = lambda text: make_menu.screen_codes(text, len(text))  # noqa: E731
        # menu-presentacion (AC3): every key of the main screen, N included.
        row1 = (b"\x42" + inv("1-8") + b"\x56" + txt(" FUENTE  ") + b"\x42" + inv("E") + b"\x56"
                + txt(" SERVIDOR ") + b"\x42" + inv("N") + b"\x56" + txt(" NUEVA ATR"))
        row2 = (b"\x42" + inv("A") + b"\x56" + txt(" ARRANCAR D1  ") + b"\x42" + inv("U") + b"\x56"
                + txt(" DESMONTAR UNIDAD") + b"\x00" * 3)
        self.assertEqual((len(row1), len(row2)), (40, 40))
        foot = make_menu.MAIN_FOOTER
        self.assertEqual(screen[foot:foot + 80], row1 + row2)

    def test_list_template_has_the_fixed_title_of_the_main_screen(self):
        template = make_menu.list_template()
        title = template[make_menu.LIST_TITLE:make_menu.LIST_TITLE + 20]
        self.assertEqual(title, make_menu.screen_codes(make_menu.MAIN_TITLE_TEXT, 20))
        self.assertEqual(template[make_menu.LIST_PATH:make_menu.LIST_PATH + 40], bytes(40))  # the host fills it

    def test_list_template_footer_is_forty_columns(self):
        footer = make_menu.list_template()[make_menu.LIST_FOOTER:make_menu.LIST_MESSAGE]
        self.assertEqual(len(footer), 40)
        self.assertEqual(footer[0], 0x42)

    def test_start_sets_colors_and_asks_the_host_for_the_screen(self):
        cpu = Menu6502(self.block, [], lambda dcb: host_reply(0, 0))
        self.assertEqual(cpu.run_until_idle(), "waiting")
        for address, value in make_menu.COLORS.items():
            self.assertEqual(cpu.mem[address], value, hex(address))
        self.assertEqual(cpu.mem[make_menu.CHACT], 0x02)
        self.assertEqual(len(cpu.siov_calls), 1)
        call = cpu.siov_calls[0]
        self.assertEqual((call["device"], call["unit"], call["command"]), (0x5A, 1, 0x4B))
        self.assertEqual((call["dstats"], call["aux1"], call["aux2"]), (0x40, 0xFF, 1 << 5 | make_menu.NO_ROW))
        self.assertEqual((call["buffer"], call["length"]), (self.labels["hidden"], 962))  # menu-presentacion
        self.assertEqual(call["timeout"], make_menu.MENU_TIMEOUT_S)
        self.assertEqual(cpu.word(make_menu.SDLSTL), self.labels["dl_main"])
        self.assertEqual(cpu.screen(), host_reply(0, 0)[2:])

    def test_each_key_is_sent_as_its_keyboard_code(self):
        cpu = Menu6502(self.block, [KEY_1, KEY_ESC, KEY_A | 0x40], lambda dcb: host_reply(0, 0))
        self.assertEqual(cpu.run_until_idle(), "waiting")
        # Shift+A goes out as A: the program sends CH AND $3F (menu-tnfs, M41-3).
        self.assertEqual([c["aux1"] for c in cpu.siov_calls], [0xFF, KEY_1, KEY_ESC, KEY_A])
        # One sequence number per key in bits 7-5, so the host can tell a new key from SIOV's
        # retry; bits 4-0 hold NO_ROW, as there is no local cursor.
        self.assertEqual([c["aux2"] for c in cpu.siov_calls], [n << 5 | make_menu.NO_ROW for n in (1, 2, 3, 4)])

    def test_shift_and_control_are_removed_before_sending(self):
        # A, Shift+A, Control+A, Control+Shift+S, and L (code $00, so AND gives zero).
        keys = [KEY_A, KEY_A | 0x40, KEY_A | 0x80, 0xFE, 0x00]
        cpu = Menu6502(self.block, keys, lambda dcb: host_reply(0, 0))
        self.assertEqual(cpu.run_until_idle(), "waiting")
        self.assertEqual([c["aux1"] for c in cpu.siov_calls], [0xFF, 0x3F, 0x3F, 0x3F, 0x3E, 0x00])
        self.assertNotIn(0xFE, [c["aux1"] for c in cpu.siov_calls[1:]])
        self.assertEqual(cpu.mem[make_menu.CH], 0xFF)  # each key was consumed

    def test_wait_action_polls_with_fe_and_a_new_sequence_number(self):
        replies = [host_reply(0, 1), host_reply(2, 1), host_reply(2, 1), host_reply(0, 1)]
        cpu = Menu6502(self.block, [KEY_1], lambda dcb: replies.pop(0))
        self.assertEqual(cpu.run_until_idle(), "waiting")
        calls = cpu.siov_calls
        self.assertEqual([c["aux1"] for c in calls], [0xFF, KEY_1, 0xFE, 0xFE])
        self.assertEqual([c["aux2"] >> 5 for c in calls], [1, 2, 3, 4])
        # The wait loop counts 15 frames between the answer "wait" and the poll, and RTCLOK
        # really moved that much (one more for the instructions around the loop).
        for before, after in ((calls[1], calls[2]), (calls[2], calls[3])):
            self.assertEqual(after["ticks_waited"] - before["ticks_waited"], 15)
            ticks = (after["rtclok"] - before["rtclok"]) & 0xFF
            self.assertTrue(15 <= ticks <= 17, ticks)
        self.assertEqual(cpu.word(make_menu.SDLSTL), self.labels["dl_list"])
        self.assertEqual(replies, [])

    def test_a_key_during_the_wait_is_sent_normalized_instead_of_the_poll(self):
        replies = [host_reply(0, 1), host_reply(2, 1), host_reply(0, 1)]
        cpu = Menu6502(self.block, [], lambda dcb: replies.pop(0))
        cpu.wait_key = (5, 0xFE)  # Control+Shift+S during the wait: S, never the poll
        cpu.keys = [KEY_1]
        self.assertEqual(cpu.run_until_idle(), "waiting")
        calls = cpu.siov_calls
        self.assertEqual([c["aux1"] for c in calls], [0xFF, KEY_1, 0x3E])
        self.assertEqual([c["aux2"] >> 5 for c in calls], [1, 2, 3])
        ticks = (calls[2]["rtclok"] - calls[1]["rtclok"]) & 0xFF
        self.assertLess(ticks, 15)  # sent at once, without waiting the rest
        self.assertEqual(cpu.mem[make_menu.CH], 0xFF)

    def test_boot_after_a_wait_still_cold_starts(self):
        replies = [host_reply(0, 1), host_reply(2, 1), host_reply(1, 0)]
        cpu = Menu6502(self.block, [KEY_A], lambda dcb: replies.pop(0))
        self.assertEqual(cpu.run_until_idle(), "coldstart")
        self.assertEqual([c["aux1"] for c in cpu.siov_calls], [0xFF, KEY_A, 0xFE])

    def test_an_unknown_action_still_cold_starts(self):
        # Only action 2 waits and action 3 edits; any other non-zero action boots, as before.
        for action in (4, 0xFF):
            replies = [host_reply(0, 0), host_reply(action, 0)]
            cpu = Menu6502(self.block, [KEY_A], lambda dcb: replies.pop(0))
            self.assertEqual(cpu.run_until_idle(), "coldstart", action)

    def test_reply_size_does_not_change(self):
        self.assertEqual(make_menu.REPLY_SIZE, 962)

    def test_layout_byte_selects_the_display_list(self):
        replies = [host_reply(0, 0), host_reply(0, 1), host_reply(0, 0)]
        cpu = Menu6502(self.block, [KEY_1], lambda dcb: replies.pop(0))
        cpu.run_until_idle()
        self.assertEqual(cpu.word(make_menu.SDLSTL), self.labels["dl_list"])
        cpu.keys = [KEY_ESC]
        cpu.run_until_idle(self.labels["key"])
        self.assertEqual(cpu.word(make_menu.SDLSTL), self.labels["dl_main"])

    def test_boot_action_cold_starts(self):
        replies = [host_reply(0, 0), host_reply(1, 0)]
        cpu = Menu6502(self.block, [KEY_A], lambda dcb: replies.pop(0))
        self.assertEqual(cpu.run_until_idle(), "coldstart")
        self.assertEqual(len(cpu.siov_calls), 2)

    def test_no_answer_shows_the_no_host_line_on_the_main_screen(self):
        replies = [host_reply(0, 1), None]
        cpu = Menu6502(self.block, [KEY_1], lambda dcb: replies.pop(0))
        self.assertEqual(cpu.run_until_idle(), "waiting")
        self.assertEqual(cpu.word(make_menu.SDLSTL), self.labels["dl_main"])
        message = make_menu.MAIN_MESSAGE
        screen = cpu.screen()
        self.assertEqual(screen[message:message + 40], make_menu.screen_line(make_menu.NO_HOST_TEXT))
        # The list that was shown is gone: only the title (shared by both layouts) stays, so the
        # main display list never shows list rows in its title modes (demo of menu-editar-servidor).
        self.assertEqual(screen[:make_menu.TITLE_WIDTH], bytes([0x21]) * make_menu.TITLE_WIDTH)
        rest = screen[make_menu.TITLE_WIDTH:message] + screen[message + 40:]
        self.assertEqual(rest, bytes(len(rest)))

    def test_no_answer_turns_the_local_cursor_off(self):
        # A list with the bar, then no answer: the arrows go to the host again, nothing flips.
        replies = [cursor_reply(5, 0, 0), None, None]
        cpu = Menu6502(self.block, [KEY_1, 0x8F], lambda dcb: replies.pop(0) if replies else None)
        self.assertEqual(cpu.run_until_idle(), "waiting")
        self.assertEqual([c["aux1"] for c in cpu.siov_calls], [0xFF, KEY_1, make_menu.NAV_DOWN])
        self.assertEqual(cpu.screen()[make_menu.CURSOR_AT + 2], 0)

    # --- menu-navegacion (N001): arrows, joystick of port 1, fire ---

    def test_control_arrows_are_sent_as_navigation_codes(self):
        # Control + - = + * (arrows), plain =, and Control+Shift+= (still down).
        keys = [0x8E, 0x8F, 0x86, 0x87, 0x0F, 0xCF, 0x0C]
        cpu = Menu6502(self.block, keys, lambda dcb: host_reply(0, 0))
        self.assertEqual(cpu.run_until_idle(), "waiting")
        nav = make_menu.NAV_UP, make_menu.NAV_DOWN, make_menu.NAV_LEFT, make_menu.NAV_RIGHT
        self.assertEqual([c["aux1"] for c in cpu.siov_calls],
                         [0xFF, nav[0], nav[1], nav[2], nav[3], 0x0F, nav[1], 0x0C])
        self.assertEqual(cpu.mem[make_menu.CH], 0xFF)

    def test_navigation_codes_stay_clear_of_keys_poll_and_redraw(self):
        codes = [make_menu.NAV_UP, make_menu.NAV_DOWN, make_menu.NAV_LEFT, make_menu.NAV_RIGHT,
                 make_menu.NAV_FIRE]
        self.assertEqual(codes, [0x40, 0x41, 0x42, 0x43, 0x44])
        for code in codes:
            self.assertGreater(code, make_menu.KEY_MASK)
            self.assertNotIn(code, (make_menu.POLL, make_menu.NO_KEY))

    def joystick_sends(self, joy, until, action=0, rtclok=0, slow=None):
        replies = [host_reply(action, 1)] + [host_reply(0, 1)] * 200
        cpu = None

        def siov(dcb):
            # slow = (n, frames): the n-th call (1-based, after the first draw) takes that long.
            if slow is not None and len(cpu.siov_calls) == slow[0] + 1:
                cpu.frames += slow[1]
                cpu.mem[make_menu.RTCLOK_LOW] = (cpu.mem[make_menu.RTCLOK_LOW] + slow[1]) & 0xFF
            return replies.pop(0)

        cpu = Menu6502(self.block, [], siov)
        cpu.mem[make_menu.RTCLOK_LOW] = rtclok
        cpu.joy, cpu.joy_until = joy, until
        try:
            cpu.run(max_steps=until * STEPS_PER_FRAME + 20000)
        except Halt:
            pass
        except AssertionError as error:
            if "did not finish" not in str(error):
                raise
        return [(c["aux1"], c["frames"]) for c in cpu.siov_calls[1:]]

    def test_a_held_direction_repeats_after_30_frames_then_every_9(self):
        # RTCLOK starts at 240, so the count crosses its wrap to 0.
        down = lambda frame: (13, 1) if frame < 70 else (15, 1)  # noqa: E731
        sends = self.joystick_sends(down, 90, rtclok=240)
        self.assertEqual([code for code, _ in sends], [make_menu.NAV_DOWN] * 5)
        frames = [frame for _, frame in sends]
        # menu-presentacion: every answer is shown after a vertical blank, one frame the repeat
        # does not count, so 30/9 became 31/10 here (6 moves a second instead of 6.7). Five
        # sends fit in the 70 frames where six did.
        self.assertEqual([b - a for a, b in zip(frames, frames[1:])], [31, 10, 10, 10])

    def test_a_slow_answer_only_pauses_the_repeat(self):
        # The third send waits 200 frames inside SIOV (more than 127): the repeat resumes
        # 8 frames after the answer (the jump counts as one frame seen), not 2 s later.
        down = lambda frame: (13, 1) if frame < 300 else (15, 1)  # noqa: E731
        sends = self.joystick_sends(down, 320, slow=(3, 200))
        frames = [frame for _, frame in sends]
        # menu-presentacion: 200 + 10 and then 10 (was 200 + 8 and 9): the vertical blank before
        # each answer is shown, as in test_a_held_direction_repeats_after_30_frames_then_every_9.
        self.assertEqual(frames[3] - frames[2], 200 + 10)
        self.assertEqual(frames[4] - frames[3], 10)

    def test_a_new_direction_is_sent_at_once_and_restarts_the_delay(self):
        joy = lambda f: (14, 1) if f < 10 else (15, 1) if f < 12 else (7, 1) if f < 20 else (15, 1)  # noqa: E731
        sends = self.joystick_sends(joy, 40)
        self.assertEqual([code for code, _ in sends], [make_menu.NAV_UP, make_menu.NAV_RIGHT])

    def test_left_is_sent_as_its_code_and_a_diagonal_is_ignored(self):
        joy = lambda f: (11, 1) if f < 5 else (10, 1) if f < 60 else (15, 1)  # noqa: E731
        sends = self.joystick_sends(joy, 70)
        self.assertEqual([code for code, _ in sends], [make_menu.NAV_LEFT])

    def test_fire_held_for_120_frames_confirms_once_per_press(self):
        joy = lambda f: (15, 0) if 5 <= f < 125 else (15, 0) if 140 <= f < 150 else (15, 1)  # noqa: E731
        sends = self.joystick_sends(joy, 170)
        self.assertEqual([code for code, _ in sends], [make_menu.NAV_FIRE, make_menu.NAV_FIRE])

    def test_fire_held_while_the_menu_starts_is_not_a_confirmation(self):
        joy = lambda f: (15, 0) if f < 50 else (15, 1)  # noqa: E731
        self.assertEqual(self.joystick_sends(joy, 70), [])

    def test_the_joystick_during_the_wait_is_sent_instead_of_the_poll(self):
        joy = lambda f: (13, 1) if 3 <= f < 6 else (15, 1)  # noqa: E731
        sends = self.joystick_sends(joy, 10, action=2)
        self.assertEqual(sends[0][0], make_menu.NAV_DOWN)
        self.assertLess(sends[0][1], 15)  # before the 15-frame poll

    # --- N007: the cursor moves on the Atari; only RETURN, fire and a page change ask the host.

    def local_run(self, keys, reply):
        cpu = Menu6502(self.block, keys, lambda dcb: reply)
        self.assertEqual(cpu.run_until_idle(), "waiting")
        return cpu, [c["aux1"] for c in cpu.siov_calls[1:]]

    def test_up_and_down_move_the_bar_without_asking_the_host(self):
        cpu, sent = self.local_run([0x8F, 0x8F, 0x8F, 0x8E], cursor_reply(5, 0, make_menu.PAGE_BEFORE | make_menu.PAGE_AFTER))
        self.assertEqual(sent, [])
        self.assertEqual(marked_rows(cpu.screen(), 5), [2])
        self.assertEqual(cpu.screen()[make_menu.CURSOR_AT + 3], 2)

    def test_every_key_carries_the_row_the_bar_shows(self):
        # A key the 6502 does not take sends the marked row in AUX2 bits 4-0, so the host
        # keeps the bar where the Atari shows it (ADR-017, enmienda 4, punto 2).
        cpu = Menu6502(self.block, [0x8F, 0x8F, KEY_A, 0x8F, KEY_ESC], lambda dcb: cursor_reply(5, 0, 0))
        self.assertEqual(cpu.run_until_idle(), "waiting")
        calls = cpu.siov_calls
        self.assertEqual([c["aux1"] for c in calls], [0xFF, KEY_A, KEY_ESC])
        self.assertEqual([c["aux2"] & make_menu.ROW_MASK for c in calls], [make_menu.NO_ROW, 2, 1])
        self.assertEqual([c["aux2"] >> 5 for c in calls], [1, 2, 3])

    def test_return_and_fire_send_the_marked_row(self):
        # Each answer draws row 1 again, so the second RETURN (with Shift) sends row 1.
        cpu, sent = self.local_run([0x8F, 0x8F, 0x0C, 0x4C], cursor_reply(5, 1, 0))
        self.assertEqual(sent, [make_menu.CONFIRM_ROW + 3, make_menu.CONFIRM_ROW + 1])
        cpu, sent = self.local_run([0x8F], cursor_reply(20, 18, 0))
        self.assertEqual(marked_rows(cpu.screen(), 20), [19])

    def test_the_edges_ask_the_host_only_when_there_are_more_pages(self):
        both = make_menu.PAGE_BEFORE | make_menu.PAGE_AFTER
        _, sent = self.local_run([0x8E, 0x86, 0x87], cursor_reply(5, 0, both))
        self.assertEqual(sent, [make_menu.NAV_UP, make_menu.NAV_LEFT, make_menu.NAV_RIGHT])
        _, sent = self.local_run([0x8F], cursor_reply(5, 4, both))
        self.assertEqual(sent, [make_menu.NAV_DOWN])
        cpu, sent = self.local_run([0x8E, 0x86, 0x87, 0x8F, 0x8F, 0x8F, 0x8F, 0x8F], cursor_reply(3, 0, 0))
        self.assertEqual(sent, [])
        self.assertEqual(marked_rows(cpu.screen(), 3), [2])

    def test_a_move_toward_a_missing_page_keeps_the_row_and_sends_nothing(self):
        # First of two pages (only a page after): row 3, then left and up from row 0 stay here.
        cpu, sent = self.local_run([0x8F, 0x8F, 0x8F, 0x86], cursor_reply(20, 0, make_menu.PAGE_AFTER))
        self.assertEqual(sent, [])
        self.assertEqual(marked_rows(cpu.screen(), 20), [3])
        _, sent = self.local_run([0x8E], cursor_reply(20, 0, make_menu.PAGE_AFTER))
        self.assertEqual(sent, [])
        _, sent = self.local_run([0x87], cursor_reply(20, 3, make_menu.PAGE_AFTER))
        self.assertEqual(sent, [make_menu.NAV_RIGHT])
        # Last of two pages (only a page before): right and down from the bottom stay here.
        cpu, sent = self.local_run([0x8F, 0x8F, 0x8F, 0x87], cursor_reply(5, 0, make_menu.PAGE_BEFORE))
        self.assertEqual(sent, [])
        self.assertEqual(marked_rows(cpu.screen(), 5), [3])
        _, sent = self.local_run([0x8F], cursor_reply(5, 4, make_menu.PAGE_BEFORE))
        self.assertEqual(sent, [])
        _, sent = self.local_run([0x86, 0x8E], cursor_reply(5, 0, make_menu.PAGE_BEFORE))
        self.assertEqual(sent, [make_menu.NAV_LEFT, make_menu.NAV_UP])

    def test_the_main_screen_cursor_runs_from_its_own_base(self):
        base = make_menu.MAIN_SOURCES
        cpu, sent = self.local_run([0x8F, 0x8F], cursor_reply(8, 0, 0, base=base))
        self.assertEqual(sent, [])
        self.assertEqual(marked_rows(cpu.screen(), 8, base=base), [2])
        _, sent = self.local_run([0x8F, 0x8F, 0x0C], cursor_reply(8, 0, 0, base=base))
        self.assertEqual(sent, [make_menu.CONFIRM_ROW + 2])

    def test_letters_esc_and_digits_still_go_to_the_host(self):
        _, sent = self.local_run([KEY_A, KEY_ESC, KEY_1], cursor_reply(5, 0, 0))
        self.assertEqual(sent, [KEY_A, KEY_ESC, KEY_1])

    def test_no_cursor_and_the_wait_send_every_move_as_before(self):
        _, sent = self.local_run([0x8F, 0x0C], host_reply(0, 1))
        self.assertEqual(sent, [make_menu.NAV_DOWN, 0x0C])
        # During the wait (action 2) the host owns every move, as before.
        replies = [cursor_reply(5, 0, 0, action=2)] + [host_reply(0, 1)] * 5
        cpu = Menu6502(self.block, [], lambda dcb: replies.pop(0))
        cpu.wait_key = (2, 0x8F)
        self.assertEqual(cpu.run_until_idle(), "waiting")
        self.assertEqual(cpu.siov_calls[1]["aux1"], make_menu.NAV_DOWN)

    def test_the_joystick_moves_the_bar_locally(self):
        down = lambda frame: (13, 1) if 2 <= frame < 4 else (15, 0) if 6 <= frame < 8 else (15, 1)  # noqa: E731
        cpu = Menu6502(self.block, [], lambda dcb: cursor_reply(5, 0, 0))
        cpu.joy, cpu.joy_until = down, 20
        try:
            cpu.run(max_steps=20 * STEPS_PER_FRAME + 20000)
        except Halt:
            pass
        self.assertEqual([c["aux1"] for c in cpu.siov_calls[1:]], [make_menu.CONFIRM_ROW + 1])

    def test_the_cursor_block_fits_after_every_layout_and_codes_stay_clear(self):
        end = make_menu.CURSOR_AT + make_menu.CURSOR_SIZE
        self.assertLessEqual(end, make_menu.SCREEN_SIZE)
        self.assertGreaterEqual(make_menu.CURSOR_AT, max(make_menu.MAIN_END, make_menu.LIST_END))
        codes = range(make_menu.CONFIRM_ROW, make_menu.CONFIRM_ROW + make_menu.LIST_MAX_ENTRIES)
        self.assertGreater(min(codes), make_menu.NAV_FIRE)
        self.assertLess(max(codes), make_menu.POLL)
        self.assertEqual(self.labels["cur_base"], self.labels["screen"] + make_menu.CURSOR_AT)

    # --- Text input (menu-editar-servidor, M43) -------------------------------------------

    def test_the_field_edits_on_the_atari_and_return_sends_every_position(self):
        keys = [0x3F, 0x15 | 0x40, 0x22, 0x1F, KEY_DELETE, 0x33,  # a, Shift+b, ., 1, Delete, 7
                0x3F | 0x80, 0x1F | 0x40, 0x26, KEY_RETURN]       # Control+a, Shift+1, /: not text
        cpu = Menu6502(self.block, keys, edit_host(edit_reply("srv")))
        self.assertEqual(cpu.run_until_idle(), "waiting")
        k = [c for c in cpu.siov_calls if c["command"] == make_menu.MENU_COMMAND]
        t = [c for c in cpu.siov_calls if c["command"] == make_menu.TEXT_COMMAND]
        self.assertEqual([c["aux1"] for c in k], [0xFF, make_menu.EDIT_SAVE])
        self.assertEqual([c["aux1"] for c in t], list(range(32)))
        self.assertEqual(bytes(c["aux2"] for c in t), b"srvaB.7".ljust(32))
        for c in t:
            self.assertEqual((c["device"], c["unit"], c["dstats"]), (0x5A, 1, 0x00))
            self.assertEqual(c["timeout"], make_menu.MENU_TIMEOUT_S)
        # The save comes last and is a new key (sequence) with no row.
        self.assertEqual(cpu.siov_calls[-1]["command"], make_menu.MENU_COMMAND)
        self.assertNotEqual(k[0]["aux2"] >> 5, k[1]["aux2"] >> 5)
        self.assertEqual(k[1]["aux2"] & make_menu.ROW_MASK, make_menu.NO_ROW)

    def test_the_cursor_follows_the_text_and_is_gone_before_sending(self):
        seen = []

        def siov(dcb):
            if dcb["command"] == make_menu.TEXT_COMMAND:
                if dcb["aux1"] == 0:
                    seen.append(field_text(cpu.screen()))
                return b""
            return edit_reply("ab") if not seen else host_reply(0, 0)
        cpu = Menu6502(self.block, [0x2D], siov)  # t
        self.assertEqual(cpu.run_until_idle(), "waiting")
        typed = field_text(cpu.screen())
        self.assertEqual(typed[:3], make_menu.screen_codes("abt", 3))
        self.assertEqual(typed[3], 0x80, "the cursor: an inverse space after the text")
        self.assertTrue(all(b == 0 for b in typed[4:]))
        cpu.keys = [KEY_RETURN]
        cpu.run_until_idle(pc=self.labels["ed_key"])
        self.assertEqual(seen[0][:4], make_menu.screen_codes("abt ", 4), "no inverse cell is sent")

    def test_the_field_takes_32_characters_and_delete_stops_at_the_start(self):
        cpu = Menu6502(self.block, [0x3F, 0x15, KEY_RETURN], edit_host(edit_reply("x" * 31)))
        cpu.run_until_idle()
        t = [c["aux2"] for c in cpu.siov_calls if c["command"] == make_menu.TEXT_COMMAND]
        self.assertEqual(bytes(t), b"x" * 31 + b"a")
        cpu = Menu6502(self.block, [KEY_DELETE, KEY_DELETE, 0x3F, KEY_RETURN], edit_host(edit_reply("")))
        cpu.run_until_idle()
        t = [c["aux2"] for c in cpu.siov_calls if c["command"] == make_menu.TEXT_COMMAND]
        self.assertEqual(bytes(t), b"a".ljust(32))
        # A length past the field from the host is taken as a full field.
        cpu = Menu6502(self.block, [0x3F, KEY_RETURN], edit_host(edit_reply("y" * 32, length=200)))
        cpu.run_until_idle()
        t = [c["aux2"] for c in cpu.siov_calls if c["command"] == make_menu.TEXT_COMMAND]
        self.assertEqual(bytes(t), b"y" * 32)

    def test_esc_sends_esc_and_no_text(self):
        cpu = Menu6502(self.block, [0x3F, KEY_ESC | 0x40], edit_host(edit_reply("srv")))
        self.assertEqual(cpu.run_until_idle(), "waiting")
        self.assertEqual([(c["command"], c["aux1"]) for c in cpu.siov_calls],
                         [(make_menu.MENU_COMMAND, 0xFF), (make_menu.MENU_COMMAND, KEY_ESC)])

    def test_a_text_command_without_answer_shows_that_the_host_does_not_answer(self):
        cpu = Menu6502(self.block, [KEY_RETURN], edit_host(edit_reply("srv"), fail_text_at=5))
        self.assertEqual(cpu.run_until_idle(), "waiting")
        t = [c for c in cpu.siov_calls if c["command"] == make_menu.TEXT_COMMAND]
        self.assertEqual(len(t), 6)
        self.assertEqual(cpu.siov_calls[-1]["command"], make_menu.TEXT_COMMAND, "no save after a failure")
        message = cpu.screen()[make_menu.MAIN_MESSAGE:make_menu.MAIN_MESSAGE + 40]
        self.assertEqual(message, make_menu.screen_line(make_menu.NO_HOST_TEXT))

    def test_the_edit_codes_stay_clear_of_every_other_code(self):
        self.assertGreaterEqual(make_menu.EDIT_AT, max(make_menu.MAIN_END, make_menu.LIST_END))
        self.assertEqual(make_menu.EDIT_AT + make_menu.EDIT_SIZE, make_menu.CURSOR_AT)
        rows = range(make_menu.CONFIRM_ROW, make_menu.CONFIRM_ROW + make_menu.LIST_MAX_ENTRIES)
        self.assertNotIn(make_menu.EDIT_SAVE, rows)
        self.assertNotIn(make_menu.EDIT_SAVE, (make_menu.POLL, make_menu.NO_KEY))
        self.assertGreater(make_menu.EDIT_SAVE, make_menu.NAV_FIRE)
        self.assertNotIn(make_menu.TEXT_COMMAND, (make_menu.MENU_COMMAND, 0x4D))
        self.assertLessEqual(3 + make_menu.EDIT_WIDTH, make_menu.LINE_WIDTH)
        # Every key with a character maps to it, and nothing else does.
        table = make_menu.char_table()
        self.assertEqual(sum(1 for b in table if b), len(make_menu.KEYBOARD))
        self.assertEqual(table[0x22], make_menu.screen_codes(".", 1)[0])

    # menu-presentacion: the reply arrives out of sight; the screen changes at once.

    def test_the_hidden_buffer_is_ram_after_the_block_on_its_own_page(self):
        hidden = self.labels["hidden"]
        self.assertEqual(hidden & 0xFF, 0)
        self.assertGreaterEqual(hidden, ORG + len(self.block))
        self.assertLess(hidden - (ORG + len(self.block)), 0x100)
        self.assertLessEqual(hidden + make_menu.REPLY_SIZE, 0x4000)

    def test_while_the_list_loads_the_main_screen_stays_with_the_notice(self):
        first, second = host_reply(0, 0, 0x21), host_reply(0, 1, 0x22)
        replies = [first, second]
        cpu = Menu6502(self.block, [KEY_1], lambda dcb: replies.pop(0))
        cpu.run_until_idle()
        seen = cpu.siov_calls[1]["visible"]
        message = make_menu.MAIN_MESSAGE
        expected = first[2:message + 2] + make_menu.screen_line(make_menu.LOADING_TEXT) + first[message + 42:]
        self.assertEqual(seen, expected)
        self.assertEqual(cpu.screen(), second[2:])  # the list, whole

    def test_while_a_key_on_the_list_loads_the_notice_is_on_the_list_message_row(self):
        first, second = host_reply(0, 1, 0x22), host_reply(0, 0, 0x21)
        replies = [first, second]
        cpu = Menu6502(self.block, [KEY_ESC], lambda dcb: replies.pop(0))
        cpu.run_until_idle()
        seen = cpu.siov_calls[1]["visible"]
        message = make_menu.LIST_MESSAGE
        expected = first[2:message + 2] + make_menu.screen_line(make_menu.LOADING_TEXT) + first[message + 42:]
        self.assertEqual(seen, expected)
        self.assertEqual(cpu.screen(), second[2:])

    def test_a_poll_shows_no_notice_over_the_progress_of_the_host(self):
        wait = host_reply(make_menu.ACTION_WAIT, 0, 0x23)
        replies = [wait, host_reply(0, 0)]
        cpu = Menu6502(self.block, [], lambda dcb: replies.pop(0))
        cpu.run_until_idle()
        poll = cpu.siov_calls[1]
        self.assertEqual(poll["aux1"], make_menu.POLL)
        self.assertEqual(poll["visible"], wait[2:])

    def test_the_new_screen_is_copied_after_a_vertical_blank_with_its_display_list(self):
        replies = [host_reply(0, 0), host_reply(0, 1, 0x22)]
        cpu = Menu6502(self.block, [KEY_1], lambda dcb: replies.pop(0))
        cpu.watch = {"show": True}
        cpu.run_until_idle()
        self.assertEqual(len(cpu.visits), 2)
        self.assertGreater(cpu.visits[1][1], cpu.siov_calls[1]["frames"])  # RTCLOK moved first
        self.assertEqual(cpu.mem[make_menu.DLISTL], self.labels["dl_list"] & 0xFF)
        self.assertEqual(cpu.mem[make_menu.SDLSTL], self.labels["dl_list"] & 0xFF)
        self.assertEqual(cpu.screen(), host_reply(0, 1, 0x22)[2:])

    def test_after_no_answer_on_the_list_the_notice_goes_to_the_main_message_row(self):
        # The error screen uses the main display list, so the next notice must be on its row.
        replies = [host_reply(0, 1, 0x22), None, host_reply(0, 0)]
        cpu = Menu6502(self.block, [KEY_1], lambda dcb: replies.pop(0))
        self.assertEqual(cpu.run_until_idle(), "waiting")
        cpu.keys = [KEY_1]
        cpu.run_until_idle(self.labels["key"])
        message = make_menu.MAIN_MESSAGE
        seen = cpu.siov_calls[2]["visible"]
        self.assertEqual(seen[message:message + 40], make_menu.screen_line(make_menu.LOADING_TEXT))

    def test_a_cut_reply_never_reaches_the_screen(self):
        cut = host_reply(0, 1, 0x2A)
        replies = [host_reply(0, 0), ("cut", cut)]
        cpu = Menu6502(self.block, [KEY_1], lambda dcb: replies.pop(0))
        self.assertEqual(cpu.run_until_idle(), "waiting")
        self.assertNotIn(0x2A, cpu.screen())
        message = make_menu.MAIN_MESSAGE
        self.assertEqual(cpu.screen()[message:message + 40], make_menu.screen_line(make_menu.NO_HOST_TEXT))

    def test_committed_include_matches_the_generator(self):
        include = REPO / "components" / "rb_menu" / "include" / "rb" / "menu" / "menu_program.inc"
        result = subprocess.run([sys.executable, "-I", str(TOOLS / "make_menu.py"), "--check", str(include)],
                                capture_output=True, text=True)
        self.assertEqual(result.returncode, 0, result.stderr)

    def test_screen_line_maps_atascii_to_screen_codes(self):
        self.assertEqual(make_menu.screen_line("A a!")[:4], bytes([0x21, 0x00, 0x61, 0x01]))
        with self.assertRaises(ValueError):
            make_menu.screen_line("x" * 41)


if __name__ == "__main__":
    unittest.main()
