#!/usr/bin/env python3
"""Verify and package the RetroBridge diagnostic firmware for the FujiNet Atari
FN32ROV-1.3/1.5 board (RB_BOARD_FUJINET_ATARI_V1X) as a CI artifact.

Every check must pass or nothing is written. The package is assembled from an
explicit list of build outputs only (no flash dumps, no NVS data, no private
files) and carries a SHA-256 manifest and a provenance record.

Formats checked here come from the ESP-IDF v6.1 sources (see docs/fuentes.md):
esp_image_header_t / esp_app_desc_t (components/bootloader_support,
components/esp_app_format), the binary partition table
(components/partition_table/gen_esp32part.py) and flasher_args.json
(components/esptool_py). No ESP-IDF code is imported: only the standard library.

Usage (CI, after `. $IDF_PATH/export.sh` and the build):
  python tools/ci/firmware_package.py package --src SRC --build SRC/build-fujinet \
         --reports DIR --out OUT --baseline <full commit>
Re-check a downloaded package (no hardware, no build tree needed):
  python tools/ci/firmware_package.py verify <package dir>
"""

import argparse
import csv
import hashlib
import io
import json
import os
import re
import shutil
import struct
import subprocess
import sys
from dataclasses import dataclass, field
from datetime import datetime, timezone
from pathlib import Path

PROFILE_KCONFIG = "CONFIG_RB_BOARD_FUJINET_ATARI_V1X"
UNCONFIRMED_KCONFIG = "CONFIG_RB_BOARD_UNCONFIRMED"
TARGET = "esp32"
PROJECT_NAME = "retrobridge"
IDF_PREFIX = "v6.1"
FLASH_MODE, FLASH_FREQ, FLASH_SIZE = "dio", "40m", "16MB"
BOOTLOADER_OFFSET = 0x1000            # fixed for the ESP32
FACTORY_OFFSET, FACTORY_SIZE = 0x10000, 0x400000
LOW_FREE_FRACTION = 0.10
MAX_PACKAGE_BYTES = 40 * 1024 * 1024

# esp_image_header_t (24 bytes) followed by the first esp_image_segment_header_t (8 bytes).
IMAGE_HEADER = struct.Struct("<BBBBIB3sHBHH4sB")
IMAGE_MAGIC = 0xE9
SPI_MODE_DIO = 2                       # esp_image_spi_mode_t
SPI_SPEED_40M = 0                      # ESP_IMAGE_SPI_SPEED_DIV_2 of the 80 MHz source on the ESP32
SPI_SIZE_16MB = 4                      # esp_image_flash_size_t
CHIP_ID_ESP32 = 0
APP_DESC_OFFSET = IMAGE_HEADER.size + 8
APP_DESC = struct.Struct("<II8s32s32s16s16s32s32s")
APP_DESC_MAGIC = 0xABCD5432

PARTITION_ENTRY = struct.Struct("<2sBBII16sI")
PARTITION_MAGIC = b"\xAA\x50"
PARTITION_MD5_BEGIN = b"\xEB\xEB" + b"\xFF" * 14
PARTITION_TYPES = {"app": 0x00, "data": 0x01}
PARTITION_SUBTYPES = {
    0x00: {"factory": 0x00, "test": 0x20, **{f"ota_{n}": 0x10 + n for n in range(16)}},
    0x01: {"ota": 0x00, "phy": 0x01, "nvs": 0x02, "coredump": 0x03, "nvs_keys": 0x04,
           "efuse": 0x05, "undefined": 0x06, "esphttpd": 0x80, "fat": 0x81, "spiffs": 0x82, "littlefs": 0x83},
}

# Firmware source calls that would drive or claim SIO pins (gate H4): forbidden in main/ and components/.
FORBIDDEN_CALLS = re.compile(r"\b(gpio_config|gpio_set_direction|gpio_set_level|gpio_set_pull_mode|gpio_reset_pin|"
                             r"gpio_pullup_en|gpio_pulldown_en|uart_driver_install|uart_param_config|uart_set_pin)\s*\(")
SOURCE_SUFFIXES = {".c", ".cpp", ".h", ".hpp"}
SECRET_VALUE = re.compile(r'^CONFIG_\w*(PASSWORD|PASSWD|PSK|SSID|SECRET|TOKEN)\w*="[^"]+"', re.MULTILINE)
PRIVATE_KEY = re.compile(r"-----BEGIN [A-Z ]*PRIVATE KEY-----")
TEXT_SUFFIXES = {".txt", ".json", ".csv", ".md", ""}
IDENTITY_PATHS = ("main", "components", "partitions", "sdkconfig.defaults",
                  "sdkconfig.board.fujinet-atari-v1x", "CMakeLists.txt")


@dataclass
class Check:
    name: str
    ok: bool
    detail: str


@dataclass
class CheckLog:
    checks: list = field(default_factory=list)
    warnings: list = field(default_factory=list)
    facts: dict = field(default_factory=dict)

    def require(self, name: str, ok: bool, detail: str = "") -> bool:
        self.checks.append(Check(name, bool(ok), detail))
        return bool(ok)

    @property
    def ok(self) -> bool:
        return bool(self.checks) and all(c.ok for c in self.checks)

    def text(self) -> str:
        lines = [f"{'OK   ' if c.ok else 'FALLO'} {c.name}: {c.detail}" for c in self.checks]
        lines += [f"AVISO {w}" for w in self.warnings]
        lines.append("RESULTADO: " + ("todas las comprobaciones OK" if self.ok else "HAY FALLOS"))
        return "\n".join(lines) + "\n"


@dataclass
class SourceState:
    head: str
    dirty: bool
    trees: dict


@dataclass
class Files:
    """Where each checked file lives (a build directory or an unpacked package)."""
    sdkconfig: Path
    flasher_args: Path
    flash_args: Path
    bootloader: Path
    partition_table: Path
    app: Path
    elf: Path
    csv: Path
    project_description: Path


# ---------- parsers (pure functions) ----------

def parse_sdkconfig(text: str) -> dict:
    values = {}
    for line in text.splitlines():
        if line.startswith("CONFIG_") and "=" in line:
            key, value = line.split("=", 1)
            values[key] = value.strip().strip('"')
    return values


def parse_image_header(data: bytes) -> dict:
    if len(data) < IMAGE_HEADER.size:
        return {"magic": None}
    f = IMAGE_HEADER.unpack_from(data)
    return {"magic": f[0], "segments": f[1], "spi_mode": f[2], "spi_speed": f[3] & 0x0F,
            "spi_size": f[3] >> 4, "chip_id": f[7], "hash_appended": f[12]}


def parse_app_desc(image: bytes) -> dict:
    if len(image) < APP_DESC_OFFSET + APP_DESC.size:
        return {"magic": None}
    f = APP_DESC.unpack_from(image, APP_DESC_OFFSET)

    def text(raw: bytes) -> str:
        return raw.split(b"\0", 1)[0].decode("ascii", "replace")

    return {"magic": f[0], "version": text(f[3]), "project_name": text(f[4]), "time": text(f[5]),
            "date": text(f[6]), "idf_ver": text(f[7]), "app_elf_sha256": f[8].hex()}


def parse_partition_table(data: bytes):
    """Returns (entries, md5_ok). Each entry: (name, type, subtype, offset, size)."""
    entries, pos = [], 0
    while pos + PARTITION_ENTRY.size <= len(data):
        chunk = data[pos:pos + PARTITION_ENTRY.size]
        if chunk[:2] != PARTITION_MAGIC:
            break
        magic, ptype, sub, offset, size, label, _flags = PARTITION_ENTRY.unpack(chunk)
        entries.append((label.split(b"\0", 1)[0].decode("ascii", "replace"), ptype, sub, offset, size))
        pos += PARTITION_ENTRY.size
    md5_entry = data[pos:pos + 32]
    md5_ok = md5_entry[:16] == PARTITION_MD5_BEGIN and md5_entry[16:] == hashlib.md5(data[:pos]).digest()
    return entries, md5_ok


def parse_number(text: str) -> int:
    text = text.strip().upper()
    scale = 1
    if text.endswith("K"):
        text, scale = text[:-1], 1024
    elif text.endswith("M"):
        text, scale = text[:-1], 1024 * 1024
    return int(text, 0) * scale


def parse_partition_csv(text: str):
    entries = []
    lines = [line for line in text.splitlines() if line.strip() and not line.lstrip().startswith("#")]
    for row in csv.reader(io.StringIO("\n".join(lines))):
        name, ptype, sub, offset, size = (cell.strip() for cell in row[:5])
        type_id = PARTITION_TYPES.get(ptype, None)
        if type_id is None:
            type_id = parse_number(ptype)
        sub_id = PARTITION_SUBTYPES.get(type_id, {}).get(sub)
        if sub_id is None:
            sub_id = parse_number(sub)
        entries.append((name, type_id, sub_id, parse_number(offset), parse_number(size)))
    return entries


def parse_flash_args(text: str):
    """Returns (settings, {offset: path}) from the build's flash_args file."""
    lines = [line.split() for line in text.splitlines() if line.strip()]
    settings, files = {}, {}
    if lines:
        tokens = lines[0]
        for i, token in enumerate(tokens[:-1]):
            key = token.lstrip("-").replace("_", "-")
            if key in ("flash-mode", "flash-freq", "flash-size"):
                settings[key] = tokens[i + 1]
    for parts in lines[1:]:
        if len(parts) == 2:
            files[int(parts[0], 0)] = parts[1]
    return settings, files


def scan_sources(src: Path):
    hits = []
    for base in ("main", "components"):
        root = src / base
        if not root.is_dir():
            continue
        for path in sorted(root.rglob("*")):
            if path.suffix not in SOURCE_SUFFIXES or "test" in path.relative_to(src).parts:
                continue
            for number, line in enumerate(path.read_text(encoding="utf-8", errors="replace").splitlines(), 1):
                if FORBIDDEN_CALLS.search(line):
                    hits.append(f"{path.relative_to(src).as_posix()}:{number}")
    return hits


def find_secrets(name: str, text: str):
    found = [f"{name}: {m.group(0).split('=')[0]}" for m in SECRET_VALUE.finditer(text)]
    if PRIVATE_KEY.search(text):
        found.append(f"{name}: clave privada")
    return found


def sha256_file(path: Path) -> str:
    h = hashlib.sha256()
    with path.open("rb") as f:
        for block in iter(lambda: f.read(1 << 20), b""):
            h.update(block)
    return h.hexdigest()


# ---------- checks ----------

def build_files(build: Path, src: Path, sdk: dict) -> Files:
    return Files(sdkconfig=build / "sdkconfig", flasher_args=build / "flasher_args.json", flash_args=build / "flash_args",
                 bootloader=build / "bootloader" / "bootloader.bin",
                 partition_table=build / "partition_table" / "partition-table.bin",
                 app=build / "retrobridge.bin", elf=build / "retrobridge.elf",
                 csv=src / sdk.get("CONFIG_PARTITION_TABLE_CUSTOM_FILENAME", "partitions/retrobridge-16mb.csv"),
                 project_description=build / "project_description.json")


def check_files(files: Files, log: CheckLog, build_relative=None) -> None:
    """Checks shared by the CI build and the re-verification of a downloaded package."""
    missing = [str(p) for p in vars(files).values() if not Path(p).is_file()]
    if not log.require("archivos presentes", not missing, ", ".join(missing) or "todos"):
        return

    sdk = parse_sdkconfig(files.sdkconfig.read_text(encoding="utf-8"))
    desc = json.loads(files.project_description.read_text(encoding="utf-8"))
    log.require("plataforma ESP32", sdk.get("CONFIG_IDF_TARGET") == TARGET and desc.get("target") == TARGET,
                f"CONFIG_IDF_TARGET={sdk.get('CONFIG_IDF_TARGET')}, project_description.target={desc.get('target')}")
    log.require("perfil FujiNet", sdk.get(PROFILE_KCONFIG) == "y" and sdk.get(UNCONFIRMED_KCONFIG) != "y",
                f"{PROFILE_KCONFIG}={sdk.get(PROFILE_KCONFIG)}, {UNCONFIRMED_KCONFIG}={sdk.get(UNCONFIRMED_KCONFIG, 'no')}")
    log.require("flash en sdkconfig",
                sdk.get("CONFIG_ESPTOOLPY_FLASHMODE") == FLASH_MODE and sdk.get("CONFIG_ESPTOOLPY_FLASHFREQ") == FLASH_FREQ
                and sdk.get("CONFIG_ESPTOOLPY_FLASHSIZE") == FLASH_SIZE,
                f"{sdk.get('CONFIG_ESPTOOLPY_FLASHMODE')}/{sdk.get('CONFIG_ESPTOOLPY_FLASHFREQ')}/"
                f"{sdk.get('CONFIG_ESPTOOLPY_FLASHSIZE')}")
    sio_options = [k for k, v in sdk.items() if k.startswith("CONFIG_RB_") and "SIO" in k and v not in ("", "n")]
    console = sdk.get("CONFIG_ESP_CONSOLE_UART_NUM")
    console_ok = console == "0" if console is not None else sdk.get("CONFIG_ESP_CONSOLE_UART_DEFAULT") == "y"
    log.require("sin GPIO/UART del SIO (configuración)", not sio_options and console_ok,
                f"opciones SIO: {sio_options or 'ninguna'}; consola en UART{console if console is not None else '0 (por defecto)'}")

    flasher = json.loads(files.flasher_args.read_text(encoding="utf-8"))
    fs = flasher.get("flash_settings", {})
    chip = flasher.get("extra_esptool_args", {}).get("chip")
    log.require("flasher_args.json",
                (fs.get("flash_mode"), fs.get("flash_freq"), fs.get("flash_size"), chip) == (FLASH_MODE, FLASH_FREQ, FLASH_SIZE, TARGET),
                f"{fs.get('flash_mode')}/{fs.get('flash_freq')}/{fs.get('flash_size')}, chip={chip}")
    fa_settings, fa_files = parse_flash_args(files.flash_args.read_text(encoding="utf-8"))
    log.require("flash_args", (fa_settings.get("flash-mode"), fa_settings.get("flash-freq"), fa_settings.get("flash-size"))
                == (FLASH_MODE, FLASH_FREQ, FLASH_SIZE), str(fa_settings))

    for label, path in (("bootloader", files.bootloader), ("aplicación", files.app)):
        h = parse_image_header(path.read_bytes()[:IMAGE_HEADER.size])
        log.require(f"cabecera {label}",
                    (h.get("magic"), h.get("spi_mode"), h.get("spi_speed"), h.get("spi_size"), h.get("chip_id"))
                    == (IMAGE_MAGIC, SPI_MODE_DIO, SPI_SPEED_40M, SPI_SIZE_16MB, CHIP_ID_ESP32),
                    f"magic=0x{h.get('magic') or 0:02X} modo={h.get('spi_mode')} frec={h.get('spi_speed')} "
                    f"tamaño={h.get('spi_size')} chip={h.get('chip_id')}")

    table, md5_ok = parse_partition_table(files.partition_table.read_bytes())
    log.facts["partitions"] = table
    log.require("tabla MD5", md5_ok, f"{len(table)} entradas")
    expected = parse_partition_csv(files.csv.read_text(encoding="utf-8"))
    log.require("tabla = CSV", table == expected, f"binario={len(table)} entradas, CSV={len(expected)}")
    apps = [e for e in table if e[1] == 0x00]
    factory = next((e for e in table if e[1] == 0x00 and e[2] == 0x00), None)
    log.require("partición factory", factory is not None and factory[3:] == (FACTORY_OFFSET, FACTORY_SIZE),
                f"{factory[0]} 0x{factory[3]:X}+0x{factory[4]:X}" if factory else "ausente")

    table_offset = parse_number(sdk.get("CONFIG_PARTITION_TABLE_OFFSET", "0x8000"))
    app_offset = factory[3] if factory else (apps[0][3] if apps else None)
    app_bin = desc.get("app_bin", files.app.name)
    want = {BOOTLOADER_OFFSET: "bootloader.bin", table_offset: "partition-table.bin", app_offset: Path(app_bin).name}
    json_files = {int(k, 0): Path(v).name for k, v in flasher.get("flash_files", {}).items()}
    args_files = {k: Path(v).name for k, v in fa_files.items()}
    log.require("direcciones", json_files == want and args_files == want,
                ", ".join(f"0x{k:X} {v}" for k, v in sorted(json_files.items(), key=lambda kv: kv[0])))
    if build_relative is not None:
        refs = [build_relative / v for v in flasher.get("flash_files", {}).values()]
        log.require("rutas de flasheo existen", all(p.is_file() for p in refs), ", ".join(str(p) for p in refs))

    app_bytes = files.app.read_bytes()
    app_size = len(app_bytes)
    limit = factory[4] if factory else 0
    free = limit - app_size
    log.facts.update(app_size=app_size, factory_size=limit, free=free)
    log.require("espacio en factory", 0 < app_size <= limit,
                f"aplicación {app_size} B de {limit} B, libres {free} B ({free / limit:.1%})" if limit else "sin factory")
    if limit and 0 <= free < LOW_FREE_FRACTION * limit:
        log.warnings.append(f"queda menos del 10 % libre en factory ({free} B)")
    boot_size = files.bootloader.stat().st_size
    log.require("espacio del bootloader", boot_size <= table_offset - BOOTLOADER_OFFSET,
                f"{boot_size} B de {table_offset - BOOTLOADER_OFFSET} B")

    app_desc = parse_app_desc(app_bytes)
    log.facts["app_desc"] = app_desc
    log.require("descriptor de aplicación", app_desc.get("magic") == APP_DESC_MAGIC
                and app_desc.get("project_name") == PROJECT_NAME and str(app_desc.get("idf_ver", "")).startswith(IDF_PREFIX),
                f"proyecto={app_desc.get('project_name')} idf={app_desc.get('idf_ver')}")
    log.require("versión de la aplicación", app_desc.get("version") == desc.get("project_version") and app_desc.get("version"),
                f"esp_app_desc={app_desc.get('version')!r}, ESP-IDF project_version={desc.get('project_version')!r}")
    elf_sha = sha256_file(files.elf)
    log.facts["elf_sha256"] = elf_sha
    log.require("SHA-256 del ELF", elf_sha == app_desc.get("app_elf_sha256"), f"{elf_sha[:16]}…")

    secrets = []
    for path in (files.sdkconfig, files.flasher_args, files.flash_args, files.project_description, files.csv):
        secrets += find_secrets(path.name, path.read_text(encoding="utf-8", errors="replace"))
    log.require("sin secretos", not secrets, ", ".join(secrets) or "ninguno")


def check_build(src: Path, build: Path, source: SourceState, baseline: str) -> CheckLog:
    log = CheckLog()
    log.require("commit del firmware", source.head == baseline, f"checkout={source.head}, fijado={baseline}")
    log.require("checkout limpio", not source.dirty, "sin cambios locales" if not source.dirty else "hay cambios")
    hits = scan_sources(src)
    log.require("sin GPIO/UART del SIO (código)", not hits, ", ".join(hits) or "ninguna llamada")
    sdk = parse_sdkconfig((build / "sdkconfig").read_text(encoding="utf-8")) if (build / "sdkconfig").is_file() else {}
    check_files(build_files(build, src, sdk), log, build_relative=build)
    return log


# ---------- package and verify ----------

LAYOUT = {
    "firmware/bootloader.bin": "bootloader", "firmware/partition-table.bin": "partition_table",
    "firmware/retrobridge.bin": "app", "firmware/retrobridge.elf": "elf",
    "flash/flash_args": "flash_args", "flash/flasher_args.json": "flasher_args",
    "config/sdkconfig": "sdkconfig", "config/retrobridge-16mb.csv": "csv",
    "config/project_description.json": "project_description",
}


def package_files(root: Path) -> Files:
    inverse = {v: root / k for k, v in LAYOUT.items()}
    return Files(**inverse)


def decoded_table(entries) -> str:
    types = {v: k for k, v in PARTITION_TYPES.items()}
    lines = ["# Nombre, Tipo, Subtipo, Desplazamiento, Tamaño (decodificado de partition-table.bin)"]
    for name, ptype, sub, offset, size in entries:
        subs = {v: k for k, v in PARTITION_SUBTYPES.get(ptype, {}).items()}
        lines.append(f"{name}, {types.get(ptype, hex(ptype))}, {subs.get(sub, hex(sub))}, 0x{offset:X}, 0x{size:X}")
    return "\n".join(lines) + "\n"


def tool_versions() -> dict:
    def run(*cmd):
        try:
            return subprocess.run(cmd, capture_output=True, text=True, timeout=60).stdout.strip().splitlines()[0]
        except (OSError, IndexError, subprocess.SubprocessError):
            return "no disponible"
    idf_path = os.environ.get("IDF_PATH", "")
    return {"idf_path_commit": run("git", "-C", idf_path, "rev-parse", "HEAD") if idf_path else "no disponible",
            "idf_describe": run("git", "-C", idf_path, "describe", "--tags") if idf_path else "no disponible",
            "compiler": run("xtensa-esp32-elf-gcc", "--version"), "python": sys.version.split()[0]}


def package(src: Path, build: Path, reports: Path, out: Path, source: SourceState, baseline: str, run: dict) -> CheckLog:
    log = check_build(src, build, source, baseline)
    for name in ("size.txt", "size-components.txt"):
        log.require(f"informe {name}", (reports / name).is_file() and (reports / name).stat().st_size > 0, str(reports / name))
    if not log.ok:
        return log

    sdk = parse_sdkconfig((build / "sdkconfig").read_text(encoding="utf-8"))
    files = build_files(build, src, sdk)
    out.mkdir(parents=True, exist_ok=False)
    for rel, attr in LAYOUT.items():
        (out / rel).parent.mkdir(parents=True, exist_ok=True)
        shutil.copyfile(getattr(files, attr), out / rel)
    for name in ("size.txt", "size-components.txt"):
        (out / "reports").mkdir(exist_ok=True)
        shutil.copyfile(reports / name, out / "reports" / name)

    flasher = json.loads(files.flasher_args.read_text(encoding="utf-8"))
    fs = flasher["flash_settings"]
    lines = [f"--flash-mode {fs['flash_mode']} --flash-freq {fs['flash_freq']} --flash-size {fs['flash_size']}"]
    for offset, path in sorted(((int(k, 0), v) for k, v in flasher["flash_files"].items())):
        lines.append(f"0x{offset:X} firmware/{Path(path).name}")
    (out / "flash" / "flash_args.paquete").write_text("\n".join(lines) + "\n", encoding="utf-8")
    (out / "config" / "partition-table.decoded.txt").write_text(decoded_table(log.facts["partitions"]), encoding="utf-8")

    identity = [f"firmware_source_commit {source.head}", f"baseline {baseline}",
                f"identical_to_baseline {'yes' if source.head == baseline else 'no'}"]
    identity += [f"tree {path} {sha}" for path, sha in sorted(source.trees.items())]
    (out / "SOURCE_IDENTITY.txt").write_text("\n".join(identity) + "\n", encoding="utf-8")

    provenance = {
        "package": "retrobridge-fw-fujinet-atari-v1x", "board_profile": PROFILE_KCONFIG,
        "firmware_source_commit": source.head, "baseline_commit": baseline,
        "workflow_commit": run.get("GITHUB_SHA"), "repository": run.get("GITHUB_REPOSITORY"),
        "run_id": run.get("GITHUB_RUN_ID"), "run_attempt": run.get("GITHUB_RUN_ATTEMPT"),
        "run_url": (f"{run.get('GITHUB_SERVER_URL')}/{run.get('GITHUB_REPOSITORY')}/actions/runs/{run.get('GITHUB_RUN_ID')}"
                    if run.get("GITHUB_RUN_ID") else None),
        "event": run.get("GITHUB_EVENT_NAME"), "built_at_utc": datetime.now(timezone.utc).isoformat(timespec="seconds"),
        "tools": tool_versions() if run else {}, "app_desc": log.facts["app_desc"], "elf_sha256": log.facts["elf_sha256"],
        "app_size": log.facts["app_size"], "factory_size": log.facts["factory_size"], "factory_free": log.facts["free"],
        "checks": [vars(c) for c in log.checks], "warnings": log.warnings,
    }
    (out / "PROVENANCE.json").write_text(json.dumps(provenance, indent=2, ensure_ascii=False) + "\n", encoding="utf-8")
    (out / "reports" / "checks.txt").write_text(log.text(), encoding="utf-8")
    (out / "LEEME.md").write_text(readme(provenance), encoding="utf-8")

    published_secrets = []
    for path in out.rglob("*"):
        if path.is_file() and path.suffix in TEXT_SUFFIXES:
            published_secrets += find_secrets(path.relative_to(out).as_posix(), path.read_text(encoding="utf-8", errors="replace"))
    total = sum(p.stat().st_size for p in out.rglob("*") if p.is_file())
    bins = sorted(p.name for p in out.rglob("*.bin"))
    clean = (not published_secrets and total <= MAX_PACKAGE_BYTES
             and bins == ["bootloader.bin", "partition-table.bin", "retrobridge.bin"])
    if not log.require("contenido del paquete", clean,
                       f"{total} B, binarios={bins}, secretos={published_secrets or 'ninguno'}"):
        shutil.rmtree(out)
        return log
    write_manifest(out)
    return log


def write_manifest(out: Path) -> None:
    rows = [f"{sha256_file(p)}  {p.relative_to(out).as_posix()}"
            for p in sorted(out.rglob("*")) if p.is_file() and p.name != "SHA256SUMS"]
    (out / "SHA256SUMS").write_text("\n".join(rows) + "\n", encoding="utf-8")


def verify_package(root: Path) -> CheckLog:
    log = CheckLog()
    manifest = root / "SHA256SUMS"
    bad = []
    if manifest.is_file():
        listed = set()
        for line in manifest.read_text(encoding="utf-8").splitlines():
            digest, rel = line.split(None, 1)
            listed.add(rel)
            if not (root / rel).is_file() or sha256_file(root / rel) != digest:
                bad.append(rel)
        extra = [p.relative_to(root).as_posix() for p in root.rglob("*")
                 if p.is_file() and p.name != "SHA256SUMS" and p.relative_to(root).as_posix() not in listed]
        bad += [f"no listado: {e}" for e in extra]
    log.require("SHA256SUMS", manifest.is_file() and not bad, ", ".join(bad) or "todos los archivos verifican")
    check_files(package_files(root), log)
    provenance = json.loads((root / "PROVENANCE.json").read_text(encoding="utf-8")) if (root / "PROVENANCE.json").is_file() else {}
    recorded = {c["name"]: c["ok"] for c in provenance.get("checks", [])}
    for name in ("commit del firmware", "checkout limpio", "sin GPIO/UART del SIO (código)"):
        log.require(f"registrado en CI: {name}", recorded.get(name) is True, "PROVENANCE.json")
    log.require("procedencia", provenance.get("firmware_source_commit") == provenance.get("baseline_commit")
                and provenance.get("elf_sha256") == log.facts.get("elf_sha256"),
                f"firmware={provenance.get('firmware_source_commit')}")
    return log


def readme(p: dict) -> str:
    d = p["app_desc"]
    return f"""# RetroBridge Chile: firmware de diagnóstico para FujiNet Atari FN32ROV-1.3/1.5

Paquete generado por GitHub Actions. **No flashear sin autorización explícita** ni sin un respaldo verificado de la
flash (docs/hardware/procedimiento-instalacion-recuperacion.md).

| Dato | Valor |
|---|---|
| Perfil | `{p['board_profile']}` (microSD por SPI3 en GPIO 18/23/19/5; sin pines SIO) |
| Commit del firmware | `{p['firmware_source_commit']}` |
| Commit del workflow | `{p['workflow_commit']}` |
| Ejecución | {p['run_url']} (intento {p['run_attempt']}) |
| Versión (esp_app_desc) | `{d['version']}` — ESP-IDF `{d['idf_ver']}`, compilado {d['date']} {d['time']} |
| SHA-256 del ELF | `{p['elf_sha256']}` |
| Aplicación / factory | {p['app_size']} B de {p['factory_size']} B (libres {p['factory_free']} B) |

Archivos para el flasheo, con direcciones: `flash/flash_args.paquete` (rutas relativas a este paquete). Flash DIO,
40 MHz, 16 MB.

Verificación local sin hardware:

```bash
sha256sum -c SHA256SUMS
python tools/ci/firmware_package.py verify <este directorio>
```
"""


def source_state(src: Path) -> SourceState:
    def git(*args):
        return subprocess.run(["git", "-C", str(src), *args], capture_output=True, text=True, check=True).stdout.strip()
    trees = {}
    for path in IDENTITY_PATHS:
        try:
            trees[path] = git("rev-parse", f"HEAD:{path}")
        except subprocess.CalledProcessError:
            trees[path] = "ausente"
    return SourceState(head=git("rev-parse", "HEAD"), dirty=bool(git("status", "--porcelain")), trees=trees)


def main(argv=None) -> int:
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    sub = parser.add_subparsers(dest="command", required=True)
    p = sub.add_parser("package")
    p.add_argument("--src", type=Path, required=True)
    p.add_argument("--build", type=Path, required=True)
    p.add_argument("--reports", type=Path, required=True)
    p.add_argument("--out", type=Path, required=True)
    p.add_argument("--baseline", required=True)
    v = sub.add_parser("verify")
    v.add_argument("package_dir", type=Path)
    args = parser.parse_args(argv)

    if args.command == "package":
        run = {k: v for k, v in os.environ.items() if k.startswith("GITHUB_")}
        log = package(args.src, args.build, args.reports, args.out, source_state(args.src), args.baseline, run)
    else:
        log = verify_package(args.package_dir)
    sys.stdout.write(log.text())
    return 0 if log.ok else 1


if __name__ == "__main__":
    sys.exit(main())
