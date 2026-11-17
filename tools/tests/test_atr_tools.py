"""Self-tests for tools/make_atr.py and tools/compare_crc.py.

Run with: python -m unittest discover -s tools/tests
"""

import csv
import json
import sys
import tempfile
import unittest
import zlib
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent.parent))

import compare_crc  # noqa: E402
import make_atr  # noqa: E402


class MakeAtrTest(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls._tmp = tempfile.TemporaryDirectory()
        cls.out = Path(cls._tmp.name)
        make_atr.generate(cls.out, seed=0)
        with (cls.out / "images.csv").open(newline="") as f:
            cls.images = {row["image"]: row for row in csv.DictReader(f)}
        with (cls.out / "manifest.csv").open(newline="") as f:
            cls.manifest = list(csv.DictReader(f))

    @classmethod
    def tearDownClass(cls):
        cls._tmp.cleanup()

    def test_standard_sizes(self):
        self.assertEqual((self.out / "sd-720.atr").stat().st_size, 16 + 720 * 128)
        self.assertEqual((self.out / "ed-1040.atr").stat().st_size, 16 + 1040 * 128)
        self.assertEqual((self.out / "dd-720.atr").stat().st_size, 16 + 3 * 128 + 717 * 256)

    def test_header_fields(self):
        data = (self.out / "dd-720.atr").read_bytes()
        self.assertEqual(data[0:2], b"\x96\x02")
        paragraphs = data[2] | (data[3] << 8) | (data[6] << 16)
        self.assertEqual(paragraphs * 16, len(data) - 16)
        self.assertEqual(data[4] | (data[5] << 8), 256)

    def test_trailing_image_declares_the_nominal_size(self):
        data = (self.out / "sd-trailing.atr").read_bytes()
        paragraphs = data[2] | (data[3] << 8) | (data[6] << 16)
        self.assertEqual(paragraphs * 16, 720 * 128)
        self.assertEqual(len(data), 16 + 720 * 128 + 50)
        self.assertEqual(self.images["sd-trailing"]["header_mismatch"], "1")

    def test_manifest_matches_file_contents(self):
        files = {name: (self.out / f"{name}.atr").read_bytes() for name in self.images}
        for row in self.manifest:
            offset, length = int(row["offset"]), int(row["length"])
            chunk = files[row["image"]][offset:offset + length]
            self.assertEqual(len(chunk), length)
            self.assertEqual(f"{zlib.crc32(chunk) & 0xFFFFFFFF:08x}", row["crc32"])

    def test_sectors_embed_their_number(self):
        data = (self.out / "dd-1440.atr").read_bytes()
        row = next(r for r in self.manifest if r["image"] == "dd-1440" and r["sector"] == "1440")
        offset = int(row["offset"])
        self.assertEqual(data[offset] | (data[offset + 1] << 8), 1440)

    def test_generation_is_deterministic(self):
        with tempfile.TemporaryDirectory() as other:
            make_atr.generate(Path(other), seed=0)
            self.assertEqual((Path(other) / "manifest.csv").read_bytes(), (self.out / "manifest.csv").read_bytes())


def run_boot(memory: bytearray, max_steps: int = 100000) -> int:
    """Executes the rb-boot program from BOOT_ORG + 6 until it reaches its final
    JMP-to-self loop; returns that address. Interprets only the instructions the
    program uses; anything else fails the test. RTCLOK advances on every read so
    the vertical-blank wait ends."""
    a = x = 0
    z = False
    pc = make_atr.BOOT_ORG + 6

    def word(at):
        return memory[at] | (memory[at + 1] << 8)

    def read(at):
        if at == make_atr.RTCLOK_LOW:
            memory[at] = (memory[at] + 1) & 0xFF
        return memory[at]

    for _ in range(max_steps):
        op = memory[pc]
        if op == 0xA2:                       # LDX #
            x = memory[pc + 1]; z = x == 0; pc += 2
        elif op == 0x8A:                     # TXA
            a = x; z = a == 0; pc += 1
        elif op == 0x5D:                     # EOR abs,X
            a ^= memory[word(pc + 1) + x]; z = a == 0; pc += 3
        elif op == 0xE8:                     # INX
            x = (x + 1) & 0xFF; z = x == 0; pc += 1
        elif op in (0xD0, 0xF0):             # BNE / BEQ
            offset = memory[pc + 1] - 256 if memory[pc + 1] > 127 else memory[pc + 1]
            pc += 2
            if z == (op == 0xF0):
                pc += offset
        elif op == 0xC9:                     # CMP #
            z = a == memory[pc + 1]; pc += 2
        elif op == 0xA9:                     # LDA #
            a = memory[pc + 1]; z = a == 0; pc += 2
        elif op == 0x8D:                     # STA abs
            memory[word(pc + 1)] = a; pc += 3
        elif op == 0xA5:                     # LDA zp
            a = read(memory[pc + 1]); z = a == 0; pc += 2
        elif op == 0xC5:                     # CMP zp
            z = a == read(memory[pc + 1]); pc += 2
        elif op == 0x4C:                     # JMP abs
            target = word(pc + 1)
            if target == pc:
                return pc
            pc = target
        else:
            raise AssertionError(f"opcode {op:02x} at {pc:04x} not expected in the boot program")
    raise AssertionError("boot program did not reach its loop")


class BootImageTest(unittest.TestCase):
    """rb-boot (E1c): boot record and the 6502 program in sector 1."""

    @classmethod
    def setUpClass(cls):
        cls.atr, cls.rows, _ = make_atr.build_image(make_atr.BOOT_IMAGE, 720, 128, 0, 0)

    def sector(self, n):
        start = make_atr.HEADER_SIZE + (n - 1) * 128
        return self.atr[start:start + 128]

    def boot(self, corrupt_at=None):
        """Loads sectors 1-3 as the OS does, runs the program; returns the text shown."""
        memory = bytearray(0x10000)
        loaded = bytearray(b"".join(self.sector(n) for n in range(1, make_atr.BOOT_LOAD_SECTORS + 1)))
        if corrupt_at is not None:
            loaded[corrupt_at] ^= 0x01
        org = make_atr.BOOT_ORG
        memory[org:org + len(loaded)] = loaded
        run_boot(memory)
        dl = memory[make_atr.SDLSTL] | (memory[make_atr.SDLSTL + 1] << 8)
        # Display list: blank lines (0x70), then mode 7 with LMS (0x47 lo hi), then JVB to itself.
        at = dl
        while memory[at] == 0x70:
            at += 1
        self.assertEqual(memory[at], 0x47)
        text = memory[at + 1] | (memory[at + 2] << 8)
        self.assertEqual(memory[at + 3], 0x41)
        self.assertEqual(memory[at + 4] | (memory[at + 5] << 8), dl)
        codes = memory[text:text + make_atr.BOOT_TEXT_WIDTH]
        return "".join(chr(c + 0x20) for c in codes).strip()

    def test_boot_record(self):
        s1 = self.sector(1)
        self.assertEqual(s1[0], 0x00)
        self.assertEqual(s1[1], make_atr.BOOT_LOAD_SECTORS)
        self.assertEqual(s1[2] | (s1[3] << 8), make_atr.BOOT_ORG)
        init = s1[4] | (s1[5] << 8)
        self.assertEqual(s1[init - make_atr.BOOT_ORG], 0x60, "DOSINI must point at an RTS")

    def test_intact_sectors_show_ok(self):
        self.assertEqual(self.boot(), make_atr.BOOT_OK_TEXT)

    def test_a_changed_byte_in_sector_2_or_3_shows_err(self):
        self.assertEqual(self.boot(corrupt_at=128), make_atr.BOOT_ERR_TEXT)
        self.assertEqual(self.boot(corrupt_at=383), make_atr.BOOT_ERR_TEXT)

    def test_manifest_covers_the_boot_sector(self):
        row = next(r for r in self.rows if r[1] == 1)
        self.assertEqual(row[4], zlib.crc32(self.sector(1)) & 0xFFFFFFFF)

    def test_meta_a_set_is_unchanged_and_boot_set_is_separate(self):
        # test_meta_a (H3) and the firmware diagnostics expect exactly these five images.
        self.assertEqual([i[0] for i in make_atr.IMAGES], ["sd-720", "ed-1040", "dd-720", "dd-1440", "sd-trailing"])
        with tempfile.TemporaryDirectory() as out:
            make_atr.main(["--out", out, "--boot"])
            self.assertEqual(sorted(p.name for p in Path(out).glob("*.atr")), ["rb-boot.atr"])
            with (Path(out) / "manifest.csv").open(newline="") as f:
                rows = list(csv.DictReader(f))
            self.assertEqual({r["image"] for r in rows}, {"rb-boot"})
            self.assertEqual(len(rows), 720)
            self.assertEqual((Path(out) / "rb-boot.atr").read_bytes(), self.atr)

    def test_other_sectors_keep_the_standard_content(self):
        s720 = self.sector(720)
        self.assertEqual(s720[0] | (s720[1] << 8), 720)


class DemoImagesTest(BootImageTest):
    """--demo (Issue #38): two bootable images with different messages and one invalid ATR."""

    @classmethod
    def setUpClass(cls):
        cls.atr, cls.rows, _ = make_atr.build_image("demo-uno", 720, 128, 0, 0)

    def test_intact_sectors_show_ok(self):
        self.assertEqual(self.boot(), "DEMO UNO OK")

    def test_the_two_bootable_images_show_different_messages(self):
        dos = DemoImagesTest("test_boot_record")
        dos.atr = make_atr.build_image("demo-dos", 720, 128, 0, 0)[0]
        self.assertEqual(dos.boot(), "DEMO DOS OK")
        self.assertNotEqual(self.boot(), dos.boot())

    def test_rota_is_not_an_atr(self):
        rota = make_atr.build_image("rota", 720, 128, 0, 0)[0]
        self.assertEqual(rota[0:2], bytes(2))
        self.assertNotEqual(rota[0:2], bytes([0x96, 0x02]))

    def test_demo_set_is_separate(self):
        with tempfile.TemporaryDirectory() as out:
            make_atr.main(["--out", out, "--demo"])
            self.assertEqual(sorted(p.name for p in Path(out).glob("*.atr")),
                             ["demo-dos.atr", "demo-uno.atr", "rota.atr"])

    # The Meta A check belongs to rb-boot only.
    test_meta_a_set_is_unchanged_and_boot_set_is_separate = None


def host_log(*datagrams, image="rb-boot.atr"):
    """A retrobridge_host JSONL with a start event and the given (dir, hex) datagrams."""
    lines = [json.dumps({"kind": "event", "name": "start", "detail": f"image={image} peer=netsiohub"})]
    for direction, hex_text in datagrams:
        lines.append(json.dumps({"kind": "datagram", "dir": direction, "type": "X", "hex": hex_text}))
    return lines


def read_frame(sector: int, trailer: str = " 07") -> str:
    frame = bytes([0x31, 0x52, sector & 0xFF, sector >> 8])
    return "02 " + frame.hex(" ") + f" {compare_crc.sio_checksum(frame):02x}" + trailer


def complete(data: bytes, checksum=None) -> str:
    checksum = compare_crc.sio_checksum(data) if checksum is None else checksum
    return "02 43 " + data.hex(" ") + f" {checksum:02x}"


class CompareHostJsonlTest(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.atr, rows, _ = make_atr.build_image(make_atr.BOOT_IMAGE, 720, 128, 0, 0)
        cls.expected = {make_atr.BOOT_IMAGE: {r[1]: r[4] for r in rows}}

    def sector(self, n):
        start = make_atr.HEADER_SIZE + (n - 1) * 128
        return self.atr[start:start + 128]

    def check(self, lines):
        self.assertTrue(compare_crc.is_jsonl(lines))
        reads, problems = compare_crc.parse_host_jsonl(lines)
        return reads, problems + compare_crc.compare_reads(self.expected, reads)

    def test_reads_that_match_pass(self):
        lines = host_log(("in", "02 31 53 00 00 84 05"), ("out", "81 01 01 41 00 00"),
                         ("in", read_frame(1)), ("out", "81 02 01 41 00 00"), ("out", complete(self.sector(1))),
                         ("in", read_frame(2)), ("out", "c6 00"), ("in", "c7 03 09"), ("out", complete(self.sector(2))))
        reads, problems = self.check(lines)
        self.assertEqual(problems, [])
        self.assertEqual([s for _, s, _ in reads], [1, 2])

    def test_each_run_of_an_appended_log_uses_its_own_image(self):
        other = {"other": {1: 0x12345678}}
        lines = (host_log(("in", read_frame(1)), ("out", complete(b"\x00" * 128)), image="other.atr")
                 + host_log(("in", read_frame(1)), ("out", complete(self.sector(1)))))
        reads, problems = compare_crc.parse_host_jsonl(lines)
        problems += compare_crc.compare_reads({**self.expected, **other}, reads)
        self.assertEqual([i for i, _, _ in reads], ["other", make_atr.BOOT_IMAGE])
        zeros = zlib.crc32(b"\x00" * 128) & 0xFFFFFFFF
        self.assertEqual(problems, [f"other: sector 1 crc {zeros:08x}, expected 12345678"])

    def test_malformed_lines_are_problems_not_crashes(self):
        lines = host_log(("in", read_frame(1)), ("out", complete(self.sector(1)))) + ["[]", "5",
                         json.dumps({"kind": "datagram", "dir": "in", "hex": "0"})]
        _, problems = self.check(lines)
        self.assertEqual(len(problems), 3)

    def test_a_changed_byte_fails(self):
        data = bytearray(self.sector(3))
        data[10] ^= 0xFF
        reads, problems = self.check(host_log(("in", read_frame(3)), ("out", complete(bytes(data)))))
        self.assertEqual(len(problems), 1)
        self.assertIn("sector 3 crc", problems[0])

    def test_bad_sio_checksum_and_error_status_fail(self):
        bad_sum = complete(self.sector(1), checksum=0x00)
        error = "02 45 " + self.sector(1).hex(" ") + " 00"
        _, problems = self.check(host_log(("in", read_frame(1)), ("out", bad_sum),
                                          ("in", read_frame(1)), ("out", error)))
        self.assertTrue(any("SIO checksum" in p for p in problems))
        self.assertTrue(any("not COMPLETE" in p for p in problems))

    def test_no_read_or_unknown_image_fails(self):
        self.assertTrue(self.check(host_log(("in", "02 31 53 00 00 84 05")))[1])
        _, problems = self.check(host_log(("in", read_frame(1)), ("out", complete(self.sector(1))), image="x.atr"))
        self.assertEqual(problems, ["x: not in the manifest"])

    def test_strict_profile_frames_without_trailer_are_read(self):
        reads, problems = self.check(host_log(("in", read_frame(1, trailer="")), ("out", complete(self.sector(1)))))
        self.assertEqual((problems, [s for _, s, _ in reads]), ([], [1]))


class CompareCrcTest(unittest.TestCase):
    expected = {"img": {1: 0x11111111, 2: 0x22222222}}

    def test_matching_log_passes(self):
        log = ["boot noise", "IMAGE img", "CRC 1 11111111", "CRC 2 22222222", "done"]
        self.assertEqual(compare_crc.compare(self.expected, compare_crc.parse_log(log)), [])

    def test_mismatch_missing_and_unexpected_are_reported(self):
        log = ["IMAGE img", "CRC 1 11111112", "CRC 3 33333333"]
        problems = compare_crc.compare(self.expected, compare_crc.parse_log(log))
        self.assertEqual(len(problems), 3)

    def test_empty_log_fails(self):
        self.assertTrue(compare_crc.compare(self.expected, compare_crc.parse_log(["nothing"])))

    def test_unknown_image_fails(self):
        problems = compare_crc.compare(self.expected, compare_crc.parse_log(["IMAGE other", "CRC 1 00000000"]))
        self.assertEqual(problems, ["other: not in the manifest"])


if __name__ == "__main__":
    unittest.main()
