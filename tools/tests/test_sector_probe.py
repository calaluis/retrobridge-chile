"""Tests for tools/make_sector_probe.py: the disk that reads sector 65535 of D2 (menu-tnfs, AC14, M41-7).

The probe runs in the Menu6502 interpreter of test_menu_program.py, with SIOV
trapped: a Python function plays D2 and returns the 256 bytes of the sector.
"""

import contextlib
import io
import sys
import tempfile
import unittest
from pathlib import Path

TOOLS = Path(__file__).resolve().parent.parent
sys.path.insert(0, str(TOOLS))
sys.path.insert(0, str(Path(__file__).resolve().parent))

import make_menu  # noqa: E402
import make_sector_probe as probe  # noqa: E402
from test_menu_program import Menu6502  # noqa: E402

SDMCTL = 0x022F
SECTOR = bytes((i * 7 + 3) & 0xFF for i in range(256))
EVERY_BYTE = bytes(range(256))


def hex_codes(value: int) -> bytes:
    """Two ANTIC screen codes for `value` in hexadecimal."""
    return make_menu.screen_codes(f"{value:02X}", 2)


class SectorProbeTest(unittest.TestCase):

    @classmethod
    def setUpClass(cls):
        cls.block, cls.labels = probe.probe_program()

    def run_probe(self, reply):
        calls = []

        def siov(dcb):
            calls.append(dcb)
            return reply

        cpu = Menu6502(self.block, [], siov, labels=self.labels)
        self.assertEqual(cpu.run_until_idle(), "halt")
        return cpu, calls

    def test_boot_record_loads_the_block_at_0700(self):
        self.assertEqual(self.block[0], 0x00)
        self.assertEqual(self.block[1] * 128, len(self.block))
        self.assertEqual(self.block[2] | self.block[3] << 8, 0x0700)
        self.assertEqual(self.block[4] | self.block[5] << 8, self.labels["init"])
        self.assertEqual(self.block[self.labels["init"] - 0x0700], 0x60)  # RTS

    def test_reads_sector_65535_of_d2_with_256_bytes(self):
        cpu, calls = self.run_probe(SECTOR)
        self.assertEqual(len(calls), 1)
        call = calls[0]
        self.assertEqual((call["device"], call["unit"], call["command"]), (0x31, 2, 0x52))
        self.assertEqual((call["aux1"], call["aux2"]), (0xFF, 0xFF))
        self.assertEqual((call["dstats"], call["buffer"], call["length"]), (0x40, 0x0600, 256))
        self.assertGreater(call["timeout"], 0)
        self.assertEqual(bytes(cpu.mem[0x0600:0x0700]), SECTOR)

    def test_shows_every_byte_in_hexadecimal_sixteen_per_line(self):
        cpu, _ = self.run_probe(EVERY_BYTE)
        hex_screen = self.labels["hex_screen"]
        for i, value in enumerate(EVERY_BYTE):
            at = hex_screen + 2 * i
            self.assertEqual(bytes(cpu.mem[at:at + 2]), hex_codes(value), f"byte {i}")
        # 16 bytes per line of 32 characters (narrow playfield).
        self.assertEqual(probe.HEX_LINE_BYTES * 2, probe.LINE_WIDTH)

    def test_shows_the_sio_status(self):
        cpu, _ = self.run_probe(SECTOR)
        at = self.labels["status_digits"]
        self.assertEqual(bytes(cpu.mem[at:at + 2]), hex_codes(0x01))
        cpu, _ = self.run_probe(None)  # no answer: SIOV status $8A
        self.assertEqual(bytes(cpu.mem[at:at + 2]), hex_codes(0x8A))

    def test_display_list_is_narrow_with_two_text_lines_and_sixteen_hex_lines(self):
        cpu, _ = self.run_probe(SECTOR)
        self.assertEqual(cpu.mem[SDMCTL], 0x21)  # narrow playfield, display list DMA on
        dl = self.labels["dl"]
        self.assertEqual(cpu.word(make_menu.SDLSTL), dl)
        text, hex_screen = self.labels["text"], self.labels["hex_screen"]
        expected = (b"\x70\x70\x70\x42" + bytes([text & 0xFF, text >> 8]) + b"\x02"
                    + b"\x70\x42" + bytes([hex_screen & 0xFF, hex_screen >> 8]) + b"\x02" * 15
                    + b"\x41" + bytes([dl & 0xFF, dl >> 8]))
        self.assertEqual(bytes(cpu.mem[dl:dl + len(expected)]), expected)
        # ANTIC's memory scan does not cross a 4 KB boundary inside a block.
        self.assertEqual(hex_screen >> 12, (hex_screen + 16 * probe.LINE_WIDTH - 1) >> 12)
        self.assertEqual(text >> 12, (text + 2 * probe.LINE_WIDTH - 1) >> 12)

    def test_title_names_the_sector_and_the_drive(self):
        cpu, _ = self.run_probe(SECTOR)
        text = self.labels["text"]
        title = bytes(cpu.mem[text:text + probe.LINE_WIDTH])
        self.assertEqual(title, make_menu.screen_codes(probe.TITLE_TEXT, probe.LINE_WIDTH))
        self.assertIn("65535", probe.TITLE_TEXT)
        self.assertIn("D2", probe.TITLE_TEXT)

    def test_the_buffer_and_the_hex_screen_are_outside_the_loaded_block(self):
        end = 0x0700 + len(self.block)
        self.assertLessEqual(probe.BUFFER + 256, 0x0700)  # the buffer ends where the block starts
        self.assertEqual(probe.BUFFER, 0x0600)
        self.assertGreaterEqual(self.labels["hex_screen"], end)

    def test_image_is_a_single_density_disk_that_boots_the_probe(self):
        image = probe.probe_image()
        self.assertEqual(image[0:2], b"\x96\x02")
        self.assertEqual(len(image), 16 + 720 * 128)
        paragraphs = (720 * 128) // 16
        self.assertEqual(image[2] | image[3] << 8 | image[6] << 16, paragraphs)
        self.assertEqual(image[4] | image[5] << 8, 128)
        self.assertEqual(image[16:16 + len(self.block)], self.block)

    def test_command_line_writes_the_image(self):
        with tempfile.TemporaryDirectory() as out:
            with contextlib.redirect_stdout(io.StringIO()):
                self.assertEqual(probe.main(["--out", out]), 0)
            written = Path(out) / "SECTOR-PROBE.ATR"
            self.assertEqual(written.read_bytes(), probe.probe_image())


if __name__ == "__main__":
    unittest.main()
