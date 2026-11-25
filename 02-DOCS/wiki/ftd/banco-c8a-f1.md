---
author: Calaluis
branch: feat/banco-c8a-f1
status: in-review
---
# Banco C8a, fase F1: enclavamiento SIO_5V y orden manual

## Intent

Implementar los controles C-1 y C-2 de `docs/hardware/banco-c8a-aislado.md` para que el banco C8 pueda volver a
usarse con `(H4 = false, M3a = true)`. Es el primer paso hacia T9b, cuyo criterio final es cargar y ejecutar
`international_karate.cas` en el Atari 65XE real.

## Scope

Dentro:
- `main/bench_c8_gate.hpp`: `classifySio5v` (C-1, fallo seguro) y `C8Trigger` (C-2, una vez por arranque).
- `main/bench_c8_console.cpp`: orden `c8a run`; ADC1 canal 7 (GPIO35, solo entrada) → enclavamiento → `run_c8`.
- `main/app_main.cpp`: sin `run_c8` al arrancar; en el banco no arranca la casetera; registra la orden.
- Pruebas: `test_bench_c8_gate`, `test_bench_c8_sequence`, `tools/tests/test_bench_c8_wiring.py`; fakes `soc/*.h`,
  `onDelay` y `failRmtDelEncoder`.
- Documentos del banco: estado F1 y corrección de GPIO36 (entrada con M3a).

Fuera:
- H4, `puertas.json`, aprobaciones del perfil, `kTapeOutput`.
- Variante del empaquetador (F2), job de CI (F3), imagen, flasheo, conexión al Atari, mediciones (F4).

## Checklist

- [x] Rama nueva desde `main` `e14b1c8` — prueba: `git log -1 origin/main`.
- [x] RED antes del código — prueba: gate 10/11 FAIL con el esqueleto; cableado 5/5 FAIL.
- [x] GREEN — prueba: `ctest` 102/102; `tools/tests` 300 OK; `check_notices` y `check_layering` OK.
- [x] La secuencia UART2 → RMT → UART2 → liberación puede fallar — prueba: 6 mutantes, los 6 detectados (sin
  `release`, sin contar llamadas fallidas, sin dispersión, sin `ok`, sin «una vez», `Invalid` que corre).
- [x] Fuera de alcance intacto — prueba: `git diff origin/main --stat` sin `puertas.json`, `components/rb_board`,
  `menu_sio.cpp`, `Kconfig`, `sdkconfig.*` ni `.github`.
- [ ] Compilación ESP-IDF de `bench_c8_console.cpp` — pendiente de F3 (no hay job que la compile).

## Evidence

Ver el cuerpo del PR (Verificación).

## Next step

Revisión independiente del PR. Después, con decisión del responsable: F2 (variante `bench-c8a`) y F3 (job de CI).
