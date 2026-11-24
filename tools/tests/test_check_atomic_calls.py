"""Self-tests for tools/ci/check_atomic_calls.py, on synthetic objdump listings.

Run with: python -m unittest discover -s tools/tests
"""

import sys
import unittest
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent.parent / "ci"))

import check_atomic_calls as cac  # noqa: E402

# Excerpts of the real listing from CI (run 45413310899, ESP-IDF 6.1, xtensa-esp32-elf, -Og).
PLAIN = """
00000000 <rb::host::DataInHandover::request(rb::host::HandoverRequest)>:
   0:	004136        	entry	a1, 32
  20:	208830        	or	a8, a8, a3
  23:	0020c0        	memw
  26:	0289      	s32i.n	a8, a2, 0
  2a:	0020c0        	memw
  2d:	f01d      	retw.n

00000000 <rb::host::DataInHandover::ackFor(unsigned long, rb::host::HandoverAck&) const>:
   0:	004136        	entry	a1, 32
   3:	0020c0        	memw
   6:	1288      	l32i.n	a8, a2, 4
   8:	0020c0        	memw
  2a:	f01d      	retw.n

00000000 <rb::host::DataInHandover::pending() const>:
   0:	006136        	entry	a1, 48
   3:	0020c0        	memw
   6:	0238      	l32i.n	a3, a2, 0
   8:	0020c0        	memw
  2b:	f01d      	retw.n

00000000 <rb::host::DataInHandover::confirm(unsigned long, rb::host::HandoverAck)>:
   0:	004136        	entry	a1, 32
   c:	0020c0        	memw
   f:	1239      	s32i.n	a3, a2, 4
  11:	0020c0        	memw
  14:	f01d      	retw.n
"""

LIBATOMIC = """
00000000 <rb_probe_load()>:
   0:	004136        	entry	a1, 32
   3:	000081        	l32r	a8, fffc0004 <rb_probe_load()+0xfffc0004>
			3: R_XTENSA_SLOT0_OP	.literal+0x4
   6:	0008e0        	callx8	a8
			6: R_XTENSA_ASM_EXPAND	__atomic_load_8
"""

CRITICAL = """
   9:	000081        	l32r	a8, 0 <foo>
			9: R_XTENSA_SLOT0_OP	vPortEnterCritical
"""


class LockCalls(unittest.TestCase):
    def test_plain_loads_and_stores_pass(self):
        self.assertEqual(cac.lock_calls(PLAIN), [])

    def test_a_libatomic_call_fails(self):
        calls = cac.lock_calls(LIBATOMIC)
        self.assertEqual(len(calls), 1)
        self.assertIn("__atomic_load_8", calls[0])

    def test_a_critical_section_fails(self):
        self.assertEqual(len(cac.lock_calls(CRITICAL)), 1)

    def test_a_symbol_that_only_contains_atomic_passes(self):
        # Our own names (std::atomic in a demangled signature) are not calls into libatomic.
        listing = "0 <rb::host::f(std::atomic<unsigned long>&)>:\n   0:\tl32i.n\ta8, a2, 0\n"
        self.assertEqual(cac.lock_calls(listing), [])

    def test_the_plain_accesses_are_counted(self):
        self.assertEqual(cac.plain_accesses(PLAIN), {"memw": 8, "s32i.n": 2, "l32i.n": 2})


class RequiredAccesses(unittest.TestCase):
    def test_the_real_listing_has_every_access(self):
        self.assertEqual(cac.missing_accesses(PLAIN), [])

    def test_an_object_without_the_functions_fails(self):
        # Review of PR #100: no forbidden call in an empty object is not evidence.
        problems = cac.missing_accesses("")
        self.assertEqual(len(problems), 4)

    def test_a_missing_function_fails(self):
        listing = PLAIN.replace("DataInHandover::confirm(", "DataInHandover::other(")
        problems = cac.missing_accesses(listing)
        self.assertEqual(len(problems), 1)
        self.assertIn("confirm", problems[0])

    def test_a_store_without_its_barrier_before_fails(self):
        listing = PLAIN.replace("   c:\t0020c0        \tmemw\n   f:", "   c:\t0020c0        \tnop\n   f:")
        problems = cac.missing_accesses(listing)
        self.assertEqual(len(problems), 1)
        self.assertIn("confirm", problems[0])

    def test_a_load_without_its_barrier_after_fails(self):
        listing = PLAIN.replace("   6:\t0238      \tl32i.n\ta3, a2, 0\n   8:\t0020c0        \tmemw",
                                "   6:\t0238      \tl32i.n\ta3, a2, 0\n   8:\t0020c0        \tnop")
        problems = cac.missing_accesses(listing)
        self.assertEqual(len(problems), 1)
        self.assertIn("pending", problems[0])


if __name__ == "__main__":
    unittest.main()
