"""Self-tests for tools/make_atr.py and tools/compare_crc.py.

Run with: python -m unittest discover -s tools/tests
"""

import csv
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
