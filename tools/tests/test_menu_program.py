"""Tests for tools/make_menu.py: the 6502 menu of retrobridge_host (Issues #38 and #40).

The program is run by a small interpreter of the instructions it uses. SIOV and
COLDSV are trapped: SIOV calls a Python function that plays the host, COLDSV
ends the run. Keys are fed through CH as the OS keyboard handler does. The
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


class Halt(Exception):
    pass


class Menu6502:
    """Interprets the menu program. Any opcode it does not know fails the test."""

    def __init__(self, block: bytes, keys, siov):
        self.mem = bytearray(0x10000)
        self.mem[ORG:ORG + len(block)] = block
        self.labels = make_menu.menu_program()[1]
        # Leftovers in the DCB, so a store the program forgets is seen by the tests.
        self.mem[make_menu.DDEVIC:make_menu.DAUX2 + 1] = bytes([0xAA]) * (make_menu.DAUX2 + 1 - make_menu.DDEVIC)
        self.mem[make_menu.CH] = 0x3F  # a key pressed during boot, to be forgotten
        self.keys = list(keys)
        self.siov = siov
        self.siov_calls = []
        self.a = self.x = self.y = 0
        self.z = self.n = False
        self.idle_key_reads = 0

    def word(self, at):
        return self.mem[at] | (self.mem[at + 1] << 8)

    def read(self, at):
        if at == make_menu.RTCLOK_LOW:
            self.mem[at] = (self.mem[at] + 1) & 0xFF
        if at == make_menu.CH and self.mem[at] == 0xFF:
            if self.keys:
                self.mem[at] = self.keys.pop(0)
            else:
                self.idle_key_reads += 1
                if self.idle_key_reads > 3:
                    raise Halt("waiting for a key")
        return self.mem[at]

    def flags(self, value):
        self.z = value == 0
        self.n = value >= 0x80

    def run(self, pc=None, max_steps=200000):
        pc = ORG + 6 if pc is None else pc
        for _ in range(max_steps):
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
                self.flags((self.a - self.read(zp)) & 0xFF); pc += 2
            elif op == 0xC9:
                self.flags((self.a - imm) & 0xFF); pc += 2
            elif op == 0x29:
                self.a &= imm; self.flags(self.a); pc += 2
            elif op == 0xAA:
                self.x = self.a; self.flags(self.x); pc += 1
            elif op == 0xCA:
                self.x = (self.x - 1) & 0xFF; self.flags(self.x); pc += 1
            elif op == 0x98:
                self.a = self.y; self.flags(self.a); pc += 1
            elif op in (0xF0, 0xD0, 0x30, 0x10):
                offset = imm - 256 if imm > 127 else imm
                taken = {0xF0: self.z, 0xD0: not self.z, 0x30: self.n, 0x10: not self.n}[op]
                pc += 2 + (offset if taken else 0)
            elif op == 0x4C:
                if ab == make_menu.COLDSV:
                    return "coldstart"
                pc = ab
            elif op == 0x20 and ab == make_menu.SIOV:
                self.siov_call(); pc += 3
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
        dcb["length"] = self.word(make_menu.DBYTLO)
        self.siov_calls.append(dcb)
        reply = self.siov(dcb)
        if reply is None:
            self.y = 0x8A      # SIO timeout
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
    screen = bytes([marker]) * make_menu.SCREEN_SIZE
    return bytes([action, layout]) + screen


KEY_1, KEY_A, KEY_ESC = 0x1F, 0x3F, 0x1C


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
        expected = (b"\x70\x47" + self.lms("list_title")
                    + b"\x70\x42" + self.lms("list_entries") + b"\x02" * 19
                    + b"\x42" + self.lms("list_footer")
                    + b"\x42" + self.lms("list_message")
                    + b"\x41" + self.lms("dl_list"))
        self.assertEqual(self.at("dl_list", len(expected)), expected)
        # 20 entries are contiguous: the LMS line plus 19 more; the message goes under the footer.
        self.assertEqual(self.labels["list_footer"] - self.labels["list_entries"], 20 * 40)
        self.assertEqual(self.labels["list_message"] - self.labels["list_footer"], 40)

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
        row1 = (b"\x42" + inv("1-8") + b"\x56" + txt(" ABRIR FUENTE") + b"\x42" + inv("E") + b"\x56"
                + txt(" EDITAR SERVIDOR") + b"\x00" * 3)
        row2 = (b"\x42" + inv("A") + b"\x56" + txt("   ARRANCAR D1 ") + b"\x42" + inv("U") + b"\x56"
                + txt(" GESTIONAR UNIDADES"))
        self.assertEqual((len(row1), len(row2)), (40, 40))
        foot = make_menu.MAIN_FOOTER
        self.assertEqual(screen[foot:foot + 80], row1 + row2)

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
        self.assertEqual((call["dstats"], call["aux1"], call["aux2"]), (0x40, 0xFF, 1))
        self.assertEqual((call["buffer"], call["length"]), (self.labels["reply"], 962))
        self.assertEqual(call["timeout"], make_menu.MENU_TIMEOUT_S)
        self.assertEqual(cpu.word(make_menu.SDLSTL), self.labels["dl_main"])
        self.assertEqual(cpu.screen(), bytes([0x21]) * make_menu.SCREEN_SIZE)

    def test_each_key_is_sent_as_its_keyboard_code(self):
        cpu = Menu6502(self.block, [KEY_1, KEY_ESC, KEY_A | 0x40], lambda dcb: host_reply(0, 0))
        self.assertEqual(cpu.run_until_idle(), "waiting")
        self.assertEqual([c["aux1"] for c in cpu.siov_calls], [0xFF, KEY_1, KEY_ESC, KEY_A | 0x40])
        # One sequence number per key, so the host can tell a new key from SIOV's retry.
        self.assertEqual([c["aux2"] for c in cpu.siov_calls], [1, 2, 3, 4])

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
        self.assertEqual(cpu.screen()[message:message + 40], make_menu.screen_line(make_menu.NO_HOST_TEXT))
        self.assertEqual(cpu.screen()[message - 1], 0x21)  # nothing else was overwritten
        self.assertEqual(cpu.screen()[message + 40], 0x21)

    def test_committed_include_matches_the_generator(self):
        include = REPO / "host" / "src" / "menu_program.inc"
        result = subprocess.run([sys.executable, "-I", str(TOOLS / "make_menu.py"), "--check", str(include)],
                                capture_output=True, text=True)
        self.assertEqual(result.returncode, 0, result.stderr)

    def test_screen_line_maps_atascii_to_screen_codes(self):
        self.assertEqual(make_menu.screen_line("A a!")[:4], bytes([0x21, 0x00, 0x61, 0x01]))
        with self.assertRaises(ValueError):
            make_menu.screen_line("x" * 41)


if __name__ == "__main__":
    unittest.main()
