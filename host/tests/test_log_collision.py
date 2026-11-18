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


class HostCase(unittest.TestCase):
    """A temporary folder with images/disk.atr; helpers to run the host there."""

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


class LogCollisionTest(HostCase):
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


class ImagesFolderTest(HostCase):
    """--images (Issue #38): the same protection for every image of the folder,
    and clear refusals for folders the menu cannot serve."""

    def run_folder(self, folder_arg, log_arg):
        return subprocess.run(
            [EXE, "--images", folder_arg, "--log", log_arg, "--port", "9", "--seconds", "1"],
            cwd=self.root, capture_output=True, text=True, timeout=60)

    def test_log_naming_an_image_of_the_folder_is_refused(self):
        (self.root / "images" / "a-first.atr").write_bytes(ATR)
        before = self.entries()
        result = self.run_folder("images", "images\\DISK.ATR" if os.name == "nt" else "images/disk.atr")
        self.assertEqual(result.returncode, 2, result.stderr)
        self.assertIn("refusing to write into the image", result.stderr)
        self.assert_image_intact()
        self.assertEqual(self.entries(), before, "a file was created")

    def make_21_images(self):
        """images/ with 21 ATRs: disk.atr plus img00..img19. In menu order disk.atr is
        the first entry and img19.atr the 21st, outside the 20 the menu shows."""
        for n in range(20):
            (self.root / "images" / f"img{n:02d}.atr").write_bytes(ATR)
        outside = self.root / "images" / "img19.atr"
        return outside, outside.stat().st_mtime_ns

    def test_log_naming_the_atr_beyond_the_menu_limit_is_refused(self):
        outside, mtime = self.make_21_images()
        before = self.entries()
        result = self.run_folder("images", "images/img19.atr")
        self.assertEqual(result.returncode, 2, result.stderr)
        self.assertIn("refusing to write into the image", result.stderr)
        self.assertIn("img19.atr", result.stderr)
        self.assertEqual(outside.read_bytes(), ATR, "the ATR beyond the limit changed")
        self.assertEqual(outside.stat().st_mtime_ns, mtime, "the ATR beyond the limit was written")
        self.assert_image_intact()
        self.assertEqual(self.entries(), before, "a file was created")

    @unittest.skipUnless(os.name == "nt", "8.3 aliases and ANSI listings are Windows behaviour")
    def test_log_as_8dot3_alias_of_a_non_ansi_atr_is_refused(self):
        # A name outside the ANSI code page comes back from FindFirstFileA with '?',
        # so it cannot be opened by that name; the check must still see it.
        import ctypes
        target = self.root / "images" / "juego Ω largo nombre.atr"
        target.write_bytes(ATR)
        mtime = target.stat().st_mtime_ns
        buffer = ctypes.create_unicode_buffer(1024)
        if ctypes.windll.kernel32.GetShortPathNameW(str(target), buffer, 1024) == 0:
            self.skipTest("no short name available")
        alias = Path(buffer.value).name
        if alias == target.name:
            self.skipTest("8.3 names are disabled on this volume")
        before = self.entries()
        result = self.run_folder("images", "images/" + alias)
        self.assertEqual(result.returncode, 2, result.stderr)
        # The match path, not the "cannot check" refusal, and the long (wide) name.
        self.assertIn("--log names the image file", result.stderr)
        self.assertIn("largo nombre.atr", result.stderr)
        self.assertEqual(target.read_bytes(), ATR, "the non-ANSI ATR changed")
        self.assertEqual(target.stat().st_mtime_ns, mtime, "the non-ANSI ATR was written")
        self.assertEqual(self.entries(), before, "a file was created")

    def test_the_menu_still_shows_twenty_of_twenty_one(self):
        self.make_21_images()
        result = self.run_folder("images", "run.jsonl")
        self.assertNotEqual(result.returncode, 2, result.stderr)
        log = (self.root / "run.jsonl").read_text(encoding="utf-8")
        entries = [line for line in log.splitlines() if '"name":"menu_entry"' in line]
        self.assertEqual(len(entries), 20)
        self.assertIn("entries=20 atr_seen=21", log)
        self.assertIn('"detail":"T img18.atr"', log)
        self.assertNotIn("img19.atr", log)

    # --sources (Issue #40): sources 2..8 of the main screen.

    def run_sources(self, sources_arg, log_arg):
        return subprocess.run(
            [EXE, "--images", "images", "--sources", sources_arg, "--log", log_arg, "--port", "9", "--seconds", "1"],
            cwd=self.root, capture_output=True, text=True, timeout=60)

    def test_sources_are_listed_and_eight_drives_served(self):
        (self.root / "sources.txt").write_text("# servers\ntnfs.example.cl\n\nlocalhost\n")
        result = self.run_sources("sources.txt", "run.jsonl")
        self.assertNotEqual(result.returncode, 2, result.stderr)
        log = (self.root / "run.jsonl").read_text(encoding="utf-8")
        self.assertIn("sources=2 drives=8", log)
        self.assertIn('"name":"menu_source","detail":"2 tnfs.example.cl"', log)
        self.assertIn('"name":"menu_source","detail":"3 localhost"', log)

    def test_log_naming_the_sources_file_is_refused(self):
        sources = self.root / "sources.txt"
        sources.write_text("localhost\n")
        before, mtime = sources.read_bytes(), sources.stat().st_mtime_ns
        result = self.run_sources("sources.txt", "SOURCES.TXT" if os.name == "nt" else "sources.txt")
        self.assertEqual(result.returncode, 2, result.stderr)
        self.assertIn("--log names the sources file", result.stderr)
        self.assertEqual(sources.read_bytes(), before)
        self.assertEqual(sources.stat().st_mtime_ns, mtime)

    def test_bad_sources_file_is_refused_before_the_log_opens(self):
        (self.root / "sources.txt").write_text("".join(f"s{n}\n" for n in range(8)))
        result = self.run_sources("sources.txt", "run.jsonl")
        self.assertEqual(result.returncode, 2, result.stderr)
        self.assertIn("more than 7 sources", result.stderr)
        self.assertFalse((self.root / "run.jsonl").exists())
        result = self.run_sources("missing.txt", "run.jsonl")
        self.assertEqual(result.returncode, 2, result.stderr)
        self.assertIn("cannot open sources file", result.stderr)

    def test_sources_without_images_is_a_usage_error(self):
        (self.root / "sources.txt").write_text("localhost\n")
        result = subprocess.run([EXE, "--image", "images/disk.atr", "--sources", "sources.txt"],
                                cwd=self.root, capture_output=True, text=True, timeout=60)
        self.assertEqual(result.returncode, 2)
        self.assertIn("usage", result.stderr)

    def test_folder_without_atr_files_is_refused(self):
        (self.root / "empty").mkdir()
        (self.root / "empty" / "notes.txt").write_text("x")
        result = self.run_folder("empty", "run.jsonl")
        self.assertEqual(result.returncode, 2, result.stderr)
        self.assertIn("no .atr images", result.stderr)
        self.assertFalse((self.root / "run.jsonl").exists())

    def test_missing_folder_is_refused(self):
        result = self.run_folder("nowhere", "run.jsonl")
        self.assertEqual(result.returncode, 2, result.stderr)
        self.assertIn("cannot read images folder", result.stderr)

    def test_image_and_images_together_are_refused(self):
        result = subprocess.run([EXE, "--image", "images/disk.atr", "--images", "images"],
                                cwd=self.root, capture_output=True, text=True, timeout=60)
        self.assertEqual(result.returncode, 2)
        self.assertIn("usage", result.stderr)

    def test_other_log_lists_the_catalog_and_leaves_images_untouched(self):
        (self.root / "images" / "B.ATR").write_bytes(ATR)
        result = self.run_folder("images", "run.jsonl")
        self.assertNotEqual(result.returncode, 2, result.stderr)
        self.assert_image_intact()
        log = (self.root / "run.jsonl").read_text(encoding="utf-8")
        self.assertIn('"name":"menu_entry","detail":"A B.ATR"', log)
        self.assertIn('"name":"menu_entry","detail":"B disk.atr"', log)
        # Nobody answers on port 9, so no step runs: the menu is requested, not yet applied.
        self.assertIn('"name":"menu_return","detail":"cold reset: menu requested for D1"', log)


if __name__ == "__main__":
    if len(sys.argv) < 2:
        sys.exit(__doc__)
    EXE = os.path.abspath(sys.argv.pop(1))
    unittest.main(verbosity=2)
