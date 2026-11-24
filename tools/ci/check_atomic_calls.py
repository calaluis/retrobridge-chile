"""Checks that an object file's atomics compile to plain instructions, not to lock-taking calls.

menu-cinta T9a.1 (plan §2.4): DataInHandover passes DATA IN between the SIO task and the cassette
task with atomics only, no lock. The ESP32 GCC (Xtensa) does not report std::atomic<uint32_t> as
always lock-free, so the compiler's word is not enough: this script reads the object code.

  check  --obj X.obj           fails if X calls __atomic_*, __sync_* or a FreeRTOS critical section;
                               prints the disassembly and the loads, stores and barriers it found.
  probe  --compdb compile_commands.json --like data_in_handover.cpp
                               negative control: compiles a 64-bit atomic load (which needs libatomic
                               on a 32-bit core) with the same command, and fails if the check does
                               NOT see the call. A check that cannot fail proves nothing.

Run with the ESP-IDF environment loaded (xtensa-esp32-elf-objdump on PATH), or pass --objdump.
"""

import argparse
import json
import re
import shlex
import subprocess
import sys
import tempfile
from pathlib import Path

LOCK_CALL = re.compile(
    r"\b(__atomic_\w+|__sync_\w+|vPortEnterCritical\w*|vPortExitCritical\w*|xPortEnterCritical\w*"
    r"|spinlock_acquire\w*|esp_cpu_compare_and_set\w*)\b"
)
LOAD_STORE = re.compile(r"\s(l32i(?:\.n)?|s32i(?:\.n)?|l32ai|s32ri|memw)\s")

PROBE_SOURCE = """#include <atomic>
#include <cstdint>
std::atomic<uint64_t> g_probe{0};
uint64_t rb_probe_load() { return g_probe.load(std::memory_order_acquire); }
"""


def lock_calls(disassembly):
    """The lines of an `objdump -dr` listing that call into a lock or a libatomic routine."""
    return [line.strip() for line in disassembly.splitlines() if LOCK_CALL.search(line)]


def plain_accesses(disassembly):
    """How many 32-bit loads, stores and memory barriers the listing has, by mnemonic."""
    counts = {}
    for line in disassembly.splitlines():
        match = LOAD_STORE.search(line + " ")
        if match:
            counts[match.group(1)] = counts.get(match.group(1), 0) + 1
    return counts


def disassemble(objdump, obj):
    return subprocess.run([objdump, "-dr", "-C", str(obj)], check=True, capture_output=True, text=True).stdout


def check(objdump, obj):
    listing = disassemble(objdump, obj)
    print(listing)
    calls = lock_calls(listing)
    print(f"accesos directos: {plain_accesses(listing)}")
    if calls:
        print(f"FALLO: {obj} llama a rutinas con cerrojo o de libatomic:")
        for line in calls:
            print(f"  {line}")
        return 1
    print(f"OK: {obj} no llama a __atomic_*, __sync_* ni a secciones críticas")
    return 0


def compile_like(compdb, like, source, output):
    """Compiles `source` into `output` with the compile command of the entry ending in `like`."""
    entries = json.loads(Path(compdb).read_text(encoding="utf-8"))
    entry = next((e for e in entries if e["file"].endswith(like)), None)
    if entry is None:
        raise SystemExit(f"FALLO: {like} no está en {compdb}")
    args = entry["arguments"] if "arguments" in entry else shlex.split(entry["command"])
    out, skip = [], 0
    for i, arg in enumerate(args):
        if skip:
            skip -= 1
            continue
        if arg in ("-o", "-MF", "-MT", "-MQ"):
            skip = 1
            continue
        if arg in ("-MD", "-MMD", "-c") or arg == entry["file"] or arg.endswith(like):
            continue
        out.append(arg)
    subprocess.run(out + ["-c", str(source), "-o", str(output)], check=True, cwd=entry["directory"])


def probe(objdump, compdb, like):
    with tempfile.TemporaryDirectory() as tmp:
        source = Path(tmp) / "rb_atomic_probe.cpp"
        source.write_text(PROBE_SOURCE, encoding="utf-8")
        obj = Path(tmp) / "rb_atomic_probe.o"
        compile_like(compdb, like, source, obj)
        calls = lock_calls(disassemble(objdump, obj))
    if not calls:
        print("FALLO: el control negativo (carga atómica de 64 bits) no se detectó: la comprobación no sirve")
        return 1
    print(f"OK: el control negativo se detecta: {calls[0]}")
    return 0


def main(argv=None):
    parser = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    parser.add_argument("mode", choices=("check", "probe"))
    parser.add_argument("--objdump", default="xtensa-esp32-elf-objdump")
    parser.add_argument("--obj")
    parser.add_argument("--compdb")
    parser.add_argument("--like", default="data_in_handover.cpp")
    args = parser.parse_args(argv)
    if args.mode == "check":
        return check(args.objdump, Path(args.obj))
    return probe(args.objdump, args.compdb, args.like)


if __name__ == "__main__":
    sys.exit(main())
