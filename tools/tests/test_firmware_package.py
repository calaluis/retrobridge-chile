"""Self-tests for tools/ci/firmware_package.py, on synthetic build outputs.

Run with: python -m unittest discover -s tools/tests
"""

import hashlib
import json
import struct
import sys
import tempfile
import unittest
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent.parent / "ci"))

import firmware_package as fp  # noqa: E402

BASELINE = "1b1527d2dfd29a5f66471cbb2a14a3d49585dd8e"
ELF_BYTES = b"\x7fELF synthetic retrobridge elf" * 100

PARTITIONS_CSV = """# Name,   Type, SubType, Offset,   Size
nvs,      data, nvs,     0x9000,   0x6000
phy_init, data, phy,     0xf000,   0x1000
factory,  app,  factory, 0x10000,  0x400000
"""

SDKCONFIG = """CONFIG_IDF_TARGET="esp32"
CONFIG_IDF_TARGET_ESP32=y
CONFIG_RB_BOARD_FUJINET_ATARI_V1X=y
# CONFIG_RB_BOARD_UNCONFIRMED is not set
CONFIG_ESPTOOLPY_FLASHMODE_DIO=y
CONFIG_ESPTOOLPY_FLASHMODE="dio"
CONFIG_ESPTOOLPY_FLASHFREQ_40M=y
CONFIG_ESPTOOLPY_FLASHFREQ="40m"
CONFIG_ESPTOOLPY_FLASHSIZE_16MB=y
CONFIG_ESPTOOLPY_FLASHSIZE="16MB"
CONFIG_PARTITION_TABLE_CUSTOM=y
CONFIG_PARTITION_TABLE_CUSTOM_FILENAME="partitions/retrobridge-16mb.csv"
CONFIG_PARTITION_TABLE_OFFSET=0x8000
CONFIG_ESP_CONSOLE_UART_DEFAULT=y
CONFIG_ESP_CONSOLE_UART_NUM=0
"""


def image_header(mode=2, speed=0, size=4, chip=0, segments=1) -> bytes:
    return struct.pack("<BBBBIB3sHBHH4sB", 0xE9, segments, mode, (size << 4) | speed,
                       0x40080000, 0xEE, b"\0\0\0", chip, 0, 0, 0xFFFF, b"\0" * 4, 1)


def app_image(version="1b1527d", elf=ELF_BYTES, project="retrobridge", idf="v6.1", size=0x30000, extra=b"",
              **header) -> bytes:
    desc = struct.pack("<II8s32s32s16s16s32s32s", 0xABCD5432, 0, b"\0" * 8, version.encode(), project.encode(),
                       b"12:00:00", b"Oct 24 2026", idf.encode(), hashlib.sha256(elf).digest())
    desc += b"\0" * (256 - len(desc))
    body = image_header(**header) + struct.pack("<II", 0x3F400020, len(desc)) + desc + extra
    return body + b"\xA5" * (size - len(body))


# H4 diagnostic firmware (variant "diag"): commit and the bytes that only exist when rb_hw_diag is linked in.
H4_COMMIT = "15b153f267cf6766978bd786eb1b463ea0e4ac50"
DIAG_APP_MARKERS = b"\0DIAG BEGIN format=%d source=retrobridge-hw-diag\0usage: diag status|memory|storage|events|report\0"
DIAG_ELF = ELF_BYTES + b"\0_ZN2rb6hwdiag12ReportWriter11writeReportE\0_ZN2rb3esp14EspSystemProbe12registerBootEv\0"


def partition_table(entries, corrupt_md5=False) -> bytes:
    blob = b"".join(struct.pack("<2sBBII16sI", b"\xAA\x50", t, s, o, n, name.encode(), 0)
                    for name, t, s, o, n in entries)
    md5 = hashlib.md5(blob).digest()
    if corrupt_md5:
        md5 = bytes(16)
    blob += b"\xEB\xEB" + b"\xFF" * 14 + md5
    return blob + b"\xFF" * (0xC00 - len(blob))


GOOD_ENTRIES = [("nvs", 1, 2, 0x9000, 0x6000), ("phy_init", 1, 1, 0xF000, 0x1000),
                ("factory", 0, 0, 0x10000, 0x400000)]


def write(path: Path, data) -> None:
    path.parent.mkdir(parents=True, exist_ok=True)
    if isinstance(data, str):
        path.write_text(data, encoding="utf-8")
    else:
        path.write_bytes(data)


class Fixture:
    """A synthetic firmware checkout and build directory that passes every check."""

    def __init__(self, root: Path):
        self.src = root / "src"
        self.build = self.src / "build-fujinet"
        self.reports = root / "reports"
        self.out = root / "out"
        write(self.src / "main" / "app_main.cpp", "// diagnostic firmware\nint main_stub();\n")
        write(self.src / "components" / "rb_core" / "core.cpp", "// core\n")
        write(self.src / "partitions" / "retrobridge-16mb.csv", PARTITIONS_CSV)
        write(self.build / "sdkconfig", SDKCONFIG)
        write(self.build / "bootloader" / "bootloader.bin", image_header() + b"\0" * 0x6000)
        write(self.build / "partition_table" / "partition-table.bin", partition_table(GOOD_ENTRIES))
        write(self.build / "retrobridge.bin", app_image())
        write(self.build / "retrobridge.elf", ELF_BYTES)
        self.flasher_args({"0x1000": "bootloader/bootloader.bin", "0x8000": "partition_table/partition-table.bin",
                           "0x10000": "retrobridge.bin"})
        write(self.build / "flash_args", "--flash-mode dio --flash-freq 40m --flash-size 16MB\n"
              "0x1000 bootloader/bootloader.bin\n0x10000 retrobridge.bin\n0x8000 partition_table/partition-table.bin\n")
        write(self.build / "project_description.json", json.dumps({
            "project_name": "retrobridge", "project_version": "1b1527d", "target": "esp32",
            "app_bin": "retrobridge.bin", "app_elf": "retrobridge.elf", "git_revision": "v6.1"}))
        write(self.reports / "size.txt", "Total image size: 196608 bytes\n")
        write(self.reports / "size-components.txt", "per component\n")
        self.source = fp.SourceState(head=BASELINE, dirty=False, trees={"main": "a" * 40})

    def flasher_args(self, files, mode="dio", size="16MB", freq="40m", chip="esp32"):
        write(self.build / "flasher_args.json", json.dumps({
            "write_flash_args": ["--flash-mode", mode, "--flash-size", size, "--flash-freq", freq],
            "flash_settings": {"flash_mode": mode, "flash_size": size, "flash_freq": freq},
            "flash_files": files,
            "extra_esptool_args": {"after": "hard_reset", "before": "default_reset", "stub": True, "chip": chip}}))

    def check(self):
        return fp.check_build(self.src, self.build, self.source, BASELINE)


class FirmwarePackageTest(unittest.TestCase):
    def setUp(self):
        self._tmp = tempfile.TemporaryDirectory()
        self.f = Fixture(Path(self._tmp.name))

    def tearDown(self):
        self._tmp.cleanup()

    def failed(self, log):
        return [c.name for c in log.checks if not c.ok]

    def assert_fails(self, check_name):
        log = self.f.check()
        self.assertFalse(log.ok)
        self.assertIn(check_name, self.failed(log))

    def test_good_build_passes_every_check(self):
        log = self.f.check()
        self.assertEqual(self.failed(log), [])
        self.assertTrue(log.ok)

    def test_package_contents_and_manifest(self):
        log = fp.package(self.f.src, self.f.build, self.f.reports, self.f.out, self.f.source, BASELINE, run={})
        self.assertTrue(log.ok, self.failed(log))
        for rel in ("firmware/bootloader.bin", "firmware/partition-table.bin", "firmware/retrobridge.bin",
                    "firmware/retrobridge.elf", "flash/flash_args", "flash/flasher_args.json",
                    "flash/flash_args.paquete", "config/sdkconfig", "config/retrobridge-16mb.csv",
                    "config/project_description.json", "config/partition-table.decoded.txt",
                    "reports/size.txt", "reports/size-components.txt", "reports/checks.txt",
                    "PROVENANCE.json", "SOURCE_IDENTITY.txt", "LEEME.md", "SHA256SUMS"):
            self.assertTrue((self.f.out / rel).is_file(), rel)
        bins = sorted(p.name for p in self.f.out.rglob("*.bin"))
        self.assertEqual(bins, ["bootloader.bin", "partition-table.bin", "retrobridge.bin"])
        provenance = json.loads((self.f.out / "PROVENANCE.json").read_text(encoding="utf-8"))
        self.assertEqual(provenance["firmware_source_commit"], BASELINE)
        self.assertEqual(provenance["app_desc"]["version"], "1b1527d")
        self.assertIn("0x10000 firmware/retrobridge.bin", (self.f.out / "flash/flash_args.paquete").read_text())

    def test_verify_accepts_the_package_and_detects_tampering(self):
        fp.package(self.f.src, self.f.build, self.f.reports, self.f.out, self.f.source, BASELINE, run={})
        self.assertTrue(fp.verify_package(self.f.out).ok)
        app = self.f.out / "firmware" / "retrobridge.bin"
        app.write_bytes(app.read_bytes()[:-1] + b"\x00")
        log = fp.verify_package(self.f.out)
        self.assertFalse(log.ok)
        self.assertIn("SHA256SUMS", self.failed(log))

    def test_package_not_written_when_a_check_fails(self):
        write(self.f.build / "sdkconfig", SDKCONFIG.replace("CONFIG_RB_BOARD_FUJINET_ATARI_V1X=y",
                                                            "CONFIG_RB_BOARD_UNCONFIRMED=y"))
        log = fp.package(self.f.src, self.f.build, self.f.reports, self.f.out, self.f.source, BASELINE, run={})
        self.assertFalse(log.ok)
        self.assertFalse((self.f.out / "SHA256SUMS").exists())

    def test_unconfirmed_profile_is_rejected(self):
        write(self.f.build / "sdkconfig", SDKCONFIG.replace("CONFIG_RB_BOARD_FUJINET_ATARI_V1X=y",
                                                            "CONFIG_RB_BOARD_UNCONFIRMED=y"))
        self.assert_fails("perfil FujiNet")

    def test_other_target_is_rejected(self):
        write(self.f.build / "sdkconfig", SDKCONFIG.replace('"esp32"', '"esp32s3"'))
        self.assert_fails("plataforma ESP32")

    def test_flash_mode_in_image_header_is_checked(self):
        write(self.f.build / "retrobridge.bin", app_image(mode=0))
        self.assert_fails("cabecera aplicación")

    def test_flash_size_in_bootloader_header_is_checked(self):
        write(self.f.build / "bootloader" / "bootloader.bin", image_header(size=2) + b"\0" * 0x6000)
        self.assert_fails("cabecera bootloader")

    def test_flasher_args_settings_are_checked(self):
        self.f.flasher_args({"0x1000": "bootloader/bootloader.bin", "0x8000": "partition_table/partition-table.bin",
                             "0x10000": "retrobridge.bin"}, freq="80m")
        self.assert_fails("flasher_args.json")

    def test_factory_must_be_4_mib(self):
        entries = GOOD_ENTRIES[:2] + [("factory", 0, 0, 0x10000, 0x300000)]
        write(self.f.build / "partition_table" / "partition-table.bin", partition_table(entries))
        self.assert_fails("partición factory")

    def test_partition_binary_must_match_csv(self):
        entries = GOOD_ENTRIES[:1] + [("phy_init", 1, 1, 0xE000, 0x1000)] + GOOD_ENTRIES[2:]
        write(self.f.build / "partition_table" / "partition-table.bin", partition_table(entries))
        self.assert_fails("tabla = CSV")

    def test_partition_md5_is_checked(self):
        write(self.f.build / "partition_table" / "partition-table.bin", partition_table(GOOD_ENTRIES, True))
        self.assert_fails("tabla MD5")

    def test_app_must_fit_its_partition(self):
        write(self.f.build / "retrobridge.bin", app_image(size=0x400001))
        self.assert_fails("espacio en factory")

    def test_low_free_space_is_only_a_warning(self):
        write(self.f.build / "retrobridge.bin", app_image(size=0x3C0000))
        log = self.f.check()
        self.assertTrue(log.ok)
        self.assertTrue(any("menos del 10" in w for w in log.warnings))

    def test_offsets_must_match_the_table(self):
        self.f.flasher_args({"0x1000": "bootloader/bootloader.bin", "0x8000": "partition_table/partition-table.bin",
                             "0x20000": "retrobridge.bin"})
        self.assert_fails("direcciones")

    def test_elf_sha256_must_match_app_desc(self):
        write(self.f.build / "retrobridge.elf", ELF_BYTES + b"changed")
        self.assert_fails("SHA-256 del ELF")

    def test_app_version_must_match_esp_idf_project_version(self):
        write(self.f.build / "retrobridge.bin", app_image(version="otra"))
        self.assert_fails("versión de la aplicación")

    def test_version_is_not_required_to_be_the_short_commit(self):
        write(self.f.build / "retrobridge.bin", app_image(version="v0.3-dirty-xyz"))
        desc = json.loads((self.f.build / "project_description.json").read_text())
        desc["project_version"] = "v0.3-dirty-xyz"
        write(self.f.build / "project_description.json", json.dumps(desc))
        self.assertTrue(self.f.check().ok)

    def test_firmware_checkout_must_be_the_pinned_commit(self):
        self.f.source = fp.SourceState(head="0" * 40, dirty=False, trees={})
        self.assert_fails("commit del firmware")

    def test_firmware_checkout_must_be_clean(self):
        self.f.source = fp.SourceState(head=BASELINE, dirty=True, trees={})
        self.assert_fails("checkout limpio")

    def test_sio_gpio_call_in_firmware_source_is_rejected(self):
        write(self.f.src / "components" / "rb_sio" / "uart.cpp", "void f() { uart_driver_install(UART_NUM_1, 256, 0, 0, 0, 0); }\n")
        self.assert_fails("sin GPIO/UART del SIO (código)")

    def test_tests_may_mention_gpio_calls(self):
        write(self.f.src / "components" / "rb_sio" / "test" / "fake.cpp", "void f() { gpio_set_level(4, 1); }\n")
        self.assertTrue(self.f.check().ok)

    def test_sio_option_in_sdkconfig_is_rejected(self):
        write(self.f.build / "sdkconfig", SDKCONFIG + "CONFIG_RB_SIO_ENABLE=y\n")
        self.assert_fails("sin GPIO/UART del SIO (configuración)")

    def test_console_must_stay_on_uart0(self):
        write(self.f.build / "sdkconfig", SDKCONFIG.replace("CONFIG_ESP_CONSOLE_UART_NUM=0", "CONFIG_ESP_CONSOLE_UART_NUM=1"))
        self.assert_fails("sin GPIO/UART del SIO (configuración)")

    def test_flash_args_paths_must_exist_in_the_build(self):
        (self.f.build / "retrobridge.bin").rename(self.f.build / "otro.bin")
        self.assert_fails("archivos presentes")

    def test_secret_in_published_text_is_rejected(self):
        write(self.f.build / "sdkconfig", SDKCONFIG + 'CONFIG_EXAMPLE_WIFI_PASSWORD="hunter2"\n')
        self.assert_fails("sin secretos")


class PackageVariantTest(unittest.TestCase):
    """H3c (default) and H4 diag packages: separate identities, and the diag one proves rb_hw_diag is inside."""

    def setUp(self):
        self._tmp = tempfile.TemporaryDirectory()
        self.f = Fixture(Path(self._tmp.name))

    def tearDown(self):
        self._tmp.cleanup()

    def make_h4_build(self, markers=True, elf_markers=True, version="15b153f"):
        write(self.f.build / "retrobridge.elf", DIAG_ELF if elf_markers else ELF_BYTES)
        elf = DIAG_ELF if elf_markers else ELF_BYTES
        write(self.f.build / "retrobridge.bin",
              app_image(version=version, elf=elf, extra=DIAG_APP_MARKERS if markers else b""))
        desc = json.loads((self.f.build / "project_description.json").read_text())
        desc["project_version"] = version
        write(self.f.build / "project_description.json", json.dumps(desc))
        self.f.source = fp.SourceState(head=H4_COMMIT, dirty=False, trees={"main": "b" * 40})

    def package(self, variant):
        return fp.package(self.f.src, self.f.build, self.f.reports, self.f.out, self.f.source,
                          self.f.source.head, run={}, variant=variant)

    def failed(self, log):
        return [c.name for c in log.checks if not c.ok]

    def test_variants_have_distinct_names(self):
        self.assertEqual(fp.VARIANTS["h3c"].package, "retrobridge-fw-fujinet-atari-v1x")
        self.assertEqual(fp.VARIANTS["diag"].package, "retrobridge-fw-diag-fujinet-atari-v1x")
        self.assertEqual(len({v.package for v in fp.VARIANTS.values()}), len(fp.VARIANTS))

    def test_h3c_default_keeps_its_original_identity(self):
        log = fp.package(self.f.src, self.f.build, self.f.reports, self.f.out, self.f.source, BASELINE, run={})
        self.assertTrue(log.ok, self.failed(log))
        provenance = json.loads((self.f.out / "PROVENANCE.json").read_text(encoding="utf-8"))
        self.assertEqual(provenance["package"], "retrobridge-fw-fujinet-atari-v1x")
        self.assertEqual(provenance["firmware_source_commit"], BASELINE)
        self.assertNotIn("variant", provenance)
        self.assertNotIn("features", provenance)
        self.assertNotIn("hw_diag", [c["name"] for c in provenance["checks"]])
        self.assertIn("# RetroBridge Chile: firmware de diagnóstico para FujiNet Atari FN32ROV-1.3/1.5",
                      (self.f.out / "LEEME.md").read_text(encoding="utf-8"))

    def test_diag_package_identity_points_to_h4(self):
        self.make_h4_build()
        log = self.package("diag")
        self.assertTrue(log.ok, self.failed(log))
        provenance = json.loads((self.f.out / "PROVENANCE.json").read_text(encoding="utf-8"))
        self.assertEqual(provenance["package"], "retrobridge-fw-diag-fujinet-atari-v1x")
        self.assertEqual(provenance["variant"], "diag")
        self.assertEqual(provenance["firmware_source_commit"], H4_COMMIT)
        self.assertEqual(provenance["app_desc"]["version"], "15b153f")
        self.assertEqual(provenance["features"], ["hw_diag"])
        self.assertIn(f"firmware_source_commit {H4_COMMIT}", (self.f.out / "SOURCE_IDENTITY.txt").read_text())
        self.assertIn("diagnóstico autónomo", (self.f.out / "LEEME.md").read_text(encoding="utf-8"))
        self.assertTrue(fp.verify_package(self.f.out).ok)

    def test_diag_package_requires_rb_hw_diag_in_the_application(self):
        self.make_h4_build(markers=False)
        log = self.package("diag")
        self.assertFalse(log.ok)
        self.assertIn("diagnóstico incorporado (aplicación)", self.failed(log))
        self.assertFalse((self.f.out / "SHA256SUMS").exists())

    def test_diag_package_requires_rb_hw_diag_symbols_in_the_elf(self):
        self.make_h4_build(elf_markers=False)
        log = self.package("diag")
        self.assertIn("diagnóstico incorporado (ELF)", self.failed(log))

    def test_an_h3c_build_cannot_be_packaged_as_diag(self):
        log = self.package("diag")  # H3c fixture: no markers, version 1b1527d, commit 1b1527d
        self.assertFalse(log.ok)
        self.assertIn("diagnóstico incorporado (aplicación)", self.failed(log))

    def test_diag_version_must_be_a_prefix_of_the_pinned_commit(self):
        self.make_h4_build(version="1b1527d")
        log = self.package("diag")
        self.assertIn("versión = commit fijado", self.failed(log))

    def test_diag_version_with_dirty_suffix_is_rejected(self):
        self.make_h4_build(version="15b153f-dirty")
        log = self.package("diag")
        self.assertIn("versión = commit fijado", self.failed(log))

    def test_verify_rechecks_the_diag_markers(self):
        self.make_h4_build()
        self.assertTrue(self.package("diag").ok)
        app = self.f.out / "firmware" / "retrobridge.bin"
        app.write_bytes(app.read_bytes().replace(b"retrobridge-hw-diag", b"retrobridge-xx-diag"))
        fp.write_manifest(self.f.out)  # even with a consistent manifest, the content check fails
        log = fp.verify_package(self.f.out)
        self.assertFalse(log.ok)
        self.assertIn("diagnóstico incorporado (aplicación)", self.failed(log))

    def test_unknown_variant_is_refused(self):
        with self.assertRaises(KeyError):
            self.package("cualquiera")


if __name__ == "__main__":
    unittest.main()
