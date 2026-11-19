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
        for step in range(max_steps):
            if step % STEPS_PER_FRAME == STEPS_PER_FRAME - 1:
                self.mem[make_menu.RTCLOK_LOW] = (self.mem[make_menu.RTCLOK_LOW] + 1) & 0xFF
                self.frames += 1
            self.pc = pc
            if pc == self.labels.get("tick"):
                self.ticks_waited += 1
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
        # Shift+A goes out as A: the program sends CH AND $3F (menu-tnfs, M41-3).
        self.assertEqual([c["aux1"] for c in cpu.siov_calls], [0xFF, KEY_1, KEY_ESC, KEY_A])
        # One sequence number per key, so the host can tell a new key from SIOV's retry.
        self.assertEqual([c["aux2"] for c in cpu.siov_calls], [1, 2, 3, 4])

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
        self.assertEqual([c["aux2"] for c in calls], [1, 2, 3, 4])
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
        self.assertEqual([c["aux2"] for c in calls], [1, 2, 3])
        ticks = (calls[2]["rtclok"] - calls[1]["rtclok"]) & 0xFF
        self.assertLess(ticks, 15)  # sent at once, without waiting the rest
        self.assertEqual(cpu.mem[make_menu.CH], 0xFF)

    def test_boot_after_a_wait_still_cold_starts(self):
        replies = [host_reply(0, 1), host_reply(2, 1), host_reply(1, 0)]
        cpu = Menu6502(self.block, [KEY_A], lambda dcb: replies.pop(0))
        self.assertEqual(cpu.run_until_idle(), "coldstart")
        self.assertEqual([c["aux1"] for c in cpu.siov_calls], [0xFF, KEY_A, 0xFE])

    def test_an_unknown_action_still_cold_starts(self):
        # Only action 2 waits; any other non-zero action boots, as before menu-tnfs.
        for action in (3, 0xFF):
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
        self.assertEqual(cpu.screen()[message:message + 40], make_menu.screen_line(make_menu.NO_HOST_TEXT))
        self.assertEqual(cpu.screen()[message - 1], 0x21)  # nothing else was overwritten
        self.assertEqual(cpu.screen()[message + 40], 0x21)

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
        self.assertEqual([code for code, _ in sends], [make_menu.NAV_DOWN] * 6)
        frames = [frame for _, frame in sends]
        self.assertEqual([b - a for a, b in zip(frames, frames[1:])], [30, 9, 9, 9, 9])

    def test_a_slow_answer_only_pauses_the_repeat(self):
        # The third send waits 200 frames inside SIOV (more than 127): the repeat resumes
        # 8 frames after the answer (the jump counts as one frame seen), not 2 s later.
        down = lambda frame: (13, 1) if frame < 300 else (15, 1)  # noqa: E731
        sends = self.joystick_sends(down, 320, slow=(3, 200))
        frames = [frame for _, frame in sends]
        self.assertEqual(frames[3] - frames[2], 200 + 8)
        self.assertEqual(frames[4] - frames[3], 9)

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
