"""Regression: --log must never append to the image retrobridge_host serves.

The executable splits --image at '/' or '\\' and the repository opens
base + '/' + name. On POSIX, --image 'images\\disk.atr' is therefore served from
'images/disk.atr', a file whose name differs from the --image text. A --log
'images/disk.atr' must be refused with exit code 2, the ATR must stay byte for
byte intact, and nothing may be created next to it.

On Windows both separators already name the same file, so the backslash case
passes there with or without the fix; it is a regression test on POSIX. CI does
not build host/: run it with ctest on a POSIX machine.

Usage: python3 -I host/tests/test_log_collision.py <path to retrobridge_host>
"""

import importlib.util
import os
import subprocess
import sys
import tempfile
import unittest
from pathlib import Path

EXE = None
REPO = Path(__file__).resolve().parents[2]


def make_atr_bytes() -> bytes:
    spec = importlib.util.spec_from_file_location("make_atr", REPO / "tools" / "make_atr.py")
    module = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(module)
    data, _rows, _info = module.build_image("sd-720", 720, 128, 0, 0)
    return data


ATR = make_atr_bytes()


class LogCollisionTest(unittest.TestCase):
    def setUp(self):
        self._tmp = tempfile.TemporaryDirectory()
        self.root = Path(self._tmp.name)
        (self.root / "images").mkdir()
        self.image = self.root / "images" / "disk.atr"
        self.image.write_bytes(ATR)
        self.mtime = self.image.stat().st_mtime_ns

    def tearDown(self):
        self._tmp.cleanup()

    def run_host(self, image_arg, log_arg):
        # Port 9 (discard) on loopback: nobody answers, so a run that gets past
        # the checks stops at connect. --seconds 1 bounds it either way.
        return subprocess.run(
            [EXE, "--image", image_arg, "--log", log_arg, "--port", "9", "--seconds", "1"],
            cwd=self.root, capture_output=True, text=True, timeout=60)

    def entries(self):
        return sorted(str(p.relative_to(self.root)) for p in self.root.rglob("*"))

    def assert_image_intact(self):
        self.assertEqual(self.image.read_bytes(), ATR, "the ATR changed")
        self.assertEqual(self.image.stat().st_mtime_ns, self.mtime, "the ATR was written")

    def assert_refused(self, image_arg, log_arg):
        before = self.entries()
        result = self.run_host(image_arg, log_arg)
        self.assertEqual(result.returncode, 2, result.stderr)
        self.assertIn("refusing to write into the image", result.stderr)
        self.assert_image_intact()
        self.assertEqual(self.entries(), before, "a file was created")

    def test_backslash_image_and_slash_log_name_the_same_file(self):
        self.assert_refused("images\\disk.atr", "images/disk.atr")

    def test_same_spelling(self):
        self.assert_refused("images/disk.atr", "images/disk.atr")

    def test_other_log_is_written_and_image_untouched(self):
        result = self.run_host("images\\disk.atr", "run.jsonl")
        self.assertNotEqual(result.returncode, 2, result.stderr)
        self.assert_image_intact()
        log = (self.root / "run.jsonl").read_text(encoding="utf-8")
        self.assertIn('"name":"start"', log)


if __name__ == "__main__":
    if len(sys.argv) < 2:
        sys.exit(__doc__)
    EXE = os.path.abspath(sys.argv.pop(1))
    unittest.main(verbosity=2)
