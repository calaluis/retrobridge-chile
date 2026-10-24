# Toolchain fijado (D-12, ADR-001, ADR-010)

| Herramienta | Versión fijada | Dónde se fija | Verificación |
|---|---|---|---|
| ESP-IDF | **v6.1** (6.1.0), última v6.x estable al 2026-10-23 | `CMakeLists.txt` (`RB_IDF_VERSION`), CI `espressif/idf:v6.1` | El build aborta si la versión no coincide |
| Chip | `esp32` | `sdkconfig.defaults` (`CONFIG_IDF_TARGET`) | — |
| Estándar C++ | `gnu++17` (ESP-IDF usa `gnu++26` por defecto) | `cmake/rb_build_flags.cmake` | `rb/core/toolchain_checks.hpp` (`#error`) |
| Excepciones / RTTI | Deshabilitadas | `rb_build_flags.cmake` + `sdkconfig.defaults` | `toolchain_checks.hpp` |
| Unity | v2.7.0 (`b6763fb`), vendorizado | `test/host/unity/` | — |
| CMake (host) | ≥ 3.22 | `test/host/CMakeLists.txt` | — |

## Compilación local

Pruebas de host (sin ESP-IDF; requiere CMake, Ninja, GCC o Clang, y Python 3). Python genera en el momento las
imágenes ATR de la Meta A (`tools/make_atr.py`); esas imágenes no se versionan:

```bash
cmake -S test/host -B build/host -G Ninja
cmake --build build/host
ctest --test-dir build/host --output-on-failure
```

En Windows con MSYS2, el directorio `C:\msys64\ucrt64\bin` tiene que estar al principio del `PATH`. Si no lo
está, CMake informa que el compilador "no puede compilar un programa simple".

Comprobaciones del repositorio:

```bash
python -I -m unittest discover -s tools/tests
python -I tools/check_layering.py
python -I tools/check_notices.py
```

Firmware (requiere ESP-IDF v6.1 instalado y `export.sh` o `export.ps1` cargado):

```bash
idf.py build
```

`idf.py flash` **no** se ejecuta sin aprobación explícita del equipo.

## Paquete de firmware en CI (H3c)

El job `firmware-package` de `.github/workflows/ci.yml` publica el firmware de diagnóstico del perfil
`RB_BOARD_FUJINET_ATARI_V1X` como artefacto descargable. Solo corre en Pull Requests y en ejecuciones manuales.

- **Qué compila:** el commit fijado `FW_COMMIT` (`1b1527d2…`), con ESP-IDF v6.1. Ese commit se descarga en `src/`,
  separado de las herramientas de empaquetado, que vienen del commit del propio workflow (`ci/`). La ejecución
  manual no acepta otros commits.
- **Qué comprueba:** `tools/ci/firmware_package.py` revisa la plataforma, el perfil, la flash (DIO, 40 MHz, 16 MB)
  en `sdkconfig`, en `flasher_args.json`, en `flash_args` y en las cabeceras de las imágenes. También comprueba la
  tabla de particiones (MD5, igualdad con el CSV y factory de 4 MiB en 0x10000), el espacio libre, las
  direcciones, la ausencia de GPIO/UART del SIO, la versión de `esp_app_desc_t` frente a la de ESP-IDF, el SHA-256
  del ELF, el commit del checkout y la ausencia de secretos.
- **Si algo falla:** no se publica nada.
- **Contenido:** el artefacto `retrobridge-fw-fujinet-atari-v1x-1b1527d` (retención de 90 días) contiene
  `firmware/`, `flash/`, `config/`, `reports/`, `PROVENANCE.json`, `SOURCE_IDENTITY.txt`, `LEEME.md` y
  `SHA256SUMS`. El resumen de la ejecución muestra el SHA-256 del zip (`artifact-digest`).

Verificar un paquete descargado, sin hardware:

```bash
sha256sum -c SHA256SUMS
python -I tools/ci/firmware_package.py verify <directorio del paquete>
```

**Limitación:** el binario no es reproducible bit a bit, porque incluye fecha, hora y rutas de compilación. La
procedencia se apoya en la ejecución de GitHub, el digest, el SHA-256 y el descriptor de la aplicación.

## Paquete de firmware H4 con diagnóstico autónomo

El job `firmware-package-h4` es **independiente** del anterior. El job H3c (`firmware-package`, commit `1b1527d`,
artefacto `retrobridge-fw-fujinet-atari-v1x-1b1527d`) no cambia: conserva su commit, su nombre, sus campos de
procedencia y sus comprobaciones, porque usa la variante por defecto `h3c` del empaquetador.

- **Qué compila:** el commit fijado `15b153f267cf6766978bd786eb1b463ea0e4ac50`, con el módulo `rb_hw_diag` y el comando
  `diag`, el perfil `RB_BOARD_FUJINET_ATARI_V1X` y ESP-IDF v6.1. Corre en Pull Requests y en ejecuciones manuales, sin
  entradas de commit.
- **Empaquetado:** `tools/ci/firmware_package.py package --variant diag`. Las variantes forman un conjunto cerrado
  (`h3c` y `diag`); no se aceptan nombres ni commits arbitrarios. Además de todas las comprobaciones de H3c, la
  variante `diag` exige:
  - **diagnóstico incorporado en la aplicación:** los literales `DIAG BEGIN format=%d source=retrobridge-hw-diag` y
    `diag status|memory|storage|events|report` dentro de `retrobridge.bin`;
  - **diagnóstico incorporado en el ELF:** los símbolos `_ZN2rb6hwdiag12ReportWriter…` y
    `_ZN2rb3esp14EspSystemProbe…`;
  - **versión = commit fijado:** la versión de `esp_app_desc_t` debe ser un prefijo hexadecimal del commit, sin
    sufijos como `-dirty`.
- **Identidad:** `PROVENANCE.json` registra `package`, `variant: diag`, `features: ["hw_diag"]` y el commit H4;
  `SOURCE_IDENTITY.txt` y `LEEME.md` también apuntan al firmware H4.
- **Verificación en CI:** después de empaquetar, el job ejecuta el modo `verify` sobre el paquete, que vuelve a
  comprobar el manifiesto, las cabeceras, la tabla, el ELF, la variante y las marcas del diagnóstico.
- **Artefacto:** `retrobridge-fw-diag-fujinet-atari-v1x-15b153f`, con retención de 90 días.
- **SHA-256:** el del zip y los de los archivos se conocen solo cuando GitHub Actions genera el artefacto (resumen de
  la ejecución y `SHA256SUMS`). Ninguno se escribe aquí por adelantado.
- **Regresión:** `tools/tests/test_ci_workflow.py` comprueba que los dos jobs mantienen commits, nombres y variantes
  distintos y fijos, sin entradas de commit y sin credenciales persistidas.
