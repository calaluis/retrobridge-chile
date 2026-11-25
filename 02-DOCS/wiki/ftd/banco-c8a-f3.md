---
author: Calaluis
branch: feat/banco-c8a-f3
status: done
---
# Banco C8a, fase F3: job de CI que compila y verifica `bench-c8a`

## Intent

Compilar de verdad el firmware del banco C8a con ESP-IDF, empaquetarlo como `bench-c8a` y verificarlo, sin
distribuir ninguna imagen (C-6 de `docs/hardware/banco-c8a-aislado.md`). Es la primera prueba de que el código de
F1 compila y de que la imagen real cumple las marcas de F2. Paso de la ruta hacia T9b (cargar y ejecutar
`international_karate.cas` en el 65XE real).

## Scope

Dentro:
- `.github/workflows/ci.yml`: job `firmware-package-bench-c8a` (compila, empaqueta, verifica; sin
  `upload-artifact`).
- `tools/tests/test_firmware_package.py`: `BenchC8aCiJobTest` (5 pruebas).
- Textos del banco que decían que ningún job lo compila.

Fuera:
- Distribuir la imagen, flashear, conectar el Atari y medir (F4).
- H4, `puertas.json`, perfiles aprobados, `kTapeOutput`.

## Checklist

- [x] Rama nueva desde `main` `0f99a3a` (elegida por el responsable).
- [x] RED — prueba: 4 de las 5 pruebas nuevas fallaron sin el job; la del árbol real pasó desde el principio
  (caracterización).
- [x] GREEN — prueba: `tools/tests` 316 OK; el YAML se lee con 4 pasos en el job.
- [x] Las pruebas pueden fallar — prueba: un paso `upload-artifact` añadido y `--variant menu-sio` fallan.
- [x] Compilación ESP-IDF real de `bench_c8.cpp` y `bench_c8_console.cpp` — prueba: ejecución `45700584507`, paso
  de compilación correcto.
- [x] Revisión 1, H1 (C-4 falló con la imagen real) — prueba: la comprobación lee `.symtab`; 3 pruebas nuevas
  del ELF y 1 de llamadas; 4 mutantes detectados; `tools/tests` 319 OK.
- [x] El job pasa entero (`package` y `verify`) — prueba: ejecución manual `45709127519` sobre `main` `d5934bb`,
  5 jobs correctos.

## Next step

Leer la CI del PR. Después, revisión independiente. F4 necesita autorización escrita del responsable.
