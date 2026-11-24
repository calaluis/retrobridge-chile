"""Self-tests for tools/ci/check_atomic_calls.py, on synthetic objdump listings.

Run with: python -m unittest discover -s tools/tests
"""

import sys
import unittest
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent.parent / "ci"))

import check_atomic_calls as cac  # noqa: E402

PLAIN = """
00000000 <rb::host::DataInHandover::pending() const>:
   0:	004136        	entry	a1, 32
   3:	0288      	l32i.n	a8, a2, 0
   5:	0020c0        	memw
   8:	f01d      	retw.n

0000000c <rb::host::DataInHandover::confirm(unsigned long, rb::host::HandoverAck)>:
   c:	0020c0        	memw
   f:	0389      	s32i.n	a8, a3, 0
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
        self.assertEqual(cac.plain_accesses(PLAIN), {"l32i.n": 1, "memw": 2, "s32i.n": 1})


if __name__ == "__main__":
    unittest.main()
