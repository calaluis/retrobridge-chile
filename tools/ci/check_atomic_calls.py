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
LOAD_STORE = re.compile(r"\s(l32i(?:\.n)?|s32i(?:\.n)?|l8ui|s8i|l32ai|s32ri|memw)\s")

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


# The functions that must be in the object, and the atomic access each one must show: an acquire load
# is a load (`l32i`, or `l8ui` for a bool) followed by `memw`; a release store is `memw` followed by a
# store (`s32i`, or `s8i`). "handover": DataInHandover (T9a.1). "progress": the cassette ports'
# TapeProgress accesses in main/cassette_ports.cpp (T9a.7, requirement 2).
REQUIRED = {
    "rb::host::DataInHandover::request(": "store",
    "rb::host::DataInHandover::confirm(": "store",
    "rb::host::DataInHandover::ackFor(": "load",
    "rb::host::DataInHandover::pending(": "load",
}
PROGRESS = {
    "rb::menusio::ProgressSink::publish(": "store",
    "rb::menusio::tapeEnded(": "load",
}
# fix-cinta-data-in-65xe (review PR #124, H1): the take diagnostics, written by the cassette task and read
# by the SIO task. "probe": EspDataIn's hardwareFree() counters (esp_data_in.cpp). "tick": the cassette
# loop's last tick (cassette_service.cpp).
PROBE = {
    "rb::esp::EspDataIn::hardwareFree(": "store",
    "rb::esp::EspDataIn::hardwareProbeCalls(": "load",
}
TICK = {
    "rb::esp::CassetteService::loop(": "store",
    "rb::esp::CassetteService::lastTickUs(": "load",
}
# fix-cinta-data-in-65xe (review PR #124, M1): the issue #125 counters, written by the SIO task and read by
# the console. "uart": EspSioUart's write failures (esp_sio_uart.cpp); "menu": MenuSioTransport's counters
# (menu_sio_transport.cpp). A key may list alternatives with "|": the release store is in the function
# itself, or in the file's bump() helper when the compiler does not inline it.
UART = {
    "rb::esp::EspSioUart::write(|rb::esp::(anonymous namespace)::bump(": "store",
}
MENU = {
    "rb::host::MenuSioTransport::|rb::host::(anonymous namespace)::bump(": "store",
}
PROFILES = {"handover": REQUIRED, "progress": PROGRESS, "probe": PROBE, "tick": TICK, "uart": UART, "menu": MENU}
FUNCTION = re.compile(r"^[0-9a-f]+ <(.+)>:$")
INSTRUCTION = re.compile(r"^\s*[0-9a-f]+:\s+[0-9a-f]+\s+([a-z0-9.]+)")


def functions(disassembly):
    """{demangled name: [mnemonics]} for every function in an `objdump -dr -C` listing."""
    result, current = {}, None
    for line in disassembly.splitlines():
        header = FUNCTION.match(line.strip())
        if header:
            current = header.group(1)
            result[current] = []
            continue
        instruction = INSTRUCTION.match(line)
        if current is not None and instruction:
            result[current].append(instruction.group(1))
    return result


def has_access(mnemonics, kind):
    """A 32-bit acquire load (l32i then memw) or release store (memw then s32i) in this function."""
    for i in range(len(mnemonics) - 1):
        first, second = mnemonics[i], mnemonics[i + 1]
        if kind == "load" and first in ("l32i", "l32i.n", "l8ui") and second == "memw":
            return True
        if kind == "store" and first == "memw" and second in ("s32i", "s32i.n", "s8i"):
            return True
    return False


def missing_accesses(disassembly, required=None):
    """What the required functions lack: absent, or without their access and barrier."""
    found = functions(disassembly)
    problems = []
    for prefix, kind in (REQUIRED if required is None else required).items():
        alternatives = tuple(prefix.split("|"))
        names = [name for name in found if name.startswith(alternatives)]
        if not names:
            problems.append(f"{prefix}...) no está en el objeto")
        elif not any(has_access(found[name], kind) for name in names):
            what = "l32i seguido de memw" if kind == "load" else "memw seguido de s32i"
            problems.append(f"{names[0]}: falta {what}")
    return problems


def check(objdump, obj, expect="handover"):
    listing = disassemble(objdump, obj)
    print(listing)
    calls = lock_calls(listing)
    problems = missing_accesses(listing, PROFILES[expect])
    print(f"accesos directos: {plain_accesses(listing)}")
    if calls:
        print(f"FALLO: {obj} llama a rutinas con cerrojo o de libatomic:")
        for line in calls:
            print(f"  {line}")
    if problems:
        print(f"FALLO: {obj} no muestra los accesos atómicos esperados:")
        for line in problems:
            print(f"  {line}")
    if calls or problems:
        return 1
    print(f"OK: {obj}: las {len(PROFILES[expect])} funciones de «{expect}» usan cargas/almacenamientos con memw; "
          "ninguna llamada a __atomic_*, __sync_* ni a secciones críticas")
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
    parser.add_argument("--expect", choices=sorted(PROFILES), default="handover",
                        help="which functions must be in the object (check mode)")
    args = parser.parse_args(argv)
    if args.mode == "check":
        return check(args.objdump, Path(args.obj), args.expect)
    return probe(args.objdump, args.compdb, args.like)


if __name__ == "__main__":
    sys.exit(main())
