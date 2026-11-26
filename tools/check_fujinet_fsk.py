#!/usr/bin/env python3
"""Check that the owner's FujiNet files in rb_fujinet_fsk are the ones recorded (ADR-012 amendment 2).

components/rb_fujinet_fsk/fuente.sha256 lists each file with its SHA-256 at the fixed commit (cf3382f) and
here, both over LF line ends. A file with no recorded change ("-") must be identical to the commit: both
hashes equal and the file's hash equal to them. A file with a change must match its recorded hash here,
and its change must be one this script knows (CHANGES): the lines it removes are absent.

Fails (exit 1) on: a missing manifest or file, a hash that differs, an unknown change, a malformed line,
or a file in lib/ that the manifest does not list.

Usage: python tools/check_fujinet_fsk.py [repo_root]
"""

import hashlib
import sys
from pathlib import Path

COMPONENT = Path("components/rb_fujinet_fsk")

# Changes to the owner's files, and the lines each one removes (FUENTE.md, "Cambio idf61-rmt-flags").
CHANGES = {
    "idf61-rmt-flags": (
        "tx_cfg.flags.io_loop_back = false;",
        "tx_cfg.flags.io_od_mode = false;",
    ),
}


def lf_sha256(path: Path) -> str:
    return hashlib.sha256(path.read_bytes().replace(b"\r\n", b"\n")).hexdigest()


def check_repo(root: Path) -> list:
    errors = []
    manifest = root / COMPONENT / "fuente.sha256"
    if not manifest.is_file():
        return [f"{manifest.relative_to(root).as_posix()} is missing"]
    listed = set()
    for number, line in enumerate(manifest.read_text(encoding="utf-8").splitlines(), 1):
        if not line.strip() or line.startswith("#"):
            continue
        fields = line.split()
        if len(fields) != 5:
            errors.append(f"fuente.sha256:{number}: expected 5 fields")
            continue
        here, _upstream_path, upstream, recorded, change = fields
        listed.add(here)
        path = root / COMPONENT / here
        if not path.is_file():
            errors.append(f"{here}: missing")
            continue
        actual = lf_sha256(path)
        if actual != recorded:
            errors.append(f"{here}: SHA-256 {actual} is not the recorded {recorded}")
        if change == "-":
            if upstream != recorded:
                errors.append(f"{here}: no change recorded, but its hash differs from the commit's")
        elif change not in CHANGES:
            errors.append(f"{here}: unknown change '{change}'")
        else:
            text = path.read_text(encoding="utf-8")
            for removed in CHANGES[change]:
                if removed in text:
                    errors.append(f"{here}: change '{change}' removes '{removed}', but it is there")
    lib = root / COMPONENT / "lib"
    for path in sorted(lib.rglob("*")) if lib.is_dir() else []:
        relative = path.relative_to(root / COMPONENT).as_posix()
        if path.is_file() and not relative.startswith("lib/include/") and relative not in listed:
            errors.append(f"{relative}: in lib/ but not in fuente.sha256")
    return errors


def main(argv: list) -> int:
    root = Path(argv[1]) if len(argv) > 1 else Path(__file__).resolve().parent.parent
    errors = check_repo(root)
    for error in errors:
        print(f"check_fujinet_fsk: {error}")
    if errors:
        return 1
    print("check_fujinet_fsk: OK")
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv))
