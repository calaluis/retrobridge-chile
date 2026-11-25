---
type: progress
title: Progreso — puertas-aceptacion-riesgo
description: Avance por tarea de la spec puertas-aceptacion-riesgo (H4 y M3a aprobadas por aceptación de riesgo).
tags: [sdd, progress, puertas, h4, m3a]
timestamp: 2026-11-25T20:00:00Z
topic: sdd
slug: puertas-aceptacion-riesgo
status: in-review
---

# Progreso — puertas-aceptacion-riesgo

Rama `gates/m3a-h4-aceptacion-riesgo` (worktree `.worktrees/gates-m3a-h4-aceptacion`, desde main `8ca5d59`).
Sin hardware, sin flasheo y sin mediciones.

## T1 — Registro y `puertas.json` (hecha)

- `docs/mediciones/puertas-aceptacion-riesgo-2026-11-25.md`: «aceptación de riesgo, no validación eléctrica».
- `puertas.json`: H4 y M3a con `aprobada: true`, fecha y registro.
- `check_gate_record` → OK; prueba de herramientas `test_real_profile_and_record_allow_the_cassette_package`.

## T2 — Perfil `fujinet_atari_v1x` (hecha)

- Las dos puertas en `true`. RED primero en las pruebas de host; luego GREEN.
- Cada prueba de rechazo usa ahora una copia con la puerta en `false`.

## T3b — `EspTapeDeck` y `kTapeOutput` (hecha)

- `main/tape_gates.hpp`: `tapeGatesOpen(kTapeOutput, perfil)`. Con `kTapeOutput=false` el menú sigue diciendo
  «AUN NO PUEDE CARGARSE».
- `test/host/board/test_tape_gates.cpp`: 3 pruebas.

## T3 — Empaquetador (hecha)

- `menu-sio` con `gates=True`; se retiran `cassette-lectura`, `bench-c8` y la maquinaria de puertas cerradas.
- El chequeo de banco sigue: todas las variantes rechazan la opción y el código C8.
- `tools/tests`: 290 pruebas OK.
- Mutante (`menu-sio` con `gates=False`): 8 pruebas caen. Restaurado.

## T4 — CI (hecha)

- Nuevo job `firmware-package-cassette` desde el commit bajo prueba.
- Se quitan los jobs `firmware-package-cassette-lectura` y `firmware-package-bench-c8`.
- Pendiente: CI del PR verde, con los artefactos `cassette` y `menu-sio`.

## T5 — Documentos (hecha)

- `h4-puerta-sio.md`: estado, §3.4, R1 y §7.
- `enmienda-motor-gpio36.md`: cabecera y §7 (opción C reemplaza a D3).
- Procedimiento pasivo, ficha M3a, demo `menu-sio`, banco C8, instalación §7 y `h4-c-casetera.md`: avisos fechados.
- Los registros históricos (`docs/mediciones/*`, specs y progresos anteriores) no se reescriben.
