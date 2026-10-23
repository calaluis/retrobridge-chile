#!/usr/bin/env python3
"""Check that every third-party dependency is listed in THIRD_PARTY_NOTICES.md (ADR-013).

Two kinds of dependencies are detected:
  * vendored code: any directory (outside the repository root) holding a
    LICENSE/COPYING file; its repository-relative path must appear in the notices.
  * ESP-IDF component manager dependencies: every name under `dependencies:` in
    an idf_component.yml, except `idf` itself, must appear in the notices.

Usage: python tools/check_notices.py [repo_root]
"""

import re
import sys
from pathlib import Path

SKIP_DIRS = {".git", "build", "managed_components"}
LICENSE_FILE = re.compile(r"^(LICEN[CS]E|COPYING)(\..*)?$", re.IGNORECASE)


def walk(root: Path):
    for path in root.rglob("*"):
        if any(part in SKIP_DIRS for part in path.relative_to(root).parts):
            continue
        yield path


def vendored_dirs(root: Path) -> list:
    dirs = set()
    for path in walk(root):
        if path.is_file() and LICENSE_FILE.match(path.name) and path.parent != root:
            dirs.add(path.parent.relative_to(root).as_posix())
    return sorted(dirs)


def manifest_dependencies(root: Path) -> list:
    names = set()
    for manifest in walk(root):
        if manifest.name != "idf_component.yml":
            continue
        in_deps = False
        for line in manifest.read_text(encoding="utf-8").splitlines():
            if re.match(r"^dependencies:\s*$", line):
                in_deps = True
                continue
            if in_deps:
                if line and not line[0].isspace():
                    in_deps = False
                    continue
                match = re.match(r"^  ([A-Za-z0-9_./-]+):", line)
                if match and match.group(1) != "idf":
                    names.add(match.group(1))
    return sorted(names)


def main(argv: list) -> int:
    root = Path(argv[1]) if len(argv) > 1 else Path(__file__).resolve().parent.parent
    notices_path = root / "THIRD_PARTY_NOTICES.md"
    if not notices_path.is_file():
        print("check_notices: THIRD_PARTY_NOTICES.md is missing")
        return 1
    notices = notices_path.read_text(encoding="utf-8")
    missing = [d for d in vendored_dirs(root) + manifest_dependencies(root) if d not in notices]
    for name in missing:
        print(f"check_notices: '{name}' is not listed in THIRD_PARTY_NOTICES.md")
    if missing:
        return 1
    print("check_notices: OK")
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv))
