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

Variants (closed set): h3c and diag (diagnostic, no SIO pin call at all) and cassette (C107: the
UART and GPIO calls of rb_esp_cassette, listed one by one, and only with H4 and M3a approved both in
the board profile compiled in and in docs/mediciones/puertas.json; checked when packaging and again
when verifying); menu-sio (the cassette calls plus the menu's SIO calls, with the same gate checks).
"""

import argparse
import csv
import datetime as dt
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
                  "sdkconfig.board.fujinet-atari-v1x", "CMakeLists.txt", "sdkconfig.bench-c8", "sdkconfig.menu-sio")

# Bytes that exist only when a feature is linked in: string literals in the
# application image (flash .rodata) and C++ symbols in the ELF symbol table.
FEATURE_MARKERS = {
    "hw_diag": {  # rb_hw_diag + EspSystemProbe + the "diag" console command (H4)
        "app": (b"DIAG BEGIN format=%d source=retrobridge-hw-diag", b"diag status|memory|storage|events|report"),
        "elf": (b"_ZN2rb6hwdiag12ReportWriter", b"_ZN2rb3esp14EspSystemProbe"),
    },
    "cassette": {  # rb_esp_cassette: EspDataIn (UART2 + RMT on DATA IN) and CassetteService (C107)
        "app": (b"DATA IN on GPIO%d: records by UART2, FSK by RMT", b"cassette ready: MOTOR %s, DATA IN %s"),
        "elf": (b"_ZN2rb3esp9EspDataIn9configure", b"_ZN2rb3esp15CassetteService5start"),
    },
    "bench_c8": {  # main/bench_c8.cpp: bench test C8 of DATA IN (H4-C), only with CONFIG_RB_BENCH_C8
        "app": (b"C8 BENCH BEGIN: board '%s', H4 %s, M3a %s", b"C8 RESULT PASS"),
        "elf": (b"_ZN2rb5bench6run_c8",),
    },
    "bench_c8a": {  # main/bench_c8_console.cpp: bench C8a F1, the `c8a run` order behind the SIO_5V interlock
        "app": (b"C8A READY: type c8a run",),
        "elf": (b"_ZN2rb5bench16start_c8_command",),
    },
    "menu_sio": {  # main/menu_sio.cpp + rb_esp_sio: the menu served as D1 on the physical SIO (menu-sio-fisico)
        "app": (b"menu-sio TEST PACKAGE: transmits on SIO DATA IN", b"D1: menu. Switch the 65XE on."),
        "elf": (b"_ZN2rb3esp10EspSioUart5begin", b"_ZN2rb7menusio5start"),
    },
}
FEATURE_LABELS = {"hw_diag": "diagnóstico incorporado", "cassette": "casetera incorporada",
                  "bench_c8": "prueba de banco C8 incorporada",
                  "bench_c8a": "orden c8a run con enclavamiento SIO_5V incorporada", "menu_sio": "menú por SIO físico incorporado"}
BENCH_KCONFIG = "CONFIG_RB_BENCH_C8"
BENCH_CHECK = "prueba de banco C8 (sdkconfig y aplicación)"
BENCH_GRANT_CHECK = "concesión de banco solo en main/bench_c8.cpp (código)"
BENCH_SOURCE = "main/bench_c8.cpp"
# Bench C8a (docs/hardware/banco-c8a-aislado.md, C-4): its build never starts the cassette, whose loop would pump the
# EspDataIn the bench drives. CassetteService::start() is not virtual: only a direct call links it, and with none the
# linker drops it. The check reads the ELF's symbol table, not its bytes: the debug information keeps the name of a
# dropped function (the real build of dee10ec, CI run 45700584507). An ELF without a readable symbol table fails.
CASSETTE_START_SYMBOL = "_ZN2rb3esp15CassetteService5startEv"
CASSETTE_START_CHECK = "casetera sin arrancar (banco C8a, tabla de símbolos del ELF)"
BENCH_DECLARED_CHECK = "banco declarado en PROVENANCE.json (bench, SIO no autorizado)"
BENCH_SIO_TEXT = "no autorizado"
BENCH_GRANT = re.compile(r"\bvalidate_for_bench_data_in\b")
REGISTER_WRITE = re.compile(r"\b(REG_WRITE|REG_SET_BIT|REG_CLR_BIT|REG_SET_FIELD|REG_SET_BITS|WRITE_PERI_REG|"
                            r"SET_PERI_REG_MASK|CLEAR_PERI_REG_MASK|SET_PERI_REG_BITS)\b")

# The cassette package (C107) watches every GPIO, UART and RMT function by name, not only the calls of
# FORBIDDEN_CALLS: any use of the identifier counts (a call, a call split over lines, a pointer), with
# comments, strings and member names (".gpio_num") left out. The list is exact: file, name, how many
# times. Anything else, anywhere in main/ or components/, fails the cassette package.
WATCHED_IDENTIFIER = re.compile(r"(?<![.>\w])((?:gpio|uart|rmt|esp_rom_gpio|ledc|sigmadelta|dedic_gpio)_\w+)\b")
_DATA_IN = "components/rb_esp_cassette/src/esp_data_in.cpp"
# NVS holds the owner's Wi-Fi settings and must never be erased or rewritten by a package
# (menu-unidades-persistentes T7): no nvs_* identifier at all in main/ or components/ (a call, a call
# split over lines, a pointer, a member after . or -> or a type, also right after <), with comments and
# strings left out. Every variant checks it.
NVS_IDENTIFIER = re.compile(r"\bnvs_\w+\b")
NVS_CHECK = "sin nvs_* en main/ ni components/ (código)"
_SERVICE = "components/rb_esp_cassette/src/cassette_service.cpp"
CASSETTE_SIO_CALLS = (
    (_DATA_IN, "uart_driver_install", 1), (_DATA_IN, "uart_param_config", 1), (_DATA_IN, "uart_set_pin", 3),
    (_DATA_IN, "uart_driver_delete", 2), (_DATA_IN, "uart_wait_tx_done", 3), (_DATA_IN, "uart_set_baudrate", 1),
    (_DATA_IN, "uart_write_bytes", 1),
    # menu-cinta T9a.2 (decision 3): the shared mode reads, never writes, what UART2 TX still holds: the ring's
    # free room and the FIFO's (HAL), plus txEmpty() (the third uart_wait_tx_done). Same port, no new pin.
    (_DATA_IN, "uart_get_tx_buffer_free_size", 1), (_DATA_IN, "uart_ll_get_txfifo_len", 1),
    (_DATA_IN, "rmt_new_tx_channel", 1), (_DATA_IN, "rmt_new_simple_encoder", 1), (_DATA_IN, "rmt_enable", 1),
    (_DATA_IN, "rmt_transmit", 1), (_DATA_IN, "rmt_disable", 1),  # once per enable (bench test C8, d05bfe2)
    (_DATA_IN, "rmt_tx_register_event_callbacks", 1),  # the end of a run comes from on_trans_done
    (_DATA_IN, "rmt_del_channel", 3), (_DATA_IN, "rmt_del_encoder", 2),
    (_DATA_IN, "gpio_reset_pin", 2),  # H4-C C8: output off when the UART refuses the pin, and release()
    (_SERVICE, "gpio_config", 1), (_SERVICE, "gpio_get_level", 1),  # MOTOR, input only
)
# The same calls before H4-C C8 (firmware up to f405106, the commit the retired cassette-lectura job pinned):
# five uart_set_pin(), no gpio_reset_pin(), two rmt_disable() and the end of a run polled with
# rmt_tx_wait_all_done() instead of on_trans_done. Accepted as a whole, never mixed with the set above.
CASSETTE_SIO_CALLS_BEFORE_C8 = tuple(
    (path, call, {"uart_set_pin": 5, "uart_driver_delete": 1, "rmt_disable": 2, "uart_wait_tx_done": 2}.get(call, count))
    for path, call, count in CASSETTE_SIO_CALLS
    if call not in ("gpio_reset_pin", "rmt_tx_register_event_callbacks", "uart_get_tx_buffer_free_size",
                    "uart_ll_get_txfifo_len")) + (
    (_DATA_IN, "rmt_tx_wait_all_done", 1),)
# Every uart_set_pin() of the cassette puts TX on the granted DATA IN pin and nothing else.
CASSETTE_UART_SET_PIN = re.compile(r"uart_set_pin\s*\(\s*kUart\s*,\s*(?:pins\.dataIn\(\)|pin_)\s*,"
                                   r"\s*UART_PIN_NO_CHANGE\s*,\s*UART_PIN_NO_CHANGE\s*,\s*UART_PIN_NO_CHANGE\s*\)")
# The one gpio_reset_pin() of the cassette resets the granted DATA IN pin and nothing else.
CASSETTE_GPIO_RESET = re.compile(r"gpio_reset_pin\s*\(\s*static_cast\s*<\s*gpio_num_t\s*>\s*\(\s*pin_\s*\)\s*\)")
CASSETTE_UART_PORT = "constexpr uart_port_t kUart = UART_NUM_2;"

# menu-sio test package (menu-sio-fisico): the menu served as D1 on the physical SIO. Its two sources are the only
# other place with watched calls: UART2 on the board's SIO pins (rb_esp_sio) and button A (GPIO0, input). Every
# other variant leaves these two files out of the call scan, but must prove they are not built in: no
# CONFIG_RB_MENU_SIO, and none of the menu-sio literals or symbols in the application or the ELF.
_SIO_UART = "components/rb_esp_sio/src/esp_sio_uart.cpp"
_MENU_MAIN = "main/menu_sio.cpp"
MENU_SIO_SOURCES = (_SIO_UART, _MENU_MAIN)
MENU_SIO_CALLS = (
    (_SIO_UART, "gpio_set_direction", 1), (_SIO_UART, "gpio_pullup_en", 1), (_SIO_UART, "gpio_get_level", 2),
    (_SIO_UART, "uart_driver_install", 1), (_SIO_UART, "uart_param_config", 1), (_SIO_UART, "uart_set_pin", 1),
    (_SIO_UART, "uart_driver_delete", 2), (_SIO_UART, "uart_read_bytes", 1), (_SIO_UART, "uart_write_bytes", 1),
    (_SIO_UART, "uart_wait_tx_done", 3), (_SIO_UART, "uart_flush_input", 1),
    (_SIO_UART, "uart_set_rx_full_threshold", 1), (_SIO_UART, "uart_set_rx_timeout", 1),
    # menu-cinta T9a.7b (plan decision 1, «Reanudar el disco»): park() waits for an empty TX (the third
    # uart_wait_tx_done); resume() sets the disk's rate back and reads the rate and 8N1 back. Same UART2, same
    # pins: no new uart_set_pin, uart_driver_install or uart_driver_delete.
    (_SIO_UART, "uart_set_baudrate", 1), (_SIO_UART, "uart_get_baudrate", 1),
    (_SIO_UART, "uart_get_word_length", 1), (_SIO_UART, "uart_get_parity", 1), (_SIO_UART, "uart_get_stop_bits", 1),
    (_MENU_MAIN, "gpio_set_direction", 1), (_MENU_MAIN, "gpio_pullup_en", 1), (_MENU_MAIN, "gpio_get_level", 1),
)
# TX on DATA IN and RX on DATA OUT of the board profile, UART2, no RTS/CTS; GPIO only on COMMAND (input) and
# DATA OUT (pull-up, read); button A only on GPIO0, as an input.
MENU_UART_SET_PIN = re.compile(r"uart_set_pin\s*\(\s*kUart\s*,\s*pins_\.dataIn\s*,\s*pins_\.dataOut\s*,"
                               r"\s*UART_PIN_NO_CHANGE\s*,\s*UART_PIN_NO_CHANGE\s*\)")
MENU_UART_GPIO = re.compile(r"\bgpio_(?:set_direction|pullup_en|get_level)\s*\(\s*static_cast\s*<\s*gpio_num_t\s*>"
                            r"\s*\(\s*pins_\.(?:command|dataOut)\s*\)")
MENU_UART_COMMAND_INPUT = re.compile(r"gpio_set_direction\s*\(\s*static_cast\s*<\s*gpio_num_t\s*>\s*\(\s*"
                                     r"pins_\.command\s*\)\s*,\s*GPIO_MODE_INPUT\s*\)")
MENU_BUTTON_GPIO = re.compile(r"\bgpio_\w+\s*\(\s*kButtonA\b")
MENU_BUTTON_PIN = "constexpr gpio_num_t kButtonA = GPIO_NUM_0;"
MENU_BUTTON_INPUT = "gpio_set_direction(kButtonA, GPIO_MODE_INPUT)"
MENU_SIO_KCONFIG = "CONFIG_RB_MENU_SIO"
MENU_SIO_CHECK = "paquete de prueba menu-sio (sdkconfig, aplicación y ELF)"
MENU_CODE_CHECK = "GPIO/UART del SIO solo en rb_esp_cassette y menu-sio (código)"

# Task stacks of menu-sio (menu-unidades-persistentes): a stack overflow in app_main (finish() -> load() with
# ~5 KB of frames in a 3584 B stack) panicked the 7a1ebf0 package on the bench. The ELF is read directly: every
# Xtensa function starts with `entry a1, N`, and N is its frame. For each call chain below, the frames, plus
# the deepest stdio/FatFs/SD-over-SPI path measured in the same ELF, plus STACK_FIXED_RESERVE, must fit in the
# task's configured stack. A missing symbol, a function that does not start with `entry` or a missing size
# fails the check (it cannot be proven to fit).
STACK_CHECK = "pila de app_main y rb_drive_save en menu-sio (ELF)"
# Exception frame of an interrupt on the task stack, and what runs below the SPI driver (locks, heap, log).
STACK_FIXED_RESERVE = 768
# No frame of these chains may hold an object as big as one remembered memory (DriveMemory: 8 x 128 B): a
# LoadResult, a slot or a slot buffer belongs to the store (heap), not to the caller's stack.
STACK_FRAME_MAX = 1024
STACK_TASKS = {  # task -> sdkconfig option with its stack size, and the frames that start it
    "app_main": ("CONFIG_ESP_MAIN_TASK_STACK_SIZE", ("vPortTaskWrapper", "main_task", "app_main")),
    "rb_drive_save": ("CONFIG_RB_DRIVE_SAVE_STACK", ("vPortTaskWrapper", r"_ZN2rb7menusio\w*driveSaveTask\w*")),
}
_FINISH = r"_ZN2rb7menusio6finishE\w*"
_LOAD = r"_ZN2rb4host13SlotFileStore4loadEv"
_SAVE = r"_ZN2rb4host13SlotFileStore4saveE\w*"
_DECODE = r"_ZN2rb4host18drive_memory_codec6decodeE\w*"  # load() decodes each slot after reading it
STACK_CHAINS = (  # (task, chain below the task's own frames, reaches the microSD)
    ("app_main", (_FINISH, _LOAD), True),
    ("app_main", (_FINISH, _LOAD, _DECODE), False),
    ("app_main", (_FINISH, r"_ZN2rb4host15planDriveMemoryE\w*"), False),
    ("rb_drive_save", (_SAVE, _LOAD), True),  # save() reads again after an error
    ("rb_drive_save", (_SAVE, _LOAD, _DECODE), False),
    ("rb_drive_save", (_SAVE,), True),        # save() writes
)
# The stdio/FatFs/SD paths below load() and save(): reading as in the 7a1ebf0 panic backtrace, and writing.
STACK_FS_PATHS = {
    "lectura": ("fopen", "open", "esp_vfs_open", "vfs_fat_open", "f_open", "follow_path", "dir_find", "move_window",
                "ff_disk_read", "ff_sdmmc_read", "sdmmc_read_sectors", "sdmmc_read_sectors_dma", "sdmmc_send_cmd",
                "sdspi_host_do_transaction", "spi_device_polling_transmit"),
    "escritura": ("fwrite", "__bufio_put", "__bufio_flush_locked", "write", "esp_vfs_write", "vfs_fat_write",
                  "f_write", "ff_disk_write", "ff_sdmmc_write", "sdmmc_write_sectors", "sdmmc_write_sectors_dma",
                  "sdmmc_send_cmd", "sdspi_host_do_transaction", "spi_device_polling_transmit"),
}
BOARD_PROFILE_HEADER = "components/rb_board/include/rb/board/board_profile.hpp"
CODE_CHECK = "sin GPIO/UART del SIO (código)"
CASSETTE_CODE_CHECK = "GPIO/UART del SIO solo en rb_esp_cassette (código)"
PROFILE_CHECK = "puertas H4 y M3a (perfil)"
RECORD_CHECK = "puertas H4 y M3a (registro)"
PROFILE_HEADER = "components/rb_board/include/rb/board/fujinet_atari_v1x.hpp"
GATE_RECORD = "docs/mediciones/puertas.json"
GATE_RECORD_IN_PACKAGE = "config/puertas.json"
GATES = ("H4", "M3a")
PROFILE_FIELDS = 11            # BoardProfile initializer, in declaration order
PROFILE_H4, PROFILE_M3A = 7, 10  # sioGateApproved, motorGateApproved


@dataclass(frozen=True)
class Variant:
    """A closed set of package identities: no free-form names or commits from the command line."""
    package: str
    title: str
    features: tuple = ()
    version_matches_commit: bool = False  # esp_app_desc version must be a prefix of the pinned commit
    sio_calls: tuple = ()                 # (file, call, count) allowed; empty: no SIO call at all
    earlier_sio_calls: tuple = ()         # an older exact set also accepted (a pinned commit), or none
    gates: tuple = ()                     # expected (H4, M3a) approval in profile and record; empty: not read
    bench: bool = False                   # the bench test C8 build (CONFIG_RB_BENCH_C8); every other variant refuses it
    menu_sio: bool = False                # the menu-sio test package (CONFIG_RB_MENU_SIO); every other variant refuses it
    profile_text: str = "microSD por SPI3 en GPIO 18/23/19/5; sin pines SIO"

    @property
    def code_check(self) -> str:
        return MENU_CODE_CHECK if self.menu_sio else CASSETTE_CODE_CHECK if self.sio_calls else CODE_CHECK


VARIANTS = {
    # H3c, validated on the board: its name, provenance and checks stay as they were.
    "h3c": Variant("retrobridge-fw-fujinet-atari-v1x", "firmware de diagnóstico para FujiNet Atari FN32ROV-1.3/1.5"),
    # H4: the same diagnostic firmware plus the autonomous diagnostics (rb_hw_diag, "diag").
    "diag": Variant("retrobridge-fw-diag-fujinet-atari-v1x",
                    "firmware de diagnóstico con diagnóstico autónomo (rb_hw_diag) para FujiNet Atari FN32ROV-1.3/1.5",
                    features=("hw_diag",), version_matches_commit=True),
    # C107: the cassette drives DATA IN (UART2 + RMT on GPIO21) and reads MOTOR (GPIO36). Only with H4 and M3a.
    "cassette": Variant("retrobridge-fw-cassette-fujinet-atari-v1x",
                        "firmware con casetera .cas (DATA IN por UART2 y RMT) para FujiNet Atari FN32ROV-1.3/1.5",
                        features=("cassette",), version_matches_commit=True, sio_calls=CASSETTE_SIO_CALLS,
                        earlier_sio_calls=CASSETTE_SIO_CALLS_BEFORE_C8, gates=(True, True),
                        profile_text="microSD por SPI3 en GPIO 18/23/19/5; DATA IN en GPIO21 y MOTOR en GPIO36, "
                                     "con H4 y M3a aprobadas"),
    # menu-sio test package (menu-sio-fisico E1): the menu served as D1 on the physical SIO (UART2: TX DATA IN
    # GPIO21, RX DATA OUT GPIO33; COMMAND GPIO39 input). M3a approved by the owner's risk acceptance of R1
    # (docs/mediciones/puertas-aceptacion-riesgo-2026-11-25.md), so MOTOR (GPIO36) is read as an input; H4 stays
    # unapproved, so the tape never leaves by the SIO. The only package to install before T9b.
    "menu-sio": Variant("retrobridge-fw-menu-sio-fujinet-atari-v1x",
                        "firmware de prueba menu-sio (menú como D1 por el SIO físico, M3a aprobada y H4 sin aprobar) "
                        "para FujiNet Atari FN32ROV-1.3/1.5",
                        features=("menu_sio",), version_matches_commit=True,
                        sio_calls=CASSETTE_SIO_CALLS + MENU_SIO_CALLS, gates=(False, True), menu_sio=True,
                        profile_text="microSD por SPI3 en GPIO 18/23/19/5; paquete de prueba menu-sio: UART2 TX en "
                                     "DATA IN (GPIO21), RX en DATA OUT (GPIO33), COMMAND en GPIO39; MOTOR (GPIO36) "
                                     "como entrada, con M3a aprobada por aceptación de riesgo; H4 sin aprobar: la "
                                     "cinta no sale por el SIO"),
    # Bench C8a (docs/hardware/banco-c8a-aislado.md, C-3 to C-7): bench C8 with the F1 controls, on the FujiNet
    # alone, USB only, the SIO cable disconnected. H4 unapproved and M3a approved, as today; if either changes the
    # package fails and the variant must be reviewed. No menu, no cassette output, no new pin grant. No CI job
    # builds or uploads it until F3 and F4 are decided.
    "bench-c8a": Variant("retrobridge-BENCH-C8A-NO-SIO-fujinet-atari-v1x",
                         "firmware de BANCO C8a (NO CONECTAR AL SIO: solo USB, cable SIO desconectado; orden c8a run "
                         "con enclavamiento SIO_5V) para FujiNet Atari FN32ROV-1.3/1.5",
                         features=("bench_c8", "bench_c8a"), version_matches_commit=True,
                         sio_calls=CASSETTE_SIO_CALLS, gates=(False, True), bench=True,
                         profile_text="microSD por SPI3 en GPIO 18/23/19/5; banco C8a: GPIO21 (DATA IN) por UART2 y "
                                      "RMT solo con la orden c8a run y sin tensión SIO_5V en GPIO35; MOTOR (GPIO36) "
                                      "como entrada, con M3a aprobada por aceptación de riesgo; H4 sin aprobar: SIO "
                                      "no autorizado"),
}


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
    """Every forbidden call outside tests, as "path:line" (the diagnostic packages allow none)."""
    return [f"{path}:{number}" for path, number, _call in scan_calls(src)]


def scan_calls(src: Path, skip: tuple = ()):
    hits = []
    for base in ("main", "components"):
        root = src / base
        if not root.is_dir():
            continue
        for path in sorted(root.rglob("*")):
            if (path.suffix not in SOURCE_SUFFIXES or "test" in path.relative_to(src).parts
                    or path.relative_to(src).as_posix() in skip):
                continue
            for number, line in enumerate(path.read_text(encoding="utf-8", errors="replace").splitlines(), 1):
                for match in FORBIDDEN_CALLS.finditer(line):
                    hits.append((path.relative_to(src).as_posix(), number, match.group(1)))
    return hits


def check_nvs(src: Path):
    """Returns (ok, detail): ok when no source outside tests in main/ or components/ names nvs_*."""
    wrong = []
    for base in ("main", "components"):
        root = src / base
        if not root.is_dir():
            continue
        for path in sorted(root.rglob("*")):
            rel = path.relative_to(src)
            if path.suffix not in SOURCE_SUFFIXES or "test" in rel.parts:
                continue
            names = sorted(set(NVS_IDENTIFIER.findall(strip_code(path.read_text(encoding="utf-8", errors="replace")))))
            wrong += [f"{rel.as_posix()}: {name}" for name in names]
    return not wrong, ", ".join(wrong) or "ninguna"


def elf_function_frames(elf: bytes) -> dict:
    """{name: frame in bytes, or None if it does not start with `entry a1, N`} for every function symbol of
    a 32-bit little-endian ELF (Xtensa). Raises ValueError if it is not one."""
    if len(elf) < 52 or elf[:4] != b"\x7fELF" or elf[4] != 1 or elf[5] != 1:
        raise ValueError("no es un ELF de 32 bits little-endian")
    shoff, = struct.unpack_from("<I", elf, 32)
    shentsize, shnum = struct.unpack_from("<HH", elf, 46)
    sections = [struct.unpack_from("<IIIIIIIIII", elf, shoff + i * shentsize) for i in range(shnum)]
    symtabs = [s for s in sections if s[1] == 2]  # SHT_SYMTAB
    if not symtabs:
        raise ValueError("sin tabla de símbolos")
    symtab = symtabs[0]
    strtab = sections[symtab[6]]
    frames = {}
    for offset in range(symtab[4], symtab[4] + symtab[5], 16):
        name_at, value, _size, info, _other, _shndx = struct.unpack_from("<IIIBBH", elf, offset)
        if info & 0xF != 2:  # STT_FUNC
            continue
        start = strtab[4] + name_at
        name = elf[start:elf.index(b"\0", start)].decode("ascii", "replace")
        frame = None
        for s in sections:
            if s[1] == 1 and s[3] <= value < s[3] + s[5] and s[4] + value - s[3] + 3 <= len(elf):  # PROGBITS
                at = s[4] + value - s[3]
                word = elf[at] | elf[at + 1] << 8 | elf[at + 2] << 16
                if word & 0xFFF == 0x136:  # entry a1, imm12 * 8
                    frame = (word >> 12) * 8
                break
        frames.setdefault(name, frame)
    return frames


def elf_defined_functions(elf: bytes) -> set:
    """Names of the functions defined in a 32-bit little-endian ELF (STT_FUNC symbols of .symtab with a section).
    Raises ValueError if it is not one or has no symbol table."""
    if len(elf) < 52 or elf[:4] != b"\x7fELF" or elf[4] != 1 or elf[5] != 1:
        raise ValueError("no es un ELF de 32 bits little-endian")
    shoff, = struct.unpack_from("<I", elf, 32)
    shentsize, shnum = struct.unpack_from("<HH", elf, 46)
    sections = [struct.unpack_from("<IIIIIIIIII", elf, shoff + i * shentsize) for i in range(shnum)]
    symtabs = [s for s in sections if s[1] == 2]  # SHT_SYMTAB
    if not symtabs:
        raise ValueError("sin tabla de símbolos")
    symtab = symtabs[0]
    strtab = sections[symtab[6]]
    names = set()
    for offset in range(symtab[4], symtab[4] + symtab[5], 16):
        name_at, _value, _size, info, _other, shndx = struct.unpack_from("<IIIBBH", elf, offset)
        if info & 0xF == 2 and shndx != 0:  # STT_FUNC, defined
            start = strtab[4] + name_at
            names.add(elf[start:elf.index(b"\0", start)].decode("ascii", "replace"))
    return names


def check_cassette_not_started(elf: bytes):
    """Returns (ok, detail) for CASSETTE_START_CHECK: ok when CassetteService::start() is not a defined function."""
    try:
        linked = CASSETTE_START_SYMBOL in elf_defined_functions(elf)
    except (ValueError, struct.error) as error:
        return False, f"ELF ilegible: {error}"
    named = CASSETTE_START_SYMBOL.encode() in elf
    return not linked, (f"{CASSETTE_START_SYMBOL}: " + ("enlazada (función en .symtab)" if linked else
                        "no enlazada" + ("; el nombre solo aparece fuera de .symtab (depuración)" if named else "")))


def check_stack(elf: bytes, sdk: dict):
    """Returns (ok, detail) for STACK_CHECK."""
    try:
        frames = elf_function_frames(elf)
    except (ValueError, struct.error) as error:
        return False, f"ELF ilegible: {error}"
    wrong, seen, big = [], [], set()

    def frame_of(pattern):
        names = [n for n in frames if re.fullmatch(pattern, n)]
        if len(names) != 1:
            wrong.append(f"{pattern}: {len(names)} símbolos")
            return 0
        if frames[names[0]] is None:
            wrong.append(f"{names[0]}: no empieza con entry")
            return 0
        if frames[names[0]] >= STACK_FRAME_MAX and names[0] not in big:
            big.add(names[0])
            wrong.append(f"{names[0]}: marco de {frames[names[0]]} B (máximo {STACK_FRAME_MAX - 1} B)")
        return frames[names[0]]

    fs = {label: sum(frame_of(name) for name in path) for label, path in STACK_FS_PATHS.items()}
    fs_worst = max(fs.values())
    for task, chain, reaches_card in STACK_CHAINS:
        option, base = STACK_TASKS[task]
        size = sdk.get(option, "")
        if not size.isdigit():
            wrong.append(f"{task}: {option} sin valor")
            continue
        used = sum(frame_of(p) for p in base + chain)
        total = used + (fs_worst if reaches_card else 0) + STACK_FIXED_RESERVE
        seen.append(f"{task} {used}+{fs_worst if reaches_card else 0}+{STACK_FIXED_RESERVE}={total}/{size}")
        if total > int(size):
            wrong.append(f"{task}: {' -> '.join(chain)} necesita {total} B de {size} B")
    detail = "; ".join(wrong) if wrong else ", ".join(seen)
    return not wrong, detail + f" (FatFs: lectura {fs['lectura']} B, escritura {fs['escritura']} B)"


def strip_code(text: str) -> str:
    """C/C++ text without comments, string literals and character literals (in one pass, so a "//" inside a
    string does not start a comment)."""
    token = re.compile(r'//[^\n]*|/\*.*?\*/|"(?:\\.|[^"\\\n])*"|\'(?:\\.|[^\'\\\n])*\'', re.DOTALL)
    return token.sub(lambda m: " " if m.group(0).startswith("/") else '""', text)


def check_sio_calls(src: Path, allowed: tuple, skip: tuple = ()):
    """Returns (ok, detail). Without `allowed` (h3c, diag): the original per-line scan, no SIO call at all.
    With it (cassette): every watched identifier in main/ and components/ must be one of `allowed`, in its
    exact count, and every uart_set_pin() must target the granted pin on UART2. Files in `skip` (the menu-sio
    sources, for every other variant) are left out; check_variant then proves they are not built in."""
    if not allowed:
        hits = scan_calls(src, skip)
        return not hits, ", ".join(f"{p}:{n}" for p, n, _c in hits) or "ninguna llamada"
    counts, texts = {}, {}
    for base in ("main", "components"):
        root = src / base
        if not root.is_dir():
            continue
        for path in sorted(root.rglob("*")):
            rel = path.relative_to(src).as_posix()
            if path.suffix not in SOURCE_SUFFIXES or "test" in path.relative_to(src).parts or rel in skip:
                continue
            text = strip_code(path.read_text(encoding="utf-8", errors="replace"))
            texts[rel] = text
            for name in WATCHED_IDENTIFIER.findall(text):
                if not name.endswith("_t"):  # types such as uart_config_t
                    counts[(rel, name)] = counts.get((rel, name), 0) + 1
    want = {(path, call): count for path, call, count in allowed}
    wrong = [f"{p} {c} x{counts.get((p, c), 0)} (permitidas {want.get((p, c), 0)})"
             for p, c in sorted(set(counts) | set(want)) if counts.get((p, c), 0) != want.get((p, c), 0)]
    data_in = texts.get(_DATA_IN, "")
    if len(CASSETTE_UART_SET_PIN.findall(data_in)) != want.get((_DATA_IN, "uart_set_pin"), 0):
        wrong.append(f"{_DATA_IN}: uart_set_pin fuera de (kUart, pins.dataIn()/pin_, sin RX/RTS/CTS)")
    if len(CASSETTE_GPIO_RESET.findall(data_in)) != want.get((_DATA_IN, "gpio_reset_pin"), 0):
        wrong.append(f"{_DATA_IN}: gpio_reset_pin fuera de (pin_)")
    if data_in.count(CASSETTE_UART_PORT) != 1:
        wrong.append(f"{_DATA_IN}: kUart no es UART_NUM_2")
    label = "de C107"
    if any(path in MENU_SIO_SOURCES for path, _call, _count in allowed):
        label = "de C107 y menu-sio"
        uart, menu = texts.get(_SIO_UART, ""), texts.get(_MENU_MAIN, "")
        if len(MENU_UART_SET_PIN.findall(uart)) != want.get((_SIO_UART, "uart_set_pin"), 0):
            wrong.append(f"{_SIO_UART}: uart_set_pin fuera de (kUart, pins_.dataIn, pins_.dataOut, sin RTS/CTS)")
        uart_gpio = sum(n for (path, call), n in want.items() if path == _SIO_UART and call.startswith("gpio_"))
        if len(MENU_UART_GPIO.findall(uart)) != uart_gpio or len(MENU_UART_COMMAND_INPUT.findall(uart)) != 1:
            wrong.append(f"{_SIO_UART}: GPIO fuera de COMMAND (entrada) y DATA OUT")
        if uart.count(CASSETTE_UART_PORT) != 1:
            wrong.append(f"{_SIO_UART}: kUart no es UART_NUM_2")
        menu_gpio = sum(n for (path, call), n in want.items() if path == _MENU_MAIN and call.startswith("gpio_"))
        if (len(MENU_BUTTON_GPIO.findall(menu)) != menu_gpio or menu.count(MENU_BUTTON_PIN) != 1
                or menu.count(MENU_BUTTON_INPUT) != 1):
            wrong.append(f"{_MENU_MAIN}: GPIO fuera del botón A (GPIO0, entrada)")
    return not wrong, ", ".join(wrong) or f"solo las {sum(want.values())} llamadas {label}"


def board_profile_fields(src: Path):
    """The BoardProfile field names in declaration order, or None."""
    path = src / BOARD_PROFILE_HEADER
    if not path.is_file():
        return None
    body = re.findall(r"struct\s+BoardProfile\s*\{(.*?)\};", strip_code(path.read_text(encoding="utf-8")), re.DOTALL)
    if len(body) != 1:
        return None
    return re.findall(r"(\w+)\s*;", body[0])


def profile_gates(src: Path):
    """(H4, M3a) as compiled into fujinet_atari_v1x_profile(), or None if it cannot be read exactly."""
    path = src / PROFILE_HEADER
    order = board_profile_fields(src)
    if (not path.is_file() or order is None or len(order) != PROFILE_FIELDS
            or order[PROFILE_H4] != "sioGateApproved" or order[PROFILE_M3A] != "motorGateApproved"):
        return None
    text = path.read_text(encoding="utf-8")
    # Only #pragma once and #include: no #if, #ifdef or #define can hide which body is compiled.
    directives = re.findall(r"^\s*#\s*(\w+)", strip_code(text), re.MULTILINE)
    if any(d not in ("pragma", "include") for d in directives):
        return None
    bodies = re.findall(r"fujinet_atari_v1x_profile\(\)\s*noexcept\s*\{\s*return\s+BoardProfile\{(.*?)\};\s*\}",
                        strip_code(text), re.DOTALL)
    if len(bodies) != 1 or len(re.findall(r"\bfujinet_atari_v1x_profile\b", strip_code(text))) != 1:
        return None
    fields, depth, current = [], 0, ""
    for ch in bodies[0]:
        if ch == "," and depth == 0:
            fields.append(current.strip())
            current = ""
            continue
        depth += {"{": 1, "}": -1}.get(ch, 0)
        current += ch
    fields.append(current.strip())
    if len(fields) != PROFILE_FIELDS or any(fields[i] not in ("true", "false") for i in (PROFILE_H4, PROFILE_M3A)):
        return None
    return fields[PROFILE_H4] == "true", fields[PROFILE_M3A] == "true"


def check_gate_record(record, src=None, expected=(True, True)):
    """Returns (ok, detail). Each gate as `expected` (H4, M3a): an approved one is JSON true, with a date and a
    record in docs/mediciones/ (with `src`, the record must also exist there and name the gate); an unapproved
    one is JSON false, nothing else."""
    if not isinstance(record, dict):
        return False, "no es un objeto JSON"
    problems = []
    for gate, approved in zip(GATES, expected):
        entry = record.get(gate)
        if not isinstance(entry, dict):
            problems.append(f"{gate}: ausente")
            continue
        if not approved:
            if entry.get("aprobada") is not False:
                problems.append(f"{gate}: no figura como no aprobada")
            continue
        if entry.get("aprobada") is not True:
            problems.append(f"{gate}: no aprobada")
        if not valid_date(entry.get("fecha")):
            problems.append(f"{gate}: sin fecha válida")
        registro = entry.get("registro")
        if (not isinstance(registro, str) or not registro.startswith("docs/mediciones/") or ".." in registro
                or not registro.endswith(".md")):
            problems.append(f"{gate}: registro fuera de docs/mediciones/")
        elif src is not None:
            target = src / registro
            if target.is_symlink() or not target.is_file():
                problems.append(f"{gate}: falta {registro}")
            elif gate not in target.read_text(encoding="utf-8", errors="replace"):
                problems.append(f"{gate}: {registro} no menciona {gate}")
    if problems:
        return False, ", ".join(problems)
    if all(expected):
        return True, "H4 y M3a aprobadas, con registro"
    return True, ", ".join(f"{g} aprobada, con registro" if a else f"{g} sin aprobar" for g, a in zip(GATES, expected))


def valid_date(value) -> bool:
    """A real calendar date written YYYY-MM-DD in ASCII digits."""
    if not isinstance(value, str) or not re.fullmatch(r"[0-9]{4}-[0-9]{2}-[0-9]{2}", value):
        return False
    try:
        dt.date.fromisoformat(value)
    except ValueError:
        return False
    return True


def load_record_json(text: str):
    """json.loads that refuses a repeated key (with it, the last "aprobada" would win silently)."""
    def no_duplicates(pairs):
        keys = [k for k, _v in pairs]
        if len(keys) != len(set(keys)):
            raise ValueError("clave repetida")
        return dict(pairs)
    return json.loads(text, object_pairs_hook=no_duplicates)


def read_gate_record(src: Path, expected=(True, True)):
    """Returns (ok, detail, record) for docs/mediciones/puertas.json in a checkout."""
    path = src / GATE_RECORD
    if not path.is_file():
        return False, f"falta {GATE_RECORD}", None
    try:
        record = load_record_json(path.read_text(encoding="utf-8"))
    except ValueError as error:
        return False, f"{GATE_RECORD}: {error}", None
    ok, detail = check_gate_record(record, src, expected)
    return ok, detail, record


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
    # CONFIG_RB_MENU_SIO has its own check, per variant (check_variant: on only in menu-sio, off everywhere else).
    sio_options = [k for k, v in sdk.items()
                   if k.startswith("CONFIG_RB_") and "SIO" in k and k != MENU_SIO_KCONFIG and v not in ("", "n")]
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


def check_variant(files: Files, variant: Variant, commit: str, log: CheckLog) -> None:
    """Checks that only some packages make: the features linked in and the version/commit link."""
    if not log.checks or "app_desc" not in log.facts:
        return  # check_files already failed (missing files)
    if variant.features:
        app, elf = files.app.read_bytes(), files.elf.read_bytes()
        for feature in variant.features:
            markers = FEATURE_MARKERS[feature]
            label = FEATURE_LABELS[feature]
            missing_app = [m.decode() for m in markers["app"] if m not in app]
            missing_elf = [m.decode() for m in markers["elf"] if m not in elf]
            log.require(f"{label} (aplicación)", not missing_app,
                        f"{feature}: " + (", ".join(missing_app) or "literales presentes en retrobridge.bin"))
            log.require(f"{label} (ELF)", not missing_elf,
                        f"{feature}: " + (", ".join(missing_elf) or "símbolos presentes en retrobridge.elf"))
    sdk = parse_sdkconfig(files.sdkconfig.read_text(encoding="utf-8"))
    bench_on = sdk.get(BENCH_KCONFIG) == "y"
    bench_in_app = FEATURE_MARKERS["bench_c8"]["app"][0] in files.app.read_bytes()
    log.require(BENCH_CHECK, bench_on == variant.bench and bench_in_app == variant.bench,
                f"{BENCH_KCONFIG}={'y' if bench_on else 'no'}, prueba en la aplicación: {'sí' if bench_in_app else 'no'}"
                f" (esperado: {'sí' if variant.bench else 'no'})")
    app, elf = files.app.read_bytes(), files.elf.read_bytes()
    menu_on = sdk.get(MENU_SIO_KCONFIG) == "y"
    menu_in_app = any(m in app for m in FEATURE_MARKERS["menu_sio"]["app"])
    menu_in_elf = any(m in elf for m in FEATURE_MARKERS["menu_sio"]["elf"])
    log.require(MENU_SIO_CHECK, menu_on == menu_in_app == menu_in_elf == variant.menu_sio,
                f"{MENU_SIO_KCONFIG}={'y' if menu_on else 'no'}, en la aplicación: {'sí' if menu_in_app else 'no'}, "
                f"en el ELF: {'sí' if menu_in_elf else 'no'} (esperado: {'sí' if variant.menu_sio else 'no'})")
    if "bench_c8a" in variant.features:
        log.require(CASSETTE_START_CHECK, *check_cassette_not_started(elf))
    if variant.menu_sio:
        stack_ok, stack_detail = check_stack(elf, sdk)
        log.require(STACK_CHECK, stack_ok, stack_detail)
    if variant.version_matches_commit:
        version = str(log.facts["app_desc"].get("version", ""))
        ok = len(version) >= 7 and all(c in "0123456789abcdef" for c in version) and commit.startswith(version)
        log.require("versión = commit fijado", ok, f"esp_app_desc={version!r}, commit={commit}")


def check_bench_grant(src: Path):
    """Returns (ok, detail): validate_for_bench_data_in() is called only from main/bench_c8.cpp (rb_board, which
    defines it, and the tests aside), and main/bench_c8.cpp writes no register."""
    wrong = []
    for base in ("main", "components"):
        root = src / base
        if not root.is_dir():
            continue
        for path in sorted(root.rglob("*")):
            rel = path.relative_to(src)
            if path.suffix not in SOURCE_SUFFIXES or "test" in rel.parts or rel.parts[:2] == ("components", "rb_board"):
                continue
            text = strip_code(path.read_text(encoding="utf-8", errors="replace"))
            if BENCH_GRANT.search(text) and rel.as_posix() != BENCH_SOURCE:
                wrong.append(f"{rel.as_posix()}: validate_for_bench_data_in")
            if rel.as_posix() == BENCH_SOURCE:
                wrong += [f"{BENCH_SOURCE}: {m}" for m in sorted(set(REGISTER_WRITE.findall(text)))]
    return not wrong, ", ".join(wrong) or "solo en main/bench_c8.cpp, sin escribir registros"


def check_build(src: Path, build: Path, source: SourceState, baseline: str, variant: Variant = VARIANTS["h3c"]) -> CheckLog:
    log = CheckLog()
    log.require("commit del firmware", source.head == baseline, f"checkout={source.head}, fijado={baseline}")
    log.require("checkout limpio", not source.dirty, "sin cambios locales" if not source.dirty else "hay cambios")
    skip = () if variant.menu_sio else MENU_SIO_SOURCES
    calls_ok, calls_detail = check_sio_calls(src, variant.sio_calls, skip)
    if not calls_ok and variant.earlier_sio_calls:
        earlier_ok, earlier_detail = check_sio_calls(src, variant.earlier_sio_calls, skip)
        if earlier_ok:
            calls_ok, calls_detail = True, earlier_detail + " (anterior a H4-C C8)"
    log.require(variant.code_check, calls_ok, calls_detail)
    nvs_ok, nvs_detail = check_nvs(src)
    log.require(NVS_CHECK, nvs_ok, nvs_detail)
    grant_ok, grant_detail = check_bench_grant(src)
    log.require(BENCH_GRANT_CHECK, grant_ok, grant_detail)
    if variant.gates:
        gates = profile_gates(src)
        log.require(PROFILE_CHECK, gates == variant.gates,
                    f"{PROFILE_HEADER}: " + ("no se pudo leer el perfil" if gates is None
                                             else f"H4={'sí' if gates[0] else 'no'}, M3a={'sí' if gates[1] else 'no'}"))
        record_ok, record_detail, record = read_gate_record(src, variant.gates)
        log.require(RECORD_CHECK, record_ok, f"{GATE_RECORD}: {record_detail}")
        log.facts["gates"] = record
    sdk = parse_sdkconfig((build / "sdkconfig").read_text(encoding="utf-8")) if (build / "sdkconfig").is_file() else {}
    files = build_files(build, src, sdk)
    check_files(files, log, build_relative=build)
    check_variant(files, variant, baseline, log)
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


def package(src: Path, build: Path, reports: Path, out: Path, source: SourceState, baseline: str, run: dict,
            variant: str = "h3c") -> CheckLog:
    spec = VARIANTS[variant]  # KeyError for anything outside the closed set
    log = check_build(src, build, source, baseline, spec)
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
        "package": spec.package, "board_profile": PROFILE_KCONFIG,
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
    if variant != "h3c":  # the H3c record keeps exactly its original fields
        provenance["variant"] = variant
        provenance["features"] = list(spec.features)
    if spec.bench:
        provenance["bench"] = True
        provenance["sio"] = BENCH_SIO_TEXT
    if spec.gates:
        provenance["gates"] = log.facts["gates"]
        (out / GATE_RECORD_IN_PACKAGE).write_text(json.dumps(log.facts["gates"], indent=2, ensure_ascii=False) + "\n",
                                                  encoding="utf-8")
    (out / "PROVENANCE.json").write_text(json.dumps(provenance, indent=2, ensure_ascii=False) + "\n", encoding="utf-8")
    (out / "reports" / "checks.txt").write_text(log.text(), encoding="utf-8")
    (out / "LEEME.md").write_text(readme(provenance, spec.title, spec.profile_text), encoding="utf-8")

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
    known = VARIANTS.get(provenance.get("variant", "h3c"))
    required = ["commit del firmware", "checkout limpio", known.code_check if known else CODE_CHECK]
    if known is not None and known.gates:
        required += [PROFILE_CHECK, RECORD_CHECK]
    for name in required:
        log.require(f"registrado en CI: {name}", recorded.get(name) is True, "PROVENANCE.json")
    if known is not None and known.gates:
        check, check_name = check_gate_record, RECORD_CHECK
        path = root / GATE_RECORD_IN_PACKAGE
        try:
            record = load_record_json(path.read_text(encoding="utf-8")) if path.is_file() else None
        except ValueError:
            record = None
        ok, detail = check(record, expected=known.gates) if record is not None else (False, f"falta {GATE_RECORD_IN_PACKAGE}")
        same = record is not None and record == provenance.get("gates")
        log.require(check_name, ok and same,
                    f"{GATE_RECORD_IN_PACKAGE}: {detail}" + ("" if same else "; distinto de PROVENANCE.json"))
    log.require("procedencia", provenance.get("firmware_source_commit") == provenance.get("baseline_commit")
                and provenance.get("elf_sha256") == log.facts.get("elf_sha256"),
                f"firmware={provenance.get('firmware_source_commit')}")
    variant = provenance.get("variant", "h3c")
    spec = VARIANTS.get(variant)
    log.require("variante del paquete", spec is not None and provenance.get("package") == spec.package,
                f"variant={variant}, package={provenance.get('package')}")
    if spec is not None and spec.bench:
        log.require(BENCH_DECLARED_CHECK, provenance.get("bench") is True and provenance.get("sio") == BENCH_SIO_TEXT,
                    f"bench={provenance.get('bench')!r}, sio={provenance.get('sio')!r}")
    if spec is not None:
        check_variant(package_files(root), spec, str(provenance.get("firmware_source_commit", "")), log)
    return log


def readme(p: dict, title: str = VARIANTS["h3c"].title, profile_text: str = VARIANTS["h3c"].profile_text) -> str:
    d = p["app_desc"]
    return f"""# RetroBridge Chile: {title}

Paquete generado por GitHub Actions. **No flashear sin autorización explícita** ni sin un respaldo verificado de la
flash (docs/hardware/procedimiento-instalacion-recuperacion.md).

| Dato | Valor |
|---|---|
| Perfil | `{p['board_profile']}` ({profile_text}) |
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
    p.add_argument("--variant", choices=sorted(VARIANTS), default="h3c",
                   help="package identity: h3c (default, unchanged), diag (H4, requires rb_hw_diag) or "
                        "cassette (C107) or menu-sio, both requiring H4 and M3a approved")
    v = sub.add_parser("verify")
    v.add_argument("package_dir", type=Path)
    args = parser.parse_args(argv)

    if args.command == "package":
        run = {k: v for k, v in os.environ.items() if k.startswith("GITHUB_")}
        log = package(args.src, args.build, args.reports, args.out, source_state(args.src), args.baseline, run,
                      args.variant)
    else:
        log = verify_package(args.package_dir)
    sys.stdout.write(log.text())
    return 0 if log.ok else 1


if __name__ == "__main__":
    sys.exit(main())
