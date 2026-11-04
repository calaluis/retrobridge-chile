#!/usr/bin/env python3
"""Enforce the dependency rule of ADR-002 on #include directives.

ESP-IDF always exposes its common components (freertos, log, esp_common, ...)
to every component, so CMake REQUIRES alone cannot keep the pure layers clean.
This script fails if a pure component includes:
  * a platform header (ESP-IDF, FreeRTOS, drivers, FATFS, SD/MMC, lwIP, sockets), or
  * a header from a layer above it (e.g. rb_domain including rb/app/...).

Usage: python tools/check_layering.py [repo_root]
Exit status 0 when clean, 1 when violations are found.
"""

import re
import sys
from pathlib import Path

# Pure component -> include prefixes (under rb/) it may use, besides its own.
PURE_LAYERS = {
    "rb_core": ("rb/core/",),
    "rb_domain": ("rb/core/", "rb/domain/"),
    "rb_application": ("rb/core/", "rb/domain/", "rb/app/"),
    "rb_sio": ("rb/core/", "rb/domain/", "rb/app/", "rb/sio/"),
    "rb_board": ("rb/core/", "rb/board/"),
    "rb_hw_diag": ("rb/core/", "rb/hwdiag/"),  # autonomous diagnostics: no Atari domain, no ESP-IDF
    "rb_netsio": ("rb/core/", "rb/app/", "rb/netsio/"),  # NetSIO transport (ADR-016): no sockets, no rb_sio
}

FORBIDDEN_PLATFORM = re.compile(
    r"^(esp_|esp/|freertos/|driver/|hal/|soc/|rom/|sdkconfig|ff\.h|diskio|"
    r"sdmmc|vfs|lwip/|sys/socket|netinet/|arpa/|mbedtls/|nvs)"
)

INCLUDE = re.compile(r'^\s*#\s*include\s*[<"]([^>"]+)[>"]')
SOURCE_SUFFIXES = {".c", ".cc", ".cpp", ".h", ".hh", ".hpp", ".inc"}


def check_file(path: Path, allowed_rb: tuple) -> list:
    violations = []
    for lineno, line in enumerate(path.read_text(encoding="utf-8").splitlines(), 1):
        match = INCLUDE.match(line)
        if not match:
            continue
        header = match.group(1)
        if FORBIDDEN_PLATFORM.match(header):
            violations.append((path, lineno, header, "platform header in a pure layer"))
        elif header.startswith("rb/") and not header.startswith(allowed_rb):
            violations.append((path, lineno, header, "include from a higher layer"))
    return violations


def check_repo(root: Path) -> list:
    violations = []
    for component, allowed_rb in PURE_LAYERS.items():
        component_dir = root / "components" / component
        if not component_dir.is_dir():
            continue
        for path in sorted(component_dir.rglob("*")):
            if path.is_file() and path.suffix in SOURCE_SUFFIXES:
                violations.extend(check_file(path, allowed_rb))
    return violations


def main(argv: list) -> int:
    root = Path(argv[1]) if len(argv) > 1 else Path(__file__).resolve().parent.parent
    violations = check_repo(root)
    for path, lineno, header, reason in violations:
        print(f"{path.relative_to(root)}:{lineno}: #include <{header}>: {reason}")
    if violations:
        print(f"check_layering: {len(violations)} violation(s)")
        return 1
    print("check_layering: OK")
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv))
