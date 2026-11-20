"""Tests for tools/make_xex_loader.py: the 6502 loader that boots a .xex from D1 (spec menu-xex, AC6 and AC7).

The loader is run in a small 6502 interpreter. SIOV is trapped and answers sector reads from a virtual disk
built the way the host wraps a .xex (X002): the loader sectors, with the .xex length patched in, then the raw
.xex bytes, 128 bytes per sector. Format source: docs/fuentes.md (menu-xex).
"""

import subprocess
import sys
import unittest
from pathlib import Path

TOOLS = Path(__file__).resolve().parent.parent
sys.path.insert(0, str(TOOLS))
import make_menu  # noqa: E402
import make_xex_loader as loader  # noqa: E402

REPO = TOOLS.parent
SECTOR = 128
RUNAD, INITAD = 0x02E0, 0x02E2
PORTB_BASIC_ON = 0xFD  # bit 1 = 0: BASIC enabled (Altirra HRM 2.6)


class Halt(Exception):
    pass


def segment(start: int, data: bytes, header: bool = True) -> bytes:
    end = start + len(data) - 1
    head = b"\xFF\xFF" if header else b""
    return head + bytes([start & 0xFF, start >> 8, end & 0xFF, end >> 8]) + data


def word_segment(address: int, value: int, header: bool = False) -> bytes:
    return segment(address, bytes([value & 0xFF, value >> 8]), header)


def virtual_disk(xex: bytes) -> bytes:
    """Sectors 1..n of the disk the host serves in D1 for this .xex (no ATR header)."""
    block = bytearray(loader.loader_block()[0])
    offset = loader.LENGTH_OFFSET_IN_BLOCK
    block[offset:offset + 3] = len(xex).to_bytes(3, "little")
    data = bytes(xex) + bytes(-len(xex) % SECTOR)
    return bytes(block) + data


class Cpu:
    """Interprets the loader. Any opcode it does not know fails the test."""

    def __init__(self, disk: bytes, fail_sector: int = None, hooks=None):
        block, self.labels = loader.loader_block()
        self.mem = bytearray(0x10000)
        sectors = block[1]
        self.mem[loader.LOADER_ORG:loader.LOADER_ORG + sectors * SECTOR] = disk[:sectors * SECTOR]
        self.mem[loader.PORTB] = PORTB_BASIC_ON
        self.disk = disk
        self.fail_sector = fail_sector
        self.hooks = hooks or {}   # address -> function(cpu); the hook returns like an RTS
        self.reads = []            # sector numbers SIOV was asked for
        self.portb_at_first_read = None
        self.a = self.x = self.y = 0
        self.z = self.n = self.c = False
        self.sp = 0xFF
        self.ticks = 0

    def word(self, at):
        return self.mem[at] | (self.mem[at + 1] << 8)

    def flags(self, value):
        self.z = value == 0
        self.n = value >= 0x80

    def compare(self, register, value):
        self.c = register >= value
        self.flags((register - value) & 0xFF)

    def push(self, value):
        self.mem[0x100 + self.sp] = value
        self.sp = (self.sp - 1) & 0xFF

    def pull(self):
        self.sp = (self.sp + 1) & 0xFF
        return self.mem[0x100 + self.sp]

    def rts(self):
        low = self.pull()
        return ((self.pull() << 8) | low) + 1

    def siov(self):
        m = self.mem
        assert m[make_menu.DDEVIC] == 0x31 and m[make_menu.DUNIT] == 1, "reads D1"
        assert m[make_menu.DCOMND] == 0x52 and m[make_menu.DSTATS] == 0x40, "command R, data to the Atari"
        assert self.word(make_menu.DBYTLO) == SECTOR
        sector = self.word(make_menu.DAUX1)
        if self.portb_at_first_read is None:
            self.portb_at_first_read = m[loader.PORTB]
        self.reads.append(sector)
        if sector == self.fail_sector:
            self.y = 0x90  # device error
            return
        start = (sector - 1) * SECTOR
        buffer = self.word(make_menu.DBUFLO)
        m[buffer:buffer + SECTOR] = self.disk[start:start + SECTOR]
        self.y = 0x01

    def run(self, max_steps=2_000_000):
        """Runs from the boot entry (ORG + 6). Returns ("run", pc) at a hooked RUNAD target or ("halt", pc)."""
        pc = loader.LOADER_ORG + 6
        for _ in range(max_steps):
            self.ticks += 1
            if self.ticks % 200 == 0:
                self.mem[make_menu.RTCLOK_LOW] = (self.mem[make_menu.RTCLOK_LOW] + 1) & 0xFF
            if pc in self.hooks:
                result = self.hooks[pc](self)
                if result is not None:
                    return result, pc
                pc = self.rts()
                continue
            op = self.mem[pc]
            imm, ab = self.mem[pc + 1], self.word(pc + 1)
            if op == 0xA9:
                self.a = imm; self.flags(self.a); pc += 2
            elif op == 0xA2:
                self.x = imm; self.flags(self.x); pc += 2
            elif op == 0xA5:
                self.a = self.mem[imm]; self.flags(self.a); pc += 2
            elif op == 0xC5:
                self.compare(self.a, self.mem[imm]); pc += 2
            elif op == 0xAD:
                self.a = self.mem[ab]; self.flags(self.a); pc += 3
            elif op == 0xBD:
                self.a = self.mem[ab + self.x]; self.flags(self.a); pc += 3
            elif op == 0xAE:
                self.x = self.mem[ab]; self.flags(self.x); pc += 3
            elif op == 0x8D:
                self.mem[ab] = self.a; pc += 3
            elif op == 0x8E:
                self.mem[ab] = self.x; pc += 3
            elif op == 0x09:
                self.a |= imm; self.flags(self.a); pc += 2
            elif op == 0x0D:
                self.a |= self.mem[ab]; self.flags(self.a); pc += 3
            elif op == 0x2D:
                self.a &= self.mem[ab]; self.flags(self.a); pc += 3
            elif op == 0xC9:
                self.compare(self.a, imm); pc += 2
            elif op == 0xCD:
                self.compare(self.a, self.mem[ab]); pc += 3
            elif op == 0xE0:
                self.compare(self.x, imm); pc += 2
            elif op == 0xE8:
                self.x = (self.x + 1) & 0xFF; self.flags(self.x); pc += 1
            elif op == 0xEE:
                self.mem[ab] = (self.mem[ab] + 1) & 0xFF; self.flags(self.mem[ab]); pc += 3
            elif op == 0xCE:
                self.mem[ab] = (self.mem[ab] - 1) & 0xFF; self.flags(self.mem[ab]); pc += 3
            elif op == 0x98:
                self.a = self.y; self.flags(self.a); pc += 1
            elif op == 0x18:
                self.c = False; pc += 1
            elif op == 0x38:
                self.c = True; pc += 1
            elif op in (0xF0, 0xD0, 0x30, 0x10, 0x90, 0xB0):
                offset = imm - 256 if imm > 127 else imm
                taken = {0xF0: self.z, 0xD0: not self.z, 0x30: self.n, 0x10: not self.n,
                         0x90: not self.c, 0xB0: self.c}[op]
                pc += 2 + (offset if taken else 0)
            elif op == 0x4C:
                if ab == pc:
                    return "halt", pc
                pc = ab
            elif op == 0x6C:
                pc = self.word(ab)
            elif op == 0x20 and ab == make_menu.SIOV:
                self.siov(); pc += 3
            elif op == 0x20:
                ret = pc + 2
                self.push(ret >> 8); self.push(ret & 0xFF)
                pc = ab
            elif op == 0x60:
                pc = self.rts()
            else:
                raise AssertionError(f"opcode {op:02x} at {pc:04x} not expected in the loader")
        raise AssertionError("loader did not finish")


def ran_at(address):
    return {address: lambda cpu: "run"}


class XexLoaderTest(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.block, cls.labels = loader.loader_block()

    def test_boot_record_loads_the_loader_at_0700(self):
        self.assertEqual(self.block[0], 0x00)
        self.assertEqual(self.block[1] * SECTOR, len(self.block))
        self.assertEqual(self.block[2] | (self.block[3] << 8), loader.LOADER_ORG)
        self.assertEqual(loader.LOADER_ORG, 0x0700)
        self.assertLessEqual(self.block[1], 3, "the loader fits in the three boot sectors")

    def test_forbidden_zone_covers_the_loader_and_its_buffer(self):
        self.assertEqual(loader.loader_low(), 0x0700)
        self.assertGreaterEqual(loader.loader_high(), loader.LOADER_ORG + len(self.block) - 1)
        self.assertEqual(loader.loader_high(), self.labels["buffer"] + SECTOR - 1)

    def test_segments_are_copied_to_their_addresses_and_runad_runs(self):
        first = bytes(range(1, 201))  # 200 bytes: crosses sector boundaries
        xex = (segment(0x3000, first) + segment(0x5000, b"\xAA\xBB\xCC", header=False)
               + segment(0x6000, b"\x11", header=True) + word_segment(RUNAD, 0x3000))
        cpu = Cpu(virtual_disk(xex), hooks=ran_at(0x3000))
        self.assertEqual(cpu.run(), ("run", 0x3000))
        self.assertEqual(bytes(cpu.mem[0x3000:0x3000 + 200]), first)
        self.assertEqual(bytes(cpu.mem[0x5000:0x5003]), b"\xAA\xBB\xCC")
        self.assertEqual(cpu.mem[0x6000], 0x11)
        self.assertEqual(cpu.mem[0x2FFF], 0)
        self.assertEqual(cpu.mem[0x30C8], 0)

    def test_initad_runs_after_its_segment_and_before_the_next(self):
        calls = []

        def init(cpu):
            calls.append((cpu.mem[0x4000], cpu.mem[0x5000]))

        xex = (segment(0x4000, b"\x60") + word_segment(INITAD, 0x4000)
               + segment(0x5000, b"\x77") + word_segment(RUNAD, 0x5000))
        hooks = {0x4000: init, 0x5000: lambda cpu: "run"}
        self.assertEqual(Cpu(virtual_disk(xex), hooks=hooks).run(), ("run", 0x5000))
        self.assertEqual(calls, [(0x60, 0x00)], "once, before the next segment is loaded")

    def test_each_initad_runs_once_in_order(self):
        calls = []
        hooks = {0x4000: lambda cpu: calls.append("a"), 0x4100: lambda cpu: calls.append("b"),
                 0x5000: lambda cpu: "run"}
        xex = (word_segment(INITAD, 0x4000, header=True) + segment(0x6000, b"\x01")
               + word_segment(INITAD, 0x4100) + word_segment(RUNAD, 0x5000))
        self.assertEqual(Cpu(virtual_disk(xex), hooks=hooks).run(), ("run", 0x5000))
        self.assertEqual(calls, ["a", "b"])

    def test_basic_is_off_before_the_first_read(self):
        xex = segment(0x3000, b"\x01") + word_segment(RUNAD, 0x3000)
        cpu = Cpu(virtual_disk(xex), hooks=ran_at(0x3000))
        cpu.run()
        self.assertEqual(cpu.portb_at_first_read, PORTB_BASIC_ON | 0x02, "bit 1 set, other bits kept")
        self.assertEqual(cpu.mem[loader.PORTB], PORTB_BASIC_ON | 0x02)

    def test_reads_the_data_sectors_in_order_after_the_loader(self):
        xex = segment(0x3000, bytes(300)) + word_segment(RUNAD, 0x3000)
        cpu = Cpu(virtual_disk(xex), hooks=ran_at(0x3000))
        cpu.run()
        first = self.block[1] + 1
        self.assertEqual(cpu.reads, list(range(first, first + -(-len(xex) // SECTOR))))

    def test_a_read_error_shows_the_message_and_stops(self):
        xex = segment(0x3000, bytes(300)) + word_segment(RUNAD, 0x3000)
        cpu = Cpu(virtual_disk(xex), fail_sector=self.block[1] + 2, hooks=ran_at(0x3000))
        result, pc = cpu.run()
        self.assertEqual(result, "halt")
        dl = cpu.word(make_menu.SDLSTL)
        self.assertEqual(dl, self.labels["error_dl"])
        lms = cpu.word(self.labels["error_lms"])
        text = make_menu.screen_codes(loader.READ_ERROR_TEXT)
        self.assertEqual(len(text), 40, "a whole mode 2 line, padded with spaces")
        self.assertEqual(bytes(cpu.mem[lms:lms + len(text)]), text)
        self.assertEqual(bytes(cpu.mem[dl:dl + 4]), b"\x70\x70\x70\x42", "24 blank lines, mode 2 with LMS")
        self.assertEqual(cpu.mem[dl + 6], 0x41, "JVB")
        self.assertEqual(cpu.word(dl + 7), dl)

    def test_a_header_split_across_sectors_is_read(self):
        first = segment(0x3000, bytes(range(121)))  # 127 bytes: the next $FFFF straddles sectors
        self.assertEqual(len(first), 127)
        xex = first + segment(0x5000, b"\x42") + word_segment(RUNAD, 0x3000)
        cpu = Cpu(virtual_disk(xex), hooks=ran_at(0x3000))
        self.assertEqual(cpu.run(), ("run", 0x3000))
        self.assertEqual(cpu.mem[0x5000], 0x42)

    def test_a_segment_ending_at_ffff_stops_there(self):
        xex = segment(0xFFFE, b"\x12\x34") + segment(0x3000, b"\x01") + word_segment(RUNAD, 0x3000)
        cpu = Cpu(virtual_disk(xex), hooks=ran_at(0x3000))
        self.assertEqual(cpu.run(), ("run", 0x3000))
        self.assertEqual(bytes(cpu.mem[0xFFFE:0x10000]), b"\x12\x34")
        self.assertEqual(cpu.mem[0x0000], 0, "no wrap to page zero")
        self.assertEqual(cpu.mem[0x3000], 0x01)

    def test_without_runad_the_loader_stops(self):
        cpu = Cpu(virtual_disk(segment(0x3000, b"\x01")))
        self.assertEqual(cpu.run()[0], "halt")
        self.assertEqual(cpu.mem[0x3000], 0x01)

    def test_length_above_64k_is_counted_with_three_bytes(self):
        data = bytes(range(256)) * 260  # 66,560 bytes in one segment
        xex = segment(0x1000, data[:0x8000]) + segment(0x9000, data[0x8000:0x8000 + 0x2000]) \
            + segment(0x1000, data[:0x6000]) + word_segment(RUNAD, 0x1000)
        self.assertGreater(len(xex), 0x10000)
        cpu = Cpu(virtual_disk(xex), hooks=ran_at(0x1000))
        self.assertEqual(cpu.run(max_steps=20_000_000), ("run", 0x1000))
        self.assertEqual(bytes(cpu.mem[0x9000:0xB000]), data[0x8000:0xA000])

    def test_include_is_up_to_date(self):
        result = subprocess.run([sys.executable, "-I", str(TOOLS / "make_xex_loader.py"), "--check",
                                 str(REPO / "host" / "src" / "xex_loader.inc")], capture_output=True, text=True)
        self.assertEqual(result.returncode, 0, result.stderr)


if __name__ == "__main__":
    unittest.main()
