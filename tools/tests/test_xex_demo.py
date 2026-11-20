"""Tests for tools/make_xex_demo.py: the RetroBridge test .xex of the demo (spec menu-xex, X005).

The .xex runs end to end in the 6502 interpreter of test_xex_loader: the loader reads it from the virtual disk,
calls its INITAD and jumps to its RUNAD; the program then shows two lines with its own display list.
"""

import subprocess
import sys
import tempfile
import unittest
from unittest import mock
from pathlib import Path

TOOLS = Path(__file__).resolve().parent.parent
sys.path.insert(0, str(TOOLS))
sys.path.insert(0, str(Path(__file__).resolve().parent))
import make_menu  # noqa: E402
import make_xex_demo as demo  # noqa: E402
import make_xex_loader as loader  # noqa: E402
from test_xex_loader import PORTB_BASIC_ON, Cpu, virtual_disk  # noqa: E402


def segments(xex: bytes):
    """(start, end) of each segment; fails the test on a malformed file."""
    assert xex[:2] == b"\xFF\xFF", "starts with $FFFF"
    found, at = [], 0
    while at < len(xex):
        if xex[at:at + 2] == b"\xFF\xFF":
            at += 2
        start, end = xex[at] | (xex[at + 1] << 8), xex[at + 2] | (xex[at + 3] << 8)
        assert start <= end and at + 4 + end - start + 1 <= len(xex), "a whole segment"
        found.append((start, end))
        at += 4 + end - start + 1
    return found


def lines_shown(cpu):
    dl = cpu.word(make_menu.SDLSTL)
    shown = []
    for n in range(2):
        lms = cpu.word(dl + 4 + 3 * n)
        shown.append(bytes(cpu.mem[lms:lms + 40]))
    return dl, shown


class XexDemoTest(unittest.TestCase):
    def setUp(self):
        self.xex = demo.demo_xex()

    def test_is_a_valid_xex_the_host_accepts(self):
        found = segments(self.xex)
        self.assertEqual([s for s in found if s[0] >= 0x02E0 and s[1] <= 0x02E3],
                         [(demo.INITAD, demo.INITAD + 1), (demo.RUNAD, demo.RUNAD + 1)])
        for start, end in found:
            self.assertFalse(start <= loader.loader_high() and end >= loader.loader_low(), "clear of the loader")
            self.assertFalse(start <= 0x01FF and end >= 0x0100, "clear of page 1")

    def test_after_the_loader_it_says_ok_with_basic_off(self):
        cpu = Cpu(virtual_disk(self.xex))
        result, _ = cpu.run()
        self.assertEqual(result, "halt")
        dl, shown = lines_shown(cpu)
        lms = [cpu.word(dl + 4), cpu.word(dl + 7)]
        expected = bytes([0x70, 0x70, 0x70, 0x42, lms[0] & 0xFF, lms[0] >> 8, 0x42, lms[1] & 0xFF, lms[1] >> 8,
                          0x41, dl & 0xFF, dl >> 8])
        self.assertEqual(bytes(cpu.mem[dl:dl + 12]), expected, "two mode 2 lines, then JVB to itself")
        self.assertEqual(shown, [make_menu.screen_codes(demo.OK_TEXT), make_menu.screen_codes(demo.BASIC_OFF_TEXT)])

    def run_program(self, portb):
        """The program alone, loaded where the .xex puts it, without the loader (so no INITAD)."""
        cpu = Cpu(virtual_disk(self.xex))
        start = segments(self.xex)[0][0]
        block = demo.program_block()
        cpu.mem[start:start + len(block)] = block
        cpu.mem[loader.PORTB] = portb
        result, _ = cpu.run(pc=demo.labels()["main"])
        self.assertEqual(result, "halt")
        return lines_shown(cpu)[1]

    def test_without_initad_it_says_so(self):
        self.assertEqual(self.run_program(PORTB_BASIC_ON | 0x02)[0], make_menu.screen_codes(demo.NO_INIT_TEXT))

    def test_with_basic_on_it_says_so(self):
        self.assertEqual(self.run_program(PORTB_BASIC_ON)[1], make_menu.screen_codes(demo.BASIC_ON_TEXT))

    def test_a_layout_across_a_1kb_block_is_refused(self):
        dl_offset = demo.labels()["dl"] - demo.DEMO_ORG
        with mock.patch.object(demo, "DEMO_ORG", 0x2400 - dl_offset - 4):  # the texts land after $2400
            with self.assertRaises(ValueError):
                demo.program_block()

    def test_out_writes_the_file(self):
        with tempfile.TemporaryDirectory() as folder:
            out = Path(folder) / "RBPRUEBA.XEX"
            result = subprocess.run([sys.executable, "-I", str(TOOLS / "make_xex_demo.py"), "--out", str(out)],
                                    capture_output=True, text=True)
            self.assertEqual(result.returncode, 0, result.stderr)
            self.assertEqual(out.read_bytes(), self.xex)


if __name__ == "__main__":
    unittest.main()
