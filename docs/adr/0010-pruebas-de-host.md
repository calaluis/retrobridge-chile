# ADR-010 — Pruebas de host con CMake, CTest y Unity

- Estado: Aprobada (2026-10-23); D-9: Unity vendorizado

## Decisión

- `test/host/` es un proyecto CMake independiente de ESP-IDF. Construye los componentes puros con sus mismos
  `CMakeLists.txt` (rama `else()` de `ESP_PLATFORM`) y los mismos flags del firmware.
- Unity v2.7.0 está vendorizado en `test/host/unity/` (MIT), sin acceso a red en CI.
- En CI se ejecuta con ASan y UBSan.

## Alternativa descartada

El objetivo `linux` de ESP-IDF, que Espressif marca como *preview*.
