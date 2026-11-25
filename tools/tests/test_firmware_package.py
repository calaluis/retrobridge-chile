"""Self-tests for tools/ci/firmware_package.py, on synthetic build outputs.

Run with: python -m unittest discover -s tools/tests
"""

import hashlib
import json
import re
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


# C107 cassette firmware (variant "cassette"): drives DATA IN by UART2 and RMT, reads MOTOR. Its package needs H4
# and M3a approved twice: in the board profile compiled in, and in the signed gate record.
CASSETTE_COMMIT = "7621040c0ffee0000000000000000000000000ab"
CASSETTE_APP_MARKERS = (b"\0\x1b[0;32mI (%lu) %s: DATA IN on GPIO%d: records by UART2, FSK by RMT\x1b[0m\n\0"
                        b"\x1b[0;32mI (%lu) %s: cassette ready: MOTOR %s, DATA IN %s\x1b[0m\n\0")
CASSETTE_ELF = (ELF_BYTES + b"\0_ZN2rb3esp9EspDataIn9configureERKNS_5board24CassetteOutputAssignmentE\0"
                b"_ZN2rb3esp15CassetteService5startEv\0")
SET_PIN = "(void)uart_set_pin(kUart, pin_, UART_PIN_NO_CHANGE, UART_PIN_NO_CHANGE, UART_PIN_NO_CHANGE);\n"
GPIO_RESET = "(void)gpio_reset_pin(static_cast<gpio_num_t>(pin_));\n"


def cassette_source(path, calls=None):
    """Synthetic C107 source using each allowed name exactly as often as the list says."""
    lines = ['#include "driver/uart.h"  // gpio_set_level( in a comment and "uart_set_pin(" in a string are ignored\n']
    if path.endswith("esp_data_in.cpp"):
        lines.append("constexpr uart_port_t kUart = UART_NUM_2;\nconst char* kTag = \"uart_write_bytes(\";\n")
        lines.append("void f(rmt_tx_channel_config_t config) { config.gpio_num = pin_; }\n")
    for file, name, count in (fp.CASSETTE_SIO_CALLS if calls is None else calls):
        if file == path:
            line = {"uart_set_pin": SET_PIN, "gpio_reset_pin": GPIO_RESET}.get(name, f"(void){name}(kUart, pin_);\n")
            lines += [line] * count
    return "".join(lines)


ESP_DATA_IN = cassette_source("components/rb_esp_cassette/src/esp_data_in.cpp")
CASSETTE_SERVICE = cassette_source("components/rb_esp_cassette/src/cassette_service.cpp")
BOARD_PROFILE = """struct BoardProfile {
    const char* name;
    bool hardwareConfirmed;  // a comment; with a semicolon
    bool hasPsram;
    SdInterface sdInterface;
    SdSpiPins spi;
    SpiHost spiHost;
    int8_t cardDetect;
    bool sioGateApproved;
    SioPins sio;
    int8_t motor;
    bool motorGateApproved;
};
"""
PROFILE_HEADER = """constexpr BoardProfile fujinet_atari_v1x_profile() noexcept
{
    return BoardProfile{"fujinet-atari-v1.3/1.5",
                        true,  // asserted by whoever selects the profile (Kconfig help)
                        true,  // ESP32-WROVER module: PSRAM on GPIO16/17
                        SdInterface::Spi,
                        SdSpiPins{18, 23, 19, 5},
                        SpiHost::Spi3,  // VSPI: the native controller for these pins
                        kNoPin,
                        %s,  // H4
                        SioPins{39, 33, 21},
                        36,      // MOTOR: GPIO36 (SENSOR_VP), straight from SIO pin 8
                        %s};  // M3a
}
"""


def gate_record(h4=True, m3a=True):
    return {"H4": {"aprobada": h4, "fecha": "2026-11-22", "registro": "docs/mediciones/h4-prueba.md"},
            "M3a": {"aprobada": m3a, "fecha": "2026-11-22", "registro": "docs/mediciones/m3a-prueba.md"}}


class CassettePackageTest(unittest.TestCase):
    """The cassette package: SIO calls only where C107 needs them, and only with H4 and M3a approved."""

    def setUp(self):
        self._tmp = tempfile.TemporaryDirectory()
        self.f = Fixture(Path(self._tmp.name))
        src = self.f.src
        write(src / "components" / "rb_esp_cassette" / "src" / "esp_data_in.cpp", ESP_DATA_IN)
        write(src / "components" / "rb_esp_cassette" / "src" / "cassette_service.cpp", CASSETTE_SERVICE)
        write(src / "components" / "rb_board" / "include" / "rb" / "board" / "board_profile.hpp", BOARD_PROFILE)
        self.profile(True, True)
        write(src / "docs" / "mediciones" / "h4-prueba.md", "# H4 (datos de prueba)\n")
        write(src / "docs" / "mediciones" / "m3a-prueba.md", "# M3a (datos de prueba)\n")
        self.gates(gate_record())
        write(self.f.build / "retrobridge.elf", CASSETTE_ELF)
        write(self.f.build / "retrobridge.bin", app_image(version="7621040", elf=CASSETTE_ELF, extra=CASSETTE_APP_MARKERS))
        desc = json.loads((self.f.build / "project_description.json").read_text())
        desc["project_version"] = "7621040"
        write(self.f.build / "project_description.json", json.dumps(desc))
        self.f.source = fp.SourceState(head=CASSETTE_COMMIT, dirty=False, trees={"main": "c" * 40})

    def tearDown(self):
        self._tmp.cleanup()

    def profile(self, h4, m3a):
        write(self.f.src / "components" / "rb_board" / "include" / "rb" / "board" / "fujinet_atari_v1x.hpp",
              PROFILE_HEADER % ("true" if h4 else "false", "true" if m3a else "false"))

    def gates(self, record):
        write(self.f.src / "docs" / "mediciones" / "puertas.json", json.dumps(record))

    def package(self, variant="cassette"):
        return fp.package(self.f.src, self.f.build, self.f.reports, self.f.out, self.f.source,
                          self.f.source.head, run={}, variant=variant)

    def failed(self, log):
        return [c.name for c in log.checks if not c.ok]

    def assert_refused(self, check_name, variant="cassette"):
        log = self.package(variant)
        self.assertFalse(log.ok)
        self.assertIn(check_name, self.failed(log))
        self.assertFalse(self.f.out.exists())  # nothing written

    # positive case (test data; the real profile and record are checked at the end)

    def test_cassette_package_with_both_gates_approved(self):
        log = self.package()
        self.assertTrue(log.ok, self.failed(log))
        provenance = json.loads((self.f.out / "PROVENANCE.json").read_text(encoding="utf-8"))
        self.assertEqual(provenance["package"], "retrobridge-fw-cassette-fujinet-atari-v1x")
        self.assertEqual(provenance["variant"], "cassette")
        self.assertEqual(provenance["features"], ["cassette"])
        self.assertEqual(provenance["gates"], gate_record())
        self.assertEqual(json.loads((self.f.out / "config" / "puertas.json").read_text(encoding="utf-8")), gate_record())
        self.assertIn("DATA IN", (self.f.out / "LEEME.md").read_text(encoding="utf-8"))
        log = fp.verify_package(self.f.out)
        self.assertTrue(log.ok, self.failed(log))

    # either approval missing, at generation

    def test_h4_not_signed_in_the_profile_is_refused(self):
        self.profile(False, True)
        self.assert_refused("puertas H4 y M3a (perfil)")

    def test_m3a_not_signed_in_the_profile_is_refused(self):
        self.profile(True, False)
        self.assert_refused("puertas H4 y M3a (perfil)")

    def test_profile_that_cannot_be_read_is_refused(self):
        write(self.f.src / "components" / "rb_board" / "include" / "rb" / "board" / "fujinet_atari_v1x.hpp",
              "constexpr BoardProfile fujinet_atari_v1x_profile() noexcept { return kApproved; }\n")
        self.assert_refused("puertas H4 y M3a (perfil)")

    def test_profile_missing_is_refused(self):
        (self.f.src / "components" / "rb_board" / "include" / "rb" / "board" / "fujinet_atari_v1x.hpp").unlink()
        self.assert_refused("puertas H4 y M3a (perfil)")

    def test_h4_not_approved_in_the_record_is_refused(self):
        self.gates(gate_record(h4=False))
        self.assert_refused("puertas H4 y M3a (registro)")

    def test_m3a_not_approved_in_the_record_is_refused(self):
        self.gates(gate_record(m3a=False))
        self.assert_refused("puertas H4 y M3a (registro)")

    def test_approval_must_be_the_json_true(self):
        record = gate_record()
        record["M3a"]["aprobada"] = "sí"
        self.gates(record)
        self.assert_refused("puertas H4 y M3a (registro)")

    def test_a_gate_missing_from_the_record_is_refused(self):
        record = gate_record()
        del record["H4"]
        self.gates(record)
        self.assert_refused("puertas H4 y M3a (registro)")

    def test_measurement_record_must_exist(self):
        (self.f.src / "docs" / "mediciones" / "m3a-prueba.md").unlink()
        self.assert_refused("puertas H4 y M3a (registro)")

    def test_measurement_record_must_stay_in_docs_mediciones(self):
        record = gate_record()
        record["H4"]["registro"] = "main/app_main.cpp"
        self.gates(record)
        self.assert_refused("puertas H4 y M3a (registro)")

    def test_gate_record_missing_is_refused(self):
        (self.f.src / "docs" / "mediciones" / "puertas.json").unlink()
        self.assert_refused("puertas H4 y M3a (registro)")

    def test_gate_record_not_json_is_refused(self):
        write(self.f.src / "docs" / "mediciones" / "puertas.json", "{H4: aprobada}")
        self.assert_refused("puertas H4 y M3a (registro)")

    # the exception is only the calls C107 needs

    def test_sio_call_outside_rb_esp_cassette_is_refused(self):
        write(self.f.src / "components" / "rb_sio" / "uart.cpp", "void f() { uart_set_pin(UART_NUM_2, 21, -1, -1, -1); }\n")
        self.assert_refused("GPIO/UART del SIO solo en rb_esp_cassette (código)")

    def test_one_more_call_in_esp_data_in_is_refused(self):
        write(self.f.src / "components" / "rb_esp_cassette" / "src" / "esp_data_in.cpp",
              ESP_DATA_IN + "void g() { uart_set_pin(kUart, 22, -1, -1, -1); }\n")
        self.assert_refused("GPIO/UART del SIO solo en rb_esp_cassette (código)")

    def test_a_call_not_in_the_list_is_refused(self):
        write(self.f.src / "components" / "rb_esp_cassette" / "src" / "esp_data_in.cpp",
              ESP_DATA_IN + "void g() { gpio_set_level(21, 0); }\n")
        self.assert_refused("GPIO/UART del SIO solo en rb_esp_cassette (código)")

    def test_the_allowed_call_in_another_file_of_the_component_is_refused(self):
        write(self.f.src / "components" / "rb_esp_cassette" / "src" / "other.cpp", "void g() { gpio_config(&c); }\n")
        self.assert_refused("GPIO/UART del SIO solo en rb_esp_cassette (código)")

    def test_cassette_feature_must_be_linked_in(self):
        write(self.f.build / "retrobridge.bin", app_image(version="7621040", elf=CASSETTE_ELF))
        self.assert_refused("casetera incorporada (aplicación)")

    def test_cassette_symbols_must_be_in_the_elf(self):
        write(self.f.build / "retrobridge.elf", ELF_BYTES)
        write(self.f.build / "retrobridge.bin", app_image(version="7621040", extra=CASSETTE_APP_MARKERS))
        self.assert_refused("casetera incorporada (ELF)")

    def test_cassette_version_must_be_a_prefix_of_the_pinned_commit(self):
        self.f.source = fp.SourceState(head=BASELINE, dirty=False, trees={})
        self.assert_refused("versión = commit fijado")

    # either approval missing, at verification

    def test_verify_refuses_a_record_changed_to_unapproved(self):
        self.assertTrue(self.package().ok)
        write(self.f.out / "config" / "puertas.json", json.dumps(gate_record(m3a=False)))
        fp.write_manifest(self.f.out)  # a consistent manifest does not help
        log = fp.verify_package(self.f.out)
        self.assertFalse(log.ok)
        self.assertIn("puertas H4 y M3a (registro)", self.failed(log))

    def test_verify_refuses_a_record_that_differs_from_provenance(self):
        self.assertTrue(self.package().ok)
        record = gate_record()
        record["H4"]["fecha"] = "2030-01-01"
        write(self.f.out / "config" / "puertas.json", json.dumps(record))
        fp.write_manifest(self.f.out)
        self.assertIn("puertas H4 y M3a (registro)", self.failed(fp.verify_package(self.f.out)))

    def test_verify_refuses_a_missing_record(self):
        self.assertTrue(self.package().ok)
        (self.f.out / "config" / "puertas.json").unlink()
        fp.write_manifest(self.f.out)
        self.assertIn("puertas H4 y M3a (registro)", self.failed(fp.verify_package(self.f.out)))

    def test_verify_requires_the_profile_check_recorded(self):
        self.assertTrue(self.package().ok)
        path = self.f.out / "PROVENANCE.json"
        provenance = json.loads(path.read_text(encoding="utf-8"))
        provenance["checks"] = [c for c in provenance["checks"] if c["name"] != "puertas H4 y M3a (perfil)"]
        path.write_text(json.dumps(provenance), encoding="utf-8")
        fp.write_manifest(self.f.out)
        self.assertIn("registrado en CI: puertas H4 y M3a (perfil)", self.failed(fp.verify_package(self.f.out)))

    def test_verify_requires_the_restricted_code_check_recorded(self):
        self.assertTrue(self.package().ok)
        path = self.f.out / "PROVENANCE.json"
        provenance = json.loads(path.read_text(encoding="utf-8"))
        for c in provenance["checks"]:
            if c["name"] == "GPIO/UART del SIO solo en rb_esp_cassette (código)":
                c["ok"] = False
        path.write_text(json.dumps(provenance), encoding="utf-8")
        fp.write_manifest(self.f.out)
        self.assertIn("registrado en CI: GPIO/UART del SIO solo en rb_esp_cassette (código)",
                      self.failed(fp.verify_package(self.f.out)))

    # holes found in review: hidden profiles, other GPIO/UART/RMT uses, wrong pins, record tricks

    def test_an_approved_profile_hidden_behind_if_0_is_refused(self):
        hidden = "#if 0\n" + PROFILE_HEADER % ("true", "true") + "#endif\n" + PROFILE_HEADER % ("false", "false")
        write(self.f.src / "components" / "rb_board" / "include" / "rb" / "board" / "fujinet_atari_v1x.hpp", hidden)
        self.assert_refused("puertas H4 y M3a (perfil)")

    def test_a_define_in_the_profile_header_is_refused(self):
        write(self.f.src / "components" / "rb_board" / "include" / "rb" / "board" / "fujinet_atari_v1x.hpp",
              "#define true false\n" + PROFILE_HEADER % ("true", "true"))  # reads approved, compiles unapproved
        self.assert_refused("puertas H4 y M3a (perfil)")

    def test_a_second_profile_definition_is_refused(self):
        write(self.f.src / "components" / "rb_board" / "include" / "rb" / "board" / "fujinet_atari_v1x.hpp",
              PROFILE_HEADER % ("true", "true") + PROFILE_HEADER % ("false", "false"))
        self.assert_refused("puertas H4 y M3a (perfil)")

    def test_profile_fields_must_keep_their_order(self):
        write(self.f.src / "components" / "rb_board" / "include" / "rb" / "board" / "board_profile.hpp",
              BOARD_PROFILE.replace("    bool sioGateApproved;\n    SioPins sio;\n",
                                    "    SioPins sio;\n    bool sioGateApproved;\n"))
        self.assert_refused("puertas H4 y M3a (perfil)")

    def test_an_rmt_use_in_another_component_is_refused(self):
        write(self.f.src / "components" / "rb_sio" / "tx.cpp", "void f() { rmt_transmit(ch, enc, d, 1, &c); }\n")
        self.assert_refused("GPIO/UART del SIO solo en rb_esp_cassette (código)")

    def test_a_call_split_over_lines_is_counted(self):
        write(self.f.src / "components" / "rb_sio" / "tx.cpp", "void f() { gpio_set_level\n(21, 0); }\n")
        self.assert_refused("GPIO/UART del SIO solo en rb_esp_cassette (código)")

    def test_a_function_pointer_is_counted(self):
        write(self.f.src / "components" / "rb_sio" / "tx.cpp", "auto p = &gpio_matrix_out;\n")
        self.assert_refused("GPIO/UART del SIO solo en rb_esp_cassette (código)")

    def test_uart_set_pin_on_another_pin_is_refused(self):
        write(self.f.src / "components" / "rb_esp_cassette" / "src" / "esp_data_in.cpp",
              ESP_DATA_IN.replace(SET_PIN, SET_PIN.replace("pin_", "33"), 1))
        self.assert_refused("GPIO/UART del SIO solo en rb_esp_cassette (código)")

    def test_gpio_reset_pin_on_another_pin_is_refused(self):
        write(self.f.src / "components" / "rb_esp_cassette" / "src" / "esp_data_in.cpp",
              ESP_DATA_IN.replace(GPIO_RESET, GPIO_RESET.replace("pin_", "33"), 1))
        self.assert_refused("GPIO/UART del SIO solo en rb_esp_cassette (código)")

    def test_uart_other_than_uart2_is_refused(self):
        write(self.f.src / "components" / "rb_esp_cassette" / "src" / "esp_data_in.cpp",
              ESP_DATA_IN.replace("UART_NUM_2", "UART_NUM_0"))
        self.assert_refused("GPIO/UART del SIO solo en rb_esp_cassette (código)")

    def test_a_repeated_key_in_the_record_is_refused(self):
        text = json.dumps(gate_record(m3a=False))[:-1] + ', "M3a": ' + json.dumps(gate_record()["M3a"]) + "}"
        write(self.f.src / "docs" / "mediciones" / "puertas.json", text)
        self.assert_refused("puertas H4 y M3a (registro)")

    def test_an_impossible_date_is_refused(self):
        record = gate_record()
        record["H4"]["fecha"] = "2026-02-30"
        self.gates(record)
        self.assert_refused("puertas H4 y M3a (registro)")

    def test_the_measurement_record_must_name_its_gate(self):
        record = gate_record()
        record["M3a"]["registro"] = "docs/mediciones/h4-prueba.md"
        self.gates(record)
        self.assert_refused("puertas H4 y M3a (registro)")

    def test_the_calls_before_c8_are_accepted_as_a_whole(self):
        # The retired cassette-lectura job pinned f405106, written before H4-C C8.
        write(self.f.src / "components" / "rb_esp_cassette" / "src" / "esp_data_in.cpp",
              cassette_source("components/rb_esp_cassette/src/esp_data_in.cpp", fp.CASSETTE_SIO_CALLS_BEFORE_C8))
        self.assertTrue(fp.check_sio_calls(self.f.src, fp.CASSETTE_SIO_CALLS_BEFORE_C8)[0])
        log = self.package()
        self.assertTrue(log.ok, self.failed(log))
        detail = [c.detail for c in log.checks if c.name == "GPIO/UART del SIO solo en rb_esp_cassette (código)"]
        self.assertIn("anterior a H4-C C8", detail[0])

    def test_a_mix_of_both_call_sets_is_refused(self):
        mixed = cassette_source("components/rb_esp_cassette/src/esp_data_in.cpp", fp.CASSETTE_SIO_CALLS_BEFORE_C8)
        write(self.f.src / "components" / "rb_esp_cassette" / "src" / "esp_data_in.cpp", mixed + GPIO_RESET)
        self.assert_refused("GPIO/UART del SIO solo en rb_esp_cassette (código)")

    def test_main_code_has_exactly_the_cassette_calls(self):
        repo = Path(__file__).resolve().parents[2]
        ok, detail = fp.check_sio_calls(repo, fp.CASSETTE_SIO_CALLS, fp.MENU_SIO_SOURCES)
        self.assertTrue(ok, detail)

    # the diagnostic packages keep their controls

    def test_diag_package_still_refuses_the_cassette_calls(self):
        self.assert_refused("sin GPIO/UART del SIO (código)", variant="diag")

    def test_h3c_package_still_refuses_the_cassette_calls(self):
        self.assert_refused("sin GPIO/UART del SIO (código)", variant="h3c")

    def test_diag_and_h3c_do_not_read_the_gates(self):
        for variant in ("diag", "h3c"):
            names = [c.name for c in self.package(variant).checks]
            self.assertNotIn("puertas H4 y M3a (perfil)", names)
            self.assertNotIn("puertas H4 y M3a (registro)", names)

    def test_diag_verify_keeps_its_recorded_checks(self):
        for name in ("esp_data_in.cpp", "cassette_service.cpp"):
            (self.f.src / "components" / "rb_esp_cassette" / "src" / name).unlink()
        write(self.f.build / "retrobridge.elf", DIAG_ELF)
        write(self.f.build / "retrobridge.bin", app_image(version="15b153f", elf=DIAG_ELF, extra=DIAG_APP_MARKERS))
        desc = json.loads((self.f.build / "project_description.json").read_text())
        desc["project_version"] = "15b153f"
        write(self.f.build / "project_description.json", json.dumps(desc))
        self.f.source = fp.SourceState(head=H4_COMMIT, dirty=False, trees={})
        self.assertTrue(self.package("diag").ok)
        log = fp.verify_package(self.f.out)
        self.assertTrue(log.ok, self.failed(log))
        names = [c.name for c in log.checks]
        self.assertIn("registrado en CI: sin GPIO/UART del SIO (código)", names)
        self.assertNotIn("puertas H4 y M3a (registro)", names)

    def test_real_profile_and_record_refuse_the_cassette_package(self):
        # docs/mediciones/puertas-aceptacion-riesgo-2026-11-25.md: M3a approved by risk acceptance, H4 not
        repo = Path(__file__).resolve().parents[2]
        self.assertEqual(fp.profile_gates(repo), (False, True))
        ok, _detail, _record = fp.read_gate_record(repo)
        self.assertFalse(ok)

    def test_real_profile_and_record_allow_the_menu_sio_package(self):
        repo = Path(__file__).resolve().parents[2]
        self.assertEqual(fp.profile_gates(repo), fp.VARIANTS["menu-sio"].gates)
        ok, detail, _record = fp.read_gate_record(repo, fp.VARIANTS["menu-sio"].gates)
        self.assertTrue(ok, detail)

    def test_the_retired_variants_no_longer_exist(self):
        # spec puertas-aceptacion-riesgo CA4: they needed H4 and M3a unsigned; only menu-sio is installed before T9b
        for name in ("cassette-lectura", "bench-c8"):
            self.assertNotIn(name, fp.VARIANTS)
        self.assertEqual(fp.VARIANTS["menu-sio"].gates, (False, True))
        self.assertEqual(fp.VARIANTS["cassette"].gates, (True, True))


BENCH_APP_MARKERS = CASSETTE_APP_MARKERS + (b"\x1b[0;33mW (%lu) %s: C8 BENCH BEGIN: board '%s', H4 %s, M3a %s; USB "
                                            b"only\x1b[0m\n\0\x1b[0;32mI (%lu) %s: C8 RESULT PASS: GPIO21\x1b[0m\n\0")
BENCH_ELF = CASSETTE_ELF + b"_ZN2rb5bench6run_c8ERKNS_5board12BoardProfileERNS_3esp9EspDataInE\0"
BENCH_SOURCE = "void f() { (void)rb::board::validate_for_bench_data_in(kBoard); REG_READ(GPIO_ENABLE_REG); }\n"


class BenchC8CodeTest(unittest.TestCase):
    """The bench test C8 code stays in the tree. bench-c8 was retired with the gates approved; only bench-c8a
    (BenchC8aPackageTest) accepts it: every other variant refuses the bench option and the bench code, and the
    bench grant stays in main/bench_c8.cpp."""

    def setUp(self):
        CassettePackageTest.setUp(self)
        write(self.f.src / "main" / "bench_c8.cpp", BENCH_SOURCE)

    tearDown = CassettePackageTest.tearDown
    profile = CassettePackageTest.profile
    gates = CassettePackageTest.gates
    failed = CassettePackageTest.failed

    def package(self, variant="cassette"):
        return CassettePackageTest.package(self, variant)

    def assert_refused(self, check_name, variant="cassette"):
        CassettePackageTest.assert_refused(self, check_name, variant)

    def bench_build(self, option=True, app=BENCH_APP_MARKERS):
        write(self.f.build / "sdkconfig", SDKCONFIG + ("CONFIG_RB_BENCH_C8=y\n" if option else ""))
        write(self.f.build / "retrobridge.elf", BENCH_ELF)
        write(self.f.build / "retrobridge.bin", app_image(version="7621040", elf=BENCH_ELF, extra=app))

    def test_the_bench_source_alone_does_not_fail_the_package(self):
        log = self.package()
        self.assertTrue(log.ok, self.failed(log))

    def test_cassette_package_refuses_the_bench_option(self):
        self.bench_build()
        self.assert_refused(fp.BENCH_CHECK)

    def test_cassette_package_refuses_the_bench_code_without_the_option(self):
        self.bench_build(option=False)
        self.assert_refused(fp.BENCH_CHECK)

    def test_cassette_package_refuses_the_bench_option_without_the_code(self):
        self.bench_build(app=CASSETTE_APP_MARKERS)
        self.assert_refused(fp.BENCH_CHECK)

    def test_bench_grant_outside_the_bench_file_is_refused(self):
        write(self.f.src / "main" / "app_main.cpp", BENCH_SOURCE)
        self.assert_refused(fp.BENCH_GRANT_CHECK)

    def test_bench_grant_in_a_comment_or_string_is_ignored(self):
        write(self.f.src / "main" / "app_main.cpp",
              '// validate_for_bench_data_in\nconst char* s = "validate_for_bench_data_in";\n')
        log = self.package()
        self.assertTrue(log.ok, self.failed(log))

    def test_bench_file_must_not_write_registers(self):
        write(self.f.src / "main" / "bench_c8.cpp", BENCH_SOURCE + "void g() { REG_WRITE(GPIO_ENABLE_REG, 0); }\n")
        self.assert_refused(fp.BENCH_GRANT_CHECK)

    def test_main_code_calls_the_bench_grant_only_from_the_bench_file(self):
        repo = Path(__file__).resolve().parents[2]
        ok, detail = fp.check_bench_grant(repo)
        self.assertTrue(ok, detail)


MENU_APP_MARKERS = CASSETTE_APP_MARKERS + (b"\x1b[0;33mW (%lu) %s: menu-sio TEST PACKAGE: transmits on SIO DATA IN. H4 "
                                           b"NOT approved (R2, R7 not accepted; R6 accepted on USB power only).\x1b[0m\n\0\x1b[0;32mI (%lu) %s: D1: menu. "
                                           b"Switch the "
                                           b"65XE on.\x1b[0m\n\0")


def xtensa_elf(frames: dict) -> bytes:
    """A minimal 32-bit little-endian ELF: one .text section and one function symbol per entry of `frames`
    ({name: frame in bytes, or None for a function that does not start with `entry a1, N`})."""
    base = 0x400D0000
    text = b""
    symbols = []  # (name offset, address)
    names = b"\0"
    for name, frame in frames.items():
        symbols.append((len(names), base + len(text)))
        names += name.encode() + b"\0"
        word = (frame // 8) << 12 | 0x136 if frame is not None else 0x0020F0  # entry a1, N / a NOP
        text += bytes([word & 0xFF, word >> 8 & 0xFF, word >> 16 & 0xFF, 0])
    symtab = b"\0" * 16 + b"".join(struct.pack("<IIIBBH", at, addr, 4, 0x12, 0, 1) for at, addr in symbols)
    shstr = b"\0.text\0.symtab\0.strtab\0"
    text_off = 52
    sym_off = text_off + len(text)
    str_off = sym_off + len(symtab)
    shstr_off = str_off + len(names)
    sh_off = shstr_off + len(shstr)
    header = (b"\x7fELF" + bytes([1, 1, 1]) + b"\0" * 9 +
              struct.pack("<HHIIIIIHHHHHH", 2, 94, 1, base, 0, sh_off, 0, 52, 0, 0, 40, 5, 4))
    sections = [
        (0, 0, 0, 0, 0, 0, 0, 0, 0, 0),
        (1, 1, 6, base, text_off, len(text), 0, 0, 4, 0),          # .text PROGBITS
        (7, 2, 0, 0, sym_off, len(symtab), 3, 1, 4, 16),            # .symtab -> .strtab
        (15, 3, 0, 0, str_off, len(names), 0, 0, 1, 0),             # .strtab
        (0, 3, 0, 0, shstr_off, len(shstr), 0, 0, 1, 0),            # .shstrtab
    ]
    return (header + text + symtab + names + shstr +
            b"".join(struct.pack("<IIIIIIIIII", *s) for s in sections))


# Frames of the menu-sio stack chains (check_stack). The FatFs/SD ones are those of the 7a1ebf0 build.
FS_FRAMES = {"fopen": 48, "open": 80, "esp_vfs_open": 32, "vfs_fat_open": 48, "f_open": 112, "follow_path": 48,
             "dir_find": 32, "move_window": 32, "ff_disk_read": 32, "ff_sdmmc_read": 32, "sdmmc_read_sectors": 80,
             "sdmmc_read_sectors_dma": 112, "sdmmc_send_cmd": 32, "sdspi_host_do_transaction": 64,
             "spi_device_polling_transmit": 32, "fwrite": 48, "__bufio_put": 32, "__bufio_flush_locked": 48,
             "write": 32, "esp_vfs_write": 32, "vfs_fat_write": 64, "f_write": 64, "ff_disk_write": 32,
             "ff_sdmmc_write": 32, "sdmmc_write_sectors": 80, "sdmmc_write_sectors_dma": 128}
FINISH = "_ZN2rb7menusio6finishEbRKNS_6hwdiag10BootRecordE"
LOAD = "_ZN2rb4host13SlotFileStore4loadEv"
SAVE = "_ZN2rb4host13SlotFileStore4saveERKNS0_11DriveMemoryE"
SAVE_TASK = "_ZN2rb7menusio12_GLOBAL__N_1L13driveSaveTaskEPv"
PLAN = "_ZN2rb4host15planDriveMemoryEPKciPKNS0_13SlotFileStore10LoadResultEbRNS0_15DriveMemoryPlanE"
DECODE = "_ZN2rb4host18drive_memory_codec6decodeEPKhjRNS0_15DriveMemorySlotE"
MENU_FRAMES = {"vPortTaskWrapper": 32, "main_task": 48, "app_main": 80, FINISH: 400, LOAD: 160, PLAN: 32,
               SAVE_TASK: 80, SAVE: 112, DECODE: 48, **FS_FRAMES}
# 7a1ebf0: finish() held a ~1 KB LoadResult and load() three slot-sized buffers; app_main overflowed.
FRAMES_7A1EBF0 = {**MENU_FRAMES, FINISH: 2128, LOAD: 3136, SAVE: 2128, DECODE: 1072}
MENU_MARKERS = CASSETTE_ELF + b"_ZN2rb3esp10EspSioUart5beginEv\0_ZN2rb7menusio5startERKNS_5board12BoardProfileEb\0"
MENU_ELF = xtensa_elf(MENU_FRAMES) + MENU_MARKERS
MENU_STACKS = "CONFIG_ESP_MAIN_TASK_STACK_SIZE=3584\nCONFIG_RB_DRIVE_SAVE_STACK=6144\n"
SIO_UART_SOURCE = """#include "driver/uart.h"  // gpio_set_level( in a comment is ignored
constexpr uart_port_t kUart = UART_NUM_2;
bool begin() {
    if (gpio_set_direction(static_cast<gpio_num_t>(pins_.command), GPIO_MODE_INPUT) != ESP_OK) return false;
    if (uart_driver_install(kUart, 1024, 0, 0, nullptr, 0) != ESP_OK) return false;
    if (uart_param_config(kUart, &config) != ESP_OK || uart_set_rx_full_threshold(kUart, 1) != ESP_OK ||
        uart_set_rx_timeout(kUart, 1) != ESP_OK ||
        uart_set_pin(kUart, pins_.dataIn, pins_.dataOut, UART_PIN_NO_CHANGE, UART_PIN_NO_CHANGE) != ESP_OK ||
        gpio_pullup_en(static_cast<gpio_num_t>(pins_.dataOut)) != ESP_OK) {
        (void)uart_driver_delete(kUart);
        return false;
    }
    return true;
}
void end() { (void)uart_driver_delete(kUart); }
bool commandHigh() { return gpio_get_level(static_cast<gpio_num_t>(pins_.command)) != 0; }
bool dataOutHigh() { return gpio_get_level(static_cast<gpio_num_t>(pins_.dataOut)) != 0; }
int read() { return uart_read_bytes(kUart, out, n, ticks); }
int write() { (void)uart_write_bytes(kUart, b, n); return uart_wait_tx_done(kUart, 10); }
void flushInput() { (void)uart_flush_input(kUart); }
void idleLine() { (void)uart_wait_tx_done(kUart, 2); }
bool park() { return uart_wait_tx_done(kUart, 0) == ESP_OK; }
bool resume() {
    return uart_set_baudrate(kUart, 19200) == ESP_OK && uart_get_baudrate(kUart, &baud) == ESP_OK &&
           uart_get_word_length(kUart, &bits) == ESP_OK && uart_get_parity(kUart, &parity) == ESP_OK &&
           uart_get_stop_bits(kUart, &stop) == ESP_OK;
}
"""
MENU_MAIN_SOURCE = """constexpr gpio_num_t kButtonA = GPIO_NUM_0;  // "gpio_set_level(" in a string is ignored
void task() { const bool down = gpio_get_level(kButtonA) == 0; }
void start() {
    (void)gpio_set_direction(kButtonA, GPIO_MODE_INPUT);
    (void)gpio_pullup_en(kButtonA);
}
"""
MENU_SIO_CHECK = "paquete de prueba menu-sio (sdkconfig, aplicación y ELF)"


def menu_record():
    """The gate record menu-sio requires: M3a approved, H4 not (JSON false, as in docs/mediciones/puertas.json)."""
    record = gate_record()
    record["H4"] = {"aprobada": False, "fecha": None, "registro": None}
    return record


class MenuSioPackageTest(unittest.TestCase):
    """menu-sio: the cassette calls plus the menu as D1 on the physical SIO, only with M3a approved, H4 not, and
    CONFIG_RB_MENU_SIO on; its UART2 and button calls exact and on the board's pins; every other variant refuses
    the option and the code, and leaves its two sources out of the scan."""

    def setUp(self):
        CassettePackageTest.setUp(self)
        self.profile(False, True)
        self.gates(menu_record())
        write(self.f.src / "components" / "rb_esp_sio" / "src" / "esp_sio_uart.cpp", SIO_UART_SOURCE)
        write(self.f.src / "main" / "menu_sio.cpp", MENU_MAIN_SOURCE)
        self.build(menu_option=True, app=MENU_APP_MARKERS, elf=MENU_ELF)

    tearDown = CassettePackageTest.tearDown
    profile = CassettePackageTest.profile
    gates = CassettePackageTest.gates
    failed = CassettePackageTest.failed

    def build(self, menu_option, app, elf, stacks=MENU_STACKS):
        write(self.f.build / "sdkconfig", SDKCONFIG + ("CONFIG_RB_MENU_SIO=y\n" + stacks if menu_option else ""))
        write(self.f.build / "retrobridge.elf", elf)
        write(self.f.build / "retrobridge.bin", app_image(version="7621040", elf=elf, extra=app))

    def package(self, variant="menu-sio"):
        return CassettePackageTest.package(self, variant)

    def assert_refused(self, check_name, variant="menu-sio"):
        CassettePackageTest.assert_refused(self, check_name, variant)

    def edit(self, rel, old, new):
        path = self.f.src / rel
        text = path.read_text(encoding="utf-8")
        self.assertIn(old, text)
        path.write_text(text.replace(old, new, 1), encoding="utf-8")

    # positive case

    def test_menu_sio_package_with_m3a_approved_and_h4_not(self):
        log = self.package()
        self.assertTrue(log.ok, self.failed(log))
        provenance = json.loads((self.f.out / "PROVENANCE.json").read_text(encoding="utf-8"))
        self.assertEqual(provenance["package"], "retrobridge-fw-menu-sio-fujinet-atari-v1x")
        self.assertEqual(provenance["variant"], "menu-sio")
        self.assertEqual(provenance["features"], ["menu_sio"])
        self.assertEqual(provenance["gates"], menu_record())
        self.assertEqual(json.loads((self.f.out / "config" / "puertas.json").read_text(encoding="utf-8")), menu_record())
        self.assertIn("GPIO/UART del SIO solo en rb_esp_cassette y menu-sio (código)", [c["name"] for c in provenance["checks"]])
        self.assertIn("menu-sio", (self.f.out / "LEEME.md").read_text(encoding="utf-8"))
        log = fp.verify_package(self.f.out)
        self.assertTrue(log.ok, self.failed(log))

    # gates, option and code

    def test_menu_sio_package_refuses_h4_signed_in_the_profile(self):
        self.profile(True, True)
        self.assert_refused(fp.PROFILE_CHECK)

    def test_menu_sio_package_refuses_m3a_unsigned_in_the_profile(self):
        self.profile(False, False)
        self.assert_refused(fp.PROFILE_CHECK)

    def test_menu_sio_package_refuses_h4_approved_in_the_record(self):
        self.gates(gate_record())
        self.assert_refused(fp.RECORD_CHECK)

    def test_menu_sio_package_refuses_h4_not_json_false_in_the_record(self):
        record = menu_record()
        record["H4"]["aprobada"] = None
        self.gates(record)
        self.assert_refused(fp.RECORD_CHECK)

    def test_menu_sio_package_refuses_m3a_unapproved_in_the_record(self):
        record = menu_record()
        record["M3a"]["aprobada"] = False
        self.gates(record)
        self.assert_refused(fp.RECORD_CHECK)

    def test_menu_sio_package_refuses_m3a_without_its_record_file(self):
        (self.f.src / "docs" / "mediciones" / "m3a-prueba.md").unlink()
        self.assert_refused(fp.RECORD_CHECK)

    def test_menu_sio_package_refuses_a_missing_record(self):
        (self.f.src / "docs" / "mediciones" / "puertas.json").unlink()
        self.assert_refused(fp.RECORD_CHECK)

    def test_menu_sio_verify_refuses_a_record_changed_to_unapproved(self):
        self.assertTrue(self.package().ok)
        path = self.f.out / "config" / "puertas.json"
        record = json.loads(path.read_text(encoding="utf-8"))
        record["M3a"]["aprobada"] = False
        path.write_text(json.dumps(record), encoding="utf-8")
        self.assertFalse(fp.verify_package(self.f.out).ok)

    def test_menu_sio_verify_refuses_a_record_changed_to_h4_approved(self):
        self.assertTrue(self.package().ok)
        path = self.f.out / "config" / "puertas.json"
        path.write_text(json.dumps(gate_record()), encoding="utf-8")
        self.assertFalse(fp.verify_package(self.f.out).ok)

    def test_menu_sio_verify_requires_the_profile_check_recorded(self):
        self.assertTrue(self.package().ok)
        path = self.f.out / "PROVENANCE.json"
        provenance = json.loads(path.read_text(encoding="utf-8"))
        provenance["checks"] = [c for c in provenance["checks"] if c["name"] != fp.PROFILE_CHECK]
        path.write_text(json.dumps(provenance), encoding="utf-8")
        log = fp.verify_package(self.f.out)
        self.assertIn("registrado en CI: " + fp.PROFILE_CHECK, self.failed(log))

    def test_menu_sio_package_needs_the_option(self):
        self.build(menu_option=False, app=MENU_APP_MARKERS, elf=MENU_ELF)
        self.assert_refused(MENU_SIO_CHECK)

    def test_menu_sio_package_needs_the_code_in_the_application(self):
        self.build(menu_option=True, app=CASSETTE_APP_MARKERS, elf=MENU_ELF)
        self.assert_refused(MENU_SIO_CHECK)

    def test_menu_sio_package_needs_the_code_in_the_elf(self):
        self.build(menu_option=True, app=MENU_APP_MARKERS, elf=CASSETTE_ELF)
        self.assert_refused(MENU_SIO_CHECK)

    def test_menu_sio_package_does_not_need_the_cassette_started(self):
        app = MENU_APP_MARKERS.replace(b"cassette ready: MOTOR %s, DATA IN %s", b"")
        self.build(menu_option=True, app=app, elf=MENU_ELF)
        log = self.package()
        self.assertTrue(log.ok, self.failed(log))

    def test_menu_sio_package_refuses_the_bench_option(self):
        write(self.f.build / "sdkconfig", SDKCONFIG + "CONFIG_RB_MENU_SIO=y\nCONFIG_RB_BENCH_C8=y\n")
        self.assert_refused(fp.BENCH_CHECK)

    # every other variant refuses menu-sio, but its sources alone do not fail them

    def test_other_variants_accept_the_menu_sources_when_not_built_in(self):
        self.build(menu_option=False, app=CASSETTE_APP_MARKERS, elf=CASSETTE_ELF)
        self.profile(True, True)  # test data: the cassette package needs both gates
        self.gates(gate_record())
        log = CassettePackageTest.package(self, "cassette")
        self.assertTrue(log.ok, self.failed(log))

    def test_cassette_package_refuses_the_menu_sio_build(self):
        self.assert_refused(MENU_SIO_CHECK, variant="cassette")

    def test_cassette_package_refuses_menu_symbols_without_the_option(self):
        self.build(menu_option=False, app=CASSETTE_APP_MARKERS, elf=MENU_ELF)
        self.assert_refused(MENU_SIO_CHECK, variant="cassette")

    def test_cassette_package_refuses_menu_literals_without_the_option(self):
        self.build(menu_option=False, app=MENU_APP_MARKERS, elf=CASSETTE_ELF)
        self.assert_refused(MENU_SIO_CHECK, variant="cassette")

    def test_menu_calls_outside_the_two_sources_fail_every_variant(self):
        write(self.f.src / "main" / "app_main.cpp", "void f() { (void)uart_write_bytes(kUart, b, 1); }\n")
        self.assert_refused(fp.MENU_CODE_CHECK)
        self.build(menu_option=False, app=CASSETTE_APP_MARKERS, elf=CASSETTE_ELF)
        self.assert_refused(fp.CASSETTE_CODE_CHECK, variant="cassette")

    # the menu-sio calls: exact, and only on the board's SIO pins and button A

    def test_uart_pins_other_than_data_in_and_data_out_are_refused(self):
        self.edit("components/rb_esp_sio/src/esp_sio_uart.cpp", "UART_PIN_NO_CHANGE, UART_PIN_NO_CHANGE) != ESP_OK",
                  "pins_.command, UART_PIN_NO_CHANGE) != ESP_OK")
        self.assert_refused(fp.MENU_CODE_CHECK)

    def test_uart_port_other_than_uart2_is_refused(self):
        self.edit("components/rb_esp_sio/src/esp_sio_uart.cpp", "UART_NUM_2", "UART_NUM_1")
        self.assert_refused(fp.MENU_CODE_CHECK)

    def test_gpio_on_data_in_is_refused(self):
        self.edit("components/rb_esp_sio/src/esp_sio_uart.cpp", "gpio_pullup_en(static_cast<gpio_num_t>(pins_.dataOut))",
                  "gpio_pullup_en(static_cast<gpio_num_t>(pins_.dataIn))")
        self.assert_refused(fp.MENU_CODE_CHECK)

    def test_command_must_be_an_input(self):
        self.edit("components/rb_esp_sio/src/esp_sio_uart.cpp", "(pins_.command), GPIO_MODE_INPUT",
                  "(pins_.command), GPIO_MODE_OUTPUT")
        self.assert_refused(fp.MENU_CODE_CHECK)

    def test_an_extra_uart_call_is_refused(self):
        self.edit("components/rb_esp_sio/src/esp_sio_uart.cpp", "void end() {",
                  "void end() { (void)uart_set_baudrate(kUart, 57600);")
        self.assert_refused(fp.MENU_CODE_CHECK)

    def test_button_on_another_pin_is_refused(self):
        self.edit("main/menu_sio.cpp", "GPIO_NUM_0;", "GPIO_NUM_2;")
        self.assert_refused(fp.MENU_CODE_CHECK)

    def test_button_must_be_an_input(self):
        self.edit("main/menu_sio.cpp", "gpio_set_direction(kButtonA, GPIO_MODE_INPUT)",
                  "gpio_set_direction(kButtonA, GPIO_MODE_INPUT_OUTPUT)")
        self.assert_refused(fp.MENU_CODE_CHECK)

    # NVS (menu-unidades-persistentes T7): no package may erase or touch it

    def test_nvs_flash_erase_in_menu_main_is_refused(self):
        self.edit("main/menu_sio.cpp", "(void)gpio_pullup_en(kButtonA);",
                  "(void)gpio_pullup_en(kButtonA);\n    (void)nvs_flash_erase();")
        self.assert_refused(fp.NVS_CHECK)

    def test_nvs_split_over_lines_in_a_component_is_refused(self):
        write(self.f.src / "components" / "rb_menu" / "src" / "extra.cpp",
              "void wipe() { auto erase = nvs_flash_erase_partition\n    ; (void)erase; }\n")
        self.assert_refused(fp.NVS_CHECK)

    def test_nvs_after_a_dot_is_refused(self):
        write(self.f.src / "components" / "rb_menu" / "src" / "extra.cpp",
              "void f(Store& s) { s.nvs_flash_erase(); }\n")
        self.assert_refused(fp.NVS_CHECK)

    def test_nvs_after_an_arrow_is_refused(self):
        write(self.f.src / "components" / "rb_menu" / "src" / "extra.cpp",
              "void f(Store* s) { s->nvs_flash_erase(); }\n")
        self.assert_refused(fp.NVS_CHECK)

    def test_an_nvs_type_right_after_a_closing_bracket_is_refused(self):
        write(self.f.src / "components" / "rb_menu" / "src" / "extra.cpp",
              "template <typename T> T make();\ntemplate<>nvs_handle_t make<int>();\n")
        self.assert_refused(fp.NVS_CHECK)

    def test_nvs_in_comments_strings_and_tests_is_not_refused(self):
        write(self.f.src / "main" / "notes.cpp",
              '#include "nvs_flash.h"\n// nvs_flash_erase() is never called\nconst char* k = "nvs_flash_init";\n')
        write(self.f.src / "components" / "rb_menu" / "test" / "fake.cpp", "void t() { nvs_flash_erase(); }\n")
        log = self.package()
        self.assertTrue(log.ok, self.failed(log))

    # Task stacks (menu-unidades-persistentes): app_main -> finish() -> load() overflowed on the bench

    def test_the_7a1ebf0_frames_are_refused(self):
        self.build(menu_option=True, app=MENU_APP_MARKERS, elf=xtensa_elf(FRAMES_7A1EBF0) + MENU_MARKERS)
        log = self.package()
        self.assertFalse(log.ok)
        detail = next(c.detail for c in log.checks if c.name == fp.STACK_CHECK)
        self.assertIn("app_main", detail)
        self.assertIn("rb_drive_save", detail)

    def test_save_then_load_over_the_save_task_stack_is_refused(self):
        self.build(menu_option=True, app=MENU_APP_MARKERS, elf=MENU_ELF,
                   stacks="CONFIG_ESP_MAIN_TASK_STACK_SIZE=3584\nCONFIG_RB_DRIVE_SAVE_STACK=1536\n")
        self.assert_refused(fp.STACK_CHECK)

    def test_a_large_load_result_left_in_finish_is_refused(self):
        self.build(menu_option=True, app=MENU_APP_MARKERS, elf=xtensa_elf({**MENU_FRAMES, FINISH: 400 + 1040}) + MENU_MARKERS)
        self.assert_refused(fp.STACK_CHECK)

    def test_a_slot_copy_left_in_decode_is_refused(self):
        # fc176ee: decode() built a DriveMemorySlot on its stack (1072 B) under load(), in both tasks
        self.build(menu_option=True, app=MENU_APP_MARKERS, elf=xtensa_elf({**MENU_FRAMES, DECODE: 1072}) + MENU_MARKERS)
        log = self.package()
        self.assertFalse(log.ok)
        detail = next(c.detail for c in log.checks if c.name == fp.STACK_CHECK)
        self.assertIn("decode", detail)

    def test_a_missing_decode_symbol_is_refused(self):
        frames = dict(MENU_FRAMES)
        del frames[DECODE]
        self.build(menu_option=True, app=MENU_APP_MARKERS, elf=xtensa_elf(frames) + MENU_MARKERS)
        self.assert_refused(fp.STACK_CHECK)

    def test_a_missing_chain_symbol_is_refused(self):
        frames = dict(MENU_FRAMES)
        del frames[LOAD]
        self.build(menu_option=True, app=MENU_APP_MARKERS, elf=xtensa_elf(frames) + MENU_MARKERS)
        self.assert_refused(fp.STACK_CHECK)

    def test_a_chain_function_without_entry_is_refused(self):
        self.build(menu_option=True, app=MENU_APP_MARKERS, elf=xtensa_elf({**MENU_FRAMES, "f_open": None}) + MENU_MARKERS)
        self.assert_refused(fp.STACK_CHECK)

    def test_a_missing_task_stack_size_is_refused(self):
        self.build(menu_option=True, app=MENU_APP_MARKERS, elf=MENU_ELF, stacks="CONFIG_ESP_MAIN_TASK_STACK_SIZE=3584\n")
        self.assert_refused(fp.STACK_CHECK)

    def test_the_stack_check_reads_frames_from_the_elf(self):
        frames = fp.elf_function_frames(xtensa_elf({"a": 32, "b": 3136, "c": None}))
        self.assertEqual(frames, {"a": 32, "b": 3136, "c": None})

    def test_gpio_in_menu_main_on_another_pin_is_refused(self):
        self.edit("main/menu_sio.cpp", "(void)gpio_pullup_en(kButtonA);", "(void)gpio_pullup_en(GPIO_NUM_21);")
        self.assert_refused(fp.MENU_CODE_CHECK)

    # the real tree

    def test_real_tree_has_no_nvs(self):
        repo = Path(__file__).resolve().parents[2]
        ok, detail = fp.check_nvs(repo)
        self.assertTrue(ok, detail)

    def test_real_tree_has_exactly_the_cassette_and_menu_sio_calls(self):
        repo = Path(__file__).resolve().parents[2]
        ok, detail = fp.check_sio_calls(repo, fp.VARIANTS["menu-sio"].sio_calls)
        self.assertTrue(ok, detail)


# Bench C8a (docs/hardware/banco-c8a-aislado.md, phase F2): the bench of main/bench_c8.cpp with the F1 controls
# (SIO_5V interlock, `c8a run` only). Its image: the bench and the C8a order in, the cassette never started, no menu.
C8A_APP_MARKERS = BENCH_APP_MARKERS + (b"\x1b[0;32mI (%lu) %s: C8A READY: type c8a run (USB only, SIO cable "
                                       b"disconnected)\x1b[0m\n\0")
C8A_ELF = (ELF_BYTES + b"\0_ZN2rb3esp9EspDataIn9configureERKNS_5board24CassetteOutputAssignmentE\0"
           b"_ZN2rb5bench6run_c8ERKNS_5board12BoardProfileERNS_3esp9EspDataInE\0"
           b"_ZN2rb5bench16start_c8_commandERKNS_5board12BoardProfileERNS_3esp9EspDataInE\0")


class BenchC8aPackageTest(unittest.TestCase):
    """bench-c8a (C-3 to C-7): named BENCH-C8A-NO-SIO, only with H4 unapproved and M3a approved, read from the
    profile and the record; the bench and the c8a order in the image, the cassette not started, no menu; the
    manifest says bench and SIO not authorized; no CI step uploads it."""

    def setUp(self):
        CassettePackageTest.setUp(self)
        write(self.f.src / "main" / "bench_c8.cpp", BENCH_SOURCE)
        self.profile(False, True)
        self.gates(menu_record())
        self.build()

    tearDown = CassettePackageTest.tearDown
    profile = CassettePackageTest.profile
    gates = CassettePackageTest.gates
    failed = CassettePackageTest.failed

    def build(self, sdk="CONFIG_RB_BENCH_C8=y\n", app=C8A_APP_MARKERS, elf=C8A_ELF):
        write(self.f.build / "sdkconfig", SDKCONFIG + sdk)
        write(self.f.build / "retrobridge.elf", elf)
        write(self.f.build / "retrobridge.bin", app_image(version="7621040", elf=elf, extra=app))

    def package(self, variant="bench-c8a"):
        return CassettePackageTest.package(self, variant)

    def assert_refused(self, check_name, variant="bench-c8a"):
        CassettePackageTest.assert_refused(self, check_name, variant)

    def test_the_variant_is_named_and_gated_as_the_bench(self):
        spec = fp.VARIANTS["bench-c8a"]
        self.assertIn("BENCH-C8A-NO-SIO", spec.package)
        self.assertEqual(spec.gates, (False, True))
        self.assertTrue(spec.bench)
        self.assertFalse(spec.menu_sio)
        self.assertNotIn("cassette", spec.features)  # the cassette is not started in the bench build
        self.assertIn("bench_c8a", spec.features)

    def test_bench_c8a_packages_with_h4_unapproved_and_m3a_approved(self):
        log = self.package()
        self.assertTrue(log.ok, self.failed(log))
        provenance = json.loads((self.f.out / "PROVENANCE.json").read_text(encoding="utf-8"))
        self.assertEqual(provenance["variant"], "bench-c8a")
        self.assertIs(provenance["bench"], True)
        self.assertEqual(provenance["sio"], "no autorizado")
        self.assertTrue(fp.verify_package(self.f.out).ok)

    def test_bench_c8a_refuses_h4_approved_in_the_profile(self):
        self.profile(True, True)
        self.assert_refused(fp.PROFILE_CHECK)

    def test_bench_c8a_refuses_a_record_with_other_gates(self):
        self.gates(gate_record())
        self.assert_refused(fp.RECORD_CHECK)

    def test_bench_c8a_needs_the_bench_option(self):
        self.build(sdk="")
        self.assert_refused(fp.BENCH_CHECK)

    def test_bench_c8a_refuses_the_bench_that_runs_at_boot(self):
        self.build(app=BENCH_APP_MARKERS, elf=C8A_ELF.replace(b"_ZN2rb5bench16start_c8_command", b"_ZN2rb5bench0x"))
        log = self.package()
        self.assertFalse(log.ok)
        self.assertIn(f"{fp.FEATURE_LABELS['bench_c8a']} (aplicación)", self.failed(log))
        self.assertIn(f"{fp.FEATURE_LABELS['bench_c8a']} (ELF)", self.failed(log))

    def test_bench_c8a_refuses_an_image_that_starts_the_cassette(self):
        self.build(elf=C8A_ELF + b"_ZN2rb3esp15CassetteService5startEv\0")
        self.assert_refused(fp.CASSETTE_START_CHECK)

    def test_bench_c8a_refuses_the_menu(self):
        self.build(sdk="CONFIG_RB_BENCH_C8=y\nCONFIG_RB_MENU_SIO=y\n", app=C8A_APP_MARKERS + MENU_APP_MARKERS,
                   elf=C8A_ELF + MENU_ELF)
        self.assert_refused(fp.MENU_SIO_CHECK)

    def test_verify_refuses_a_manifest_without_the_bench_declaration(self):
        self.assertTrue(self.package().ok)
        path = self.f.out / "PROVENANCE.json"
        provenance = json.loads(path.read_text(encoding="utf-8"))
        provenance["sio"] = "autorizado"
        path.write_text(json.dumps(provenance), encoding="utf-8")
        self.assertIn(fp.BENCH_DECLARED_CHECK, self.failed(fp.verify_package(self.f.out)))

    def test_the_cassette_variant_refuses_the_c8a_image(self):
        self.profile(True, True)
        self.gates(gate_record())
        self.assert_refused(fp.BENCH_CHECK, "cassette")

    def test_no_ci_step_uploads_a_bench_package(self):
        # C-6: no distribution without an explicit decision. Each upload-artifact step, up to the next step.
        ci = (Path(__file__).resolve().parents[2] / ".github" / "workflows" / "ci.yml").read_text(encoding="utf-8")
        steps = re.split(r"\n\s*- (?:name|uses):", ci)
        uploads = [s for s in steps if "actions/upload-artifact" in s]
        self.assertTrue(uploads)
        for step in uploads:
            self.assertNotIn("bench", step.lower())

if __name__ == "__main__":
    unittest.main()
