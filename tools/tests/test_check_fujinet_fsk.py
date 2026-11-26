"""Self-tests for tools/check_fujinet_fsk.py: a clean tree passes, each kind of drift fails.

Run with: python -m unittest discover -s tools/tests
"""

import hashlib
import sys
import tempfile
import unittest
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent.parent))

import check_fujinet_fsk  # noqa: E402

COMPONENT = "components/rb_fujinet_fsk"
SAME = "int same();\n"
PATCHED_BEFORE = "a();\ntx_cfg.flags.io_loop_back = false;\ntx_cfg.flags.io_od_mode = false;\nb();\n"
PATCHED_AFTER = "a();\nb();\n"


def sha(text: str) -> str:
    return hashlib.sha256(text.encode("utf-8")).hexdigest()


class CheckFujinetFskTest(unittest.TestCase):
    def setUp(self):
        self._tmp = tempfile.TemporaryDirectory()
        self.root = Path(self._tmp.name)
        self.write("lib/media/atari/same.h", SAME)
        self.write("lib/device/sio/patched.cpp", PATCHED_AFTER)
        self.write("lib/include/debug.h", "// RetroBridge adapter\n")
        self.lines = [
            f"lib/media/atari/same.h lib/media/atari/same.h {sha(SAME)} {sha(SAME)} -",
            f"lib/device/sio/patched.cpp lib/device/sio/patched.cpp {sha(PATCHED_BEFORE)} {sha(PATCHED_AFTER)} "
            "idf61-rmt-flags",
        ]
        self.manifest()

    def tearDown(self):
        self._tmp.cleanup()

    def write(self, relative: str, text: str, crlf: bool = False) -> None:
        path = self.root / COMPONENT / relative
        path.parent.mkdir(parents=True, exist_ok=True)
        path.write_bytes(text.replace("\n", "\r\n" if crlf else "\n").encode("utf-8"))

    def manifest(self) -> None:
        self.write("fuente.sha256", "# header\n" + "\n".join(self.lines) + "\n")

    def test_recorded_tree_passes(self):
        self.assertEqual(check_fujinet_fsk.check_repo(self.root), [])

    def test_crlf_checkout_passes(self):
        self.write("lib/media/atari/same.h", SAME, crlf=True)
        self.assertEqual(check_fujinet_fsk.check_repo(self.root), [])

    def test_edited_owner_file_fails(self):
        self.write("lib/media/atari/same.h", "int changed();\n")
        self.assertEqual(len(check_fujinet_fsk.check_repo(self.root)), 1)

    def test_unrecorded_difference_with_the_commit_fails(self):
        self.lines[0] = f"lib/media/atari/same.h lib/media/atari/same.h {sha('other')} {sha(SAME)} -"
        self.manifest()
        self.assertEqual(len(check_fujinet_fsk.check_repo(self.root)), 1)

    def test_unknown_change_fails(self):
        self.lines[1] = self.lines[1].replace("idf61-rmt-flags", "my-tweak")
        self.manifest()
        self.assertEqual(len(check_fujinet_fsk.check_repo(self.root)), 1)

    def test_change_whose_removed_lines_are_back_fails(self):
        self.write("lib/device/sio/patched.cpp", PATCHED_BEFORE)
        self.lines[1] = (f"lib/device/sio/patched.cpp lib/device/sio/patched.cpp {sha(PATCHED_BEFORE)} "
                         f"{sha(PATCHED_BEFORE)} idf61-rmt-flags")
        self.manifest()
        self.assertEqual(len(check_fujinet_fsk.check_repo(self.root)), 2)

    def test_unlisted_file_in_lib_fails(self):
        self.write("lib/media/atari/extra.cpp", "int extra();\n")
        self.assertEqual(len(check_fujinet_fsk.check_repo(self.root)), 1)

    def test_missing_file_and_manifest_fail(self):
        (self.root / COMPONENT / "lib/media/atari/same.h").unlink()
        self.assertEqual(len(check_fujinet_fsk.check_repo(self.root)), 1)
        (self.root / COMPONENT / "fuente.sha256").unlink()
        self.assertEqual(len(check_fujinet_fsk.check_repo(self.root)), 1)

    def test_malformed_line_fails(self):
        self.lines.append("lib/x.h only-three fields")
        self.manifest()
        self.assertEqual(len(check_fujinet_fsk.check_repo(self.root)), 1)


if __name__ == "__main__":
    unittest.main()
