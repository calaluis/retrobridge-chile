"""Regression tests for the firmware packaging jobs in .github/workflows/ci.yml.

The H3c package (validated on the board) and the H4 diag package must keep
separate, fixed identities: pinned commit, artifact name and packager variant.
Plain-text parsing (no YAML dependency), job by job.

Run with: python -m unittest discover -s tools/tests
"""

import re
import sys
import unittest
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent.parent / "ci"))

import firmware_package as fp  # noqa: E402

WORKFLOW = Path(__file__).resolve().parents[2] / ".github" / "workflows" / "ci.yml"
H3C_COMMIT = "1b1527d2dfd29a5f66471cbb2a14a3d49585dd8e"
H4_COMMIT = "15b153f267cf6766978bd786eb1b463ea0e4ac50"


def jobs(text: str) -> dict:
    """{job id: job text} for the top-level jobs (two-space indentation under `jobs:`)."""
    body = text.split("\njobs:\n", 1)[1]
    parts = re.split(r"^  ([A-Za-z0-9_-]+):\s*$", body, flags=re.MULTILINE)
    return {parts[i]: parts[i + 1] for i in range(1, len(parts), 2)}


def field(job: str, pattern: str) -> str:
    match = re.search(pattern, job, flags=re.MULTILINE)
    return match.group(1) if match else ""


class FirmwareJobsTest(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.jobs = jobs(WORKFLOW.read_text(encoding="utf-8"))
        cls.h3c = cls.jobs["firmware-package"]
        cls.h4 = cls.jobs["firmware-package-h4"]

    def test_h3c_job_keeps_its_commit_name_and_default_variant(self):
        self.assertEqual(field(self.h3c, r"^\s+FW_COMMIT:\s*(\S+)"), H3C_COMMIT)
        self.assertEqual(field(self.h3c, r"^\s+name:\s*(retrobridge-fw-\S+)"), "retrobridge-fw-fujinet-atari-v1x-1b1527d")
        self.assertNotIn("--variant", self.h3c)  # default variant h3c: original provenance and checks

    def test_h4_job_pins_the_diag_commit_name_and_variant(self):
        self.assertEqual(field(self.h4, r"^\s+FW_COMMIT:\s*(\S+)"), H4_COMMIT)
        self.assertEqual(field(self.h4, r"^\s+name:\s*(retrobridge-fw-\S+)"),
                         "retrobridge-fw-diag-fujinet-atari-v1x-15b153f")
        self.assertIn("package --variant diag", self.h4)
        self.assertIn("firmware_package.py verify", self.h4)  # the package is re-verified in CI

    def test_identities_are_distinct(self):
        self.assertNotEqual(H3C_COMMIT, H4_COMMIT)
        names = {field(j, r"^\s+name:\s*(retrobridge-fw-\S+)") for j in (self.h3c, self.h4)}
        self.assertEqual(len(names), 2)
        self.assertNotEqual(fp.VARIANTS["h3c"].package, fp.VARIANTS["diag"].package)

    def test_artifact_names_match_packager_identity_and_commit(self):
        for job, variant, commit in ((self.h3c, "h3c", H3C_COMMIT), (self.h4, "diag", H4_COMMIT)):
            name = field(job, r"^\s+name:\s*(retrobridge-fw-\S+)")
            self.assertEqual(name, f"{fp.VARIANTS[variant].package}-{commit[:7]}")

    def test_both_jobs_run_only_on_pull_requests_and_manual_runs(self):
        for job in (self.h3c, self.h4):
            condition = field(job, r"^\s+if:\s*(.+)$")
            self.assertIn("pull_request", condition)
            self.assertIn("workflow_dispatch", condition)
            self.assertNotIn("push", condition)

    def test_no_arbitrary_commit_inputs(self):
        text = WORKFLOW.read_text(encoding="utf-8")
        self.assertIsNone(re.search(r"^\s+inputs:", text, flags=re.MULTILINE))  # no workflow_dispatch inputs key
        self.assertNotIn("inputs.", self.h3c + self.h4)

    def test_checkouts_do_not_persist_credentials(self):
        for job in (self.h3c, self.h4):
            self.assertEqual(job.count("persist-credentials: false"), 2)


if __name__ == "__main__":
    unittest.main()
