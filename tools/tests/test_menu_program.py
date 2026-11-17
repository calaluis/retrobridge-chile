"""Tests for tools/make_menu.py: the 6502 boot menu of retrobridge_host (Issue #38).

The program is run by a small interpreter of the instructions it uses. SIOV and
COLDSV are trapped: SIOV calls a Python function that plays the host, COLDSV
ends the run. Keys are fed through CH as the OS keyboard handler does.
"""

import subprocess
import sys
import unittest
from pathlib import Path

TOOLS = Path(__file__).resolve().parent.parent
sys.path.insert(0, str(TOOLS))

import make_menu  # noqa: E402

REPO = TOOLS.parent


class Halt(Exception):
    pass


class Menu6502:
    """Interprets the menu program. Any opcode it does not know fails the test."""

    def __init__(self, block: bytes, count: int, keys, siov):
        self.mem = bytearray(0x10000)
        self.mem[make_menu.MENU_ORG:make_menu.MENU_ORG + len(block)] = block
        self.labels = make_menu.menu_program()[1]
        self.mem[self.labels["count"]] = count
        # Leftovers in the DCB, so a store the program forgets is seen by the tests.
        self.mem[make_menu.DDEVIC:make_menu.DAUX2 + 1] = bytes([0xAA]) * (make_menu.DAUX2 + 1 - make_menu.DDEVIC)
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
        pc = make_menu.MENU_ORG + 6 if pc is None else pc
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
            elif op == 0x8D:
                self.mem[ab] = self.a; pc += 3
            elif op == 0x8E:
                self.mem[ab] = self.x; pc += 3
            elif op == 0xC5:
                self.flags((self.a - self.read(zp)) & 0xFF); pc += 2
            elif op == 0xC9:
                self.flags((self.a - imm) & 0xFF); pc += 2
            elif op == 0xEC:
                self.flags((self.x - self.read(ab)) & 0xFF); pc += 3
            elif op == 0xDD:
                self.flags((self.a - self.read(ab + self.x)) & 0xFF); pc += 3
            elif op == 0x29:
                self.a &= imm; self.flags(self.a); pc += 2
            elif op == 0xE8:
                self.x = (self.x + 1) & 0xFF; self.flags(self.x); pc += 1
            elif op == 0x98:
                self.a = self.y; self.flags(self.a); pc += 1
            elif op in (0xF0, 0xD0, 0x30):
                offset = imm - 256 if imm > 127 else imm
                taken = {0xF0: self.z, 0xD0: not self.z, 0x30: self.n}[op]
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

    def status_line(self):
        dl_status = self.labels["dl_status"]
        return bytes(self.mem[self.word(dl_status):self.word(dl_status) + make_menu.LINE_WIDTH])

    def run_until_idle(self, pc=None):
        """Runs from the entry point (or pc) until a cold start or an idle key wait."""
        self.idle_key_reads = 0
        try:
            return self.run(pc)
        except Halt:
            return "waiting"


def host_reply(status: int, text: str) -> bytes:
    return bytes([status]) + make_menu.screen_line(text)


KEY_A, KEY_B, KEY_C, KEY_T = 0x3F, 0x15, 0x12, 0x2D


class MenuProgramTest(unittest.TestCase):

    @classmethod
    def setUpClass(cls):
        cls.block, cls.labels = make_menu.menu_program()

    def test_boot_record_loads_the_whole_block_at_0700(self):
        self.assertEqual(self.block[0], 0x00)
        self.assertEqual(self.block[1] * 128, len(self.block))
        self.assertEqual(self.block[2] | self.block[3] << 8, 0x0700)
        self.assertEqual(self.block[4] | self.block[5] << 8, self.labels["init"])
        self.assertEqual(self.block[self.labels["init"] - 0x0700], 0x60)

    def test_display_list_shows_title_entries_prompt_and_status(self):
        dl = self.labels["dl"] - 0x0700
        b = self.block
        self.assertEqual(b[dl:dl + 3], bytes([0x70, 0x70, 0x42]))
        self.assertEqual(b[dl + 3] | b[dl + 4] << 8, self.labels["screen"])
        self.assertEqual(b[dl + 5:dl + 6 + 20], bytes([0x30]) + bytes([0x02] * 20))
        self.assertEqual(b[dl + 26:dl + 29], bytes([0x30, 0x02, 0x42]))
        self.assertEqual(b[dl + 29] | b[dl + 30] << 8, self.labels["reply_text"])
        self.assertEqual(b[dl + 31], 0x41)
        self.assertEqual(b[dl + 32] | b[dl + 33] << 8, self.labels["dl"])
        screen = self.labels["screen"] - 0x0700
        self.assertEqual(b[screen:screen + 40], make_menu.screen_line(make_menu.MENU_TITLE_TEXT))
        prompt = self.labels["entries"] - 0x0700 + 20 * 40
        self.assertEqual(b[prompt:prompt + 40], make_menu.screen_line(make_menu.MENU_PROMPT_TEXT))

    def test_key_codes_are_the_letters_a_to_t(self):
        # Atari keyboard codes (unshifted) of A..T.
        self.assertEqual(make_menu.KEY_CODES, bytes([
            0x3F, 0x15, 0x12, 0x3A, 0x2A, 0x38, 0x3D, 0x39, 0x0D, 0x01,
            0x05, 0x00, 0x25, 0x23, 0x08, 0x0A, 0x2F, 0x28, 0x3E, 0x2D]))
        keytab = self.labels["keytab"] - 0x0700
        self.assertEqual(self.block[keytab:keytab + 20], make_menu.KEY_CODES)

    def test_valid_choice_sends_the_menu_command_and_cold_starts(self):
        cpu = Menu6502(self.block, 3, [KEY_B], lambda dcb: host_reply(0, " ARRANCANDO"))
        self.assertEqual(cpu.run_until_idle(), "coldstart")
        self.assertEqual(len(cpu.siov_calls), 1)
        call = cpu.siov_calls[0]
        self.assertEqual((call["device"], call["unit"], call["command"]), (0x5A, 1, 0x4D))
        self.assertEqual((call["dstats"], call["aux1"], call["aux2"]), (0x40, 1, 0))
        self.assertEqual((call["buffer"], call["length"]), (self.labels["reply"], 41))
        self.assertEqual(call["timeout"], make_menu.MENU_TIMEOUT_S)
        self.assertEqual(cpu.word(make_menu.SDLSTL), self.labels["dl"])

    def test_shift_and_control_are_ignored(self):
        cpu = Menu6502(self.block, 3, [KEY_C | 0x40], lambda dcb: host_reply(0, ""))
        self.assertEqual(cpu.run_until_idle(), "coldstart")
        self.assertEqual(cpu.siov_calls[0]["aux1"], 2)

    def test_letter_outside_the_list_does_nothing(self):
        cpu = Menu6502(self.block, 2, [KEY_C, KEY_T], lambda dcb: host_reply(0, ""))
        self.assertEqual(cpu.run_until_idle(), "waiting")
        self.assertEqual(cpu.siov_calls, [])

    def test_refused_image_shows_the_host_line_and_waits_for_another_key(self):
        replies = [host_reply(1, " ERROR: ATR INVALIDA"), host_reply(0, " ARRANCANDO")]
        cpu = Menu6502(self.block, 3, [KEY_C], lambda dcb: replies.pop(0))
        self.assertEqual(cpu.run_until_idle(), "waiting")
        self.assertEqual(cpu.status_line(), make_menu.screen_line(" ERROR: ATR INVALIDA"))
        cpu.keys = [KEY_A]
        self.assertEqual(cpu.run_until_idle(self.labels["key"]), "coldstart")
        self.assertEqual([c["aux1"] for c in cpu.siov_calls], [2, 0])

    def test_no_answer_shows_the_no_host_line(self):
        cpu = Menu6502(self.block, 3, [KEY_A], lambda dcb: None)
        self.assertEqual(cpu.run_until_idle(), "waiting")
        self.assertEqual(cpu.status_line(), make_menu.screen_line(make_menu.MENU_NO_HOST_TEXT))

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
