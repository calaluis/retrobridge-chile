"""Bench C8a, phase F1 (docs/hardware/banco-c8a-aislado.md): how the bench is wired in main/.

- C-2: the bench never runs at boot. app_main.cpp does not call run_c8; it registers the console order
  (start_c8_command) in the bench build, and only main/bench_c8_console.cpp calls run_c8, after the gate.
- The bench build does not start the cassette: its loop calls EspDataIn::pump() on the same driver the console
  order uses, and EspDataIn is driven by one task only.
- C-1: the console order reads GPIO35 and asks the gate (classifySio5v, C8Trigger) before run_c8.

Run with: python -m unittest discover -s tools/tests
"""

import re
import unittest
from pathlib import Path

REPO = Path(__file__).resolve().parents[2]
APP_MAIN = REPO / "main" / "app_main.cpp"
CONSOLE = REPO / "main" / "bench_c8_console.cpp"
MAIN_CMAKE = REPO / "main" / "CMakeLists.txt"


def branches(text: str):
    """Yields (line, bench) for each source line: bench is True inside a CONFIG_RB_BENCH_C8 branch, False inside
    its #else (or an #if !CONFIG_RB_BENCH_C8), None outside both."""
    stack = []  # (condition, in_else)
    for line in text.splitlines():
        stripped = line.strip()
        if stripped.startswith("#if"):
            stack.append((stripped.split(None, 1)[1] if " " in stripped else "", False))
            continue
        if stripped.startswith("#else") or stripped.startswith("#elif"):
            if stack:
                stack[-1] = (stack[-1][0], True)
            continue
        if stripped.startswith("#endif"):
            if stack:
                stack.pop()
            continue
        bench = None
        for condition, in_else in stack:
            if condition == "CONFIG_RB_BENCH_C8":
                bench = not in_else
            elif condition == "!CONFIG_RB_BENCH_C8":
                bench = in_else
        yield line, bench


def code(text: str) -> str:
    """The text without // comments, so a comment that names a call does not count as the call."""
    return "\n".join(line.split("//", 1)[0] for line in text.splitlines())


class BenchC8WiringTest(unittest.TestCase):
    def test_app_main_never_runs_the_bench_at_boot(self):
        self.assertFalse("run_c8(" in code(APP_MAIN.read_text(encoding="utf-8")), "app_main.cpp calls run_c8")

    def test_the_bench_build_registers_the_console_order(self):
        lines = [(code(line), bench) for line, bench in branches(APP_MAIN.read_text(encoding="utf-8"))]
        registered = [bench for line, bench in lines if "start_c8_command(" in line]
        self.assertEqual(registered, [True], "start_c8_command must be called once, only in the bench build")

    def test_the_bench_build_never_starts_the_cassette(self):
        lines = [(code(line), bench) for line, bench in branches(APP_MAIN.read_text(encoding="utf-8"))]
        starts = [bench for line, bench in lines if re.search(r"\bg_cassette\.start\(\)", line)]
        self.assertTrue(starts, "app_main.cpp starts the cassette somewhere")
        self.assertTrue(all(bench is False for bench in starts),
                        "g_cassette.start() only where CONFIG_RB_BENCH_C8 is off")

    def test_only_the_console_order_runs_the_bench_after_the_gate(self):
        self.assertTrue(CONSOLE.exists(), "main/bench_c8_console.cpp")
        text = code(CONSOLE.read_text(encoding="utf-8"))
        self.assertEqual(len(re.findall(r"\brun_c8\(", text)), 1)
        run = text.index("run_c8(")
        for gate in ("classifySio5v(", ".request(", "Answer::Run"):
            self.assertIn(gate, text[:run], f"{gate} before run_c8")
        for path in (REPO / "main").glob("*.cpp"):
            if path.name not in ("bench_c8.cpp", "bench_c8_console.cpp"):
                self.assertNotIn("run_c8(", code(path.read_text(encoding="utf-8")), path.name)

    def test_the_console_order_is_built_only_with_the_bench(self):
        text = MAIN_CMAKE.read_text(encoding="utf-8")
        block = re.search(r"if\(CONFIG_RB_BENCH_C8\)(.*?)endif\(\)", text, re.S)
        self.assertIsNotNone(block)
        self.assertIn("bench_c8_console.cpp", block.group(1))
        self.assertEqual(text.count("bench_c8_console.cpp"), 1)


if __name__ == "__main__":
    unittest.main()
