#!/usr/bin/env python3
"""Check that every third-party dependency is listed in THIRD_PARTY_NOTICES.md (ADR-013).

Two kinds of dependencies are detected:
  * vendored code: any directory (outside the repository root) holding a
    LICENSE/COPYING file; its repository-relative path must appear in the notices.
  * ESP-IDF component manager dependencies: every name under `dependencies:` in
    an idf_component.yml, except `idf` itself, must appear in the notices.

Only files the repository can version are examined: tracked files plus untracked
files not ignored by a committed .gitignore. Copies that a local tool generates and
the repository's .gitignore excludes (e.g. the rsc harness under .rsc/,
.claude/skills/<id>, .codex/rsc/<id>) are machine state, not distributed code.
Machine-local rules (.git/info/exclude, core.excludesFile) are deliberately NOT
honoured, so they cannot hide vendored code from this check. Outside a git work
tree every file is examined.

Usage: python tools/check_notices.py [repo_root]
"""

import re
import subprocess
import sys
from pathlib import Path

SKIP_DIRS = {".git", "build", "managed_components"}
LICENSE_FILE = re.compile(r"^(LICEN[CS]E|COPYING)(\..*)?$", re.IGNORECASE)


def versionable_files(root: Path):
    """Tracked files plus untracked files not ignored by .gitignore; None outside git.

    Git lists a nested repository or a submodule as a single directory entry; the
    caller expands it. A tracked file deleted from disk is still listed.
    """
    try:
        result = subprocess.run(
            ["git", "-C", str(root), "ls-files", "-z", "--cached", "--others",
             "--exclude-per-directory=.gitignore"],
            capture_output=True, check=True)
    except (OSError, subprocess.CalledProcessError):
        return None
    names = result.stdout.decode("utf-8", errors="surrogateescape").split("\0")
    return [root / name for name in names if name]


def _skipped(root: Path, path: Path) -> bool:
    return any(part in SKIP_DIRS for part in path.relative_to(root).parts)


def walk(root: Path):
    files = versionable_files(root)
    if files is None:
        files = root.rglob("*")
    for path in files:
        if _skipped(root, path):
            continue
        if path.is_dir():
            # Nested repository or submodule: inspect everything inside it.
            for inner in path.rglob("*"):
                if not _skipped(root, inner):
                    yield inner
        elif path.exists():
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
