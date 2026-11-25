---
author: Calaluis
branch: feat/banco-c8a-f2
status: in-review
---
# Banco C8a, fase F2: variante `bench-c8a` del empaquetador

## Intent

Que el empaquetador sepa reconocer y empaquetar el firmware del banco C8a (F1, PR #114) sin confundirlo con
`menu-sio` ni con la casetera: controles C-3 a C-7 de `docs/hardware/banco-c8a-aislado.md`. Paso de la ruta hacia
T9b (cargar y ejecutar `international_karate.cas` en el 65XE real).

## Scope

Dentro:
- `tools/ci/firmware_package.py`: variante `bench-c8a`, marcas `bench_c8a`, comprobación «casetera sin arrancar»,
  `PROVENANCE.json` con `bench` y `sio`, y su comprobación en `verify`.
- `tools/tests/test_firmware_package.py`: `BenchC8aPackageTest` (11 pruebas).
- Textos que decían «ningún paquete lo compila»: docs del banco, `main/bench_c8.hpp`, `sdkconfig.bench-c8`.

Fuera:
- `.github/workflows/ci.yml` (F3), imagen, distribución, flasheo, Atari, mediciones (F4).
- H4, `puertas.json`, perfiles aprobados, `kTapeOutput`.

## Checklist

- [x] Rama nueva desde `main` `75f98a1` (elegida por el responsable).
- [x] RED antes del código — prueba: 9 de las 11 pruebas nuevas en ERROR; las de la casetera y la CI pasan desde el
  principio (caracterización).
- [x] GREEN — prueba: `tools/tests` 311 OK.
- [x] Las pruebas pueden fallar — prueba: 5 mutantes del empaquetador, los 5 detectados.
- [ ] Compilación real con ESP-IDF que cumpla las marcas — pendiente de F3.

## Next step

Revisión independiente del PR. Después, con decisión del responsable: F3 (job de CI que compila y verifica, sin
`upload-artifact`).
