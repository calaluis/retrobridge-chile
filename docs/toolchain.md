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

## Compilaciones en GitHub Actions

El workflow `CI` ejecuta las pruebas de host, verifica las capas y avisos de terceros, y compila los perfiles ESP32 con ESP-IDF v6.1. Los paquetes de diagnóstico citados en las mediciones corresponden a ejecuciones históricas del laboratorio. Esta edición permite compilar el firmware desde el código fuente.
