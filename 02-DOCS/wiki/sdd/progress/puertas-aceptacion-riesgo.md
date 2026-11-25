---
type: progress
title: Progreso — puertas-aceptacion-riesgo
description: Avance por tarea de la spec puertas-aceptacion-riesgo (M3a aprobada por aceptación de riesgo; H4 pendiente).
tags: [sdd, progress, puertas, h4, m3a]
timestamp: 2026-11-25T22:00:00Z
topic: sdd
slug: puertas-aceptacion-riesgo
status: in-review
---

# Progreso — puertas-aceptacion-riesgo

Rama `gates/m3a-h4-aceptacion-riesgo` (worktree `.worktrees/gates-m3a-h4-aceptacion`, desde main `8ca5d59`).
PR #110. Sin hardware, sin flasheo y sin mediciones.

## Ronda 1 (commit `8a639f1`)

Aprobaba H4 y M3a. La primera revisión independiente (`6061700562`) pidió:
- **H1:** respaldo explícito del responsable, sin ampliar el alcance por inferencia;
- **H2:** documentar que el paquete `cassette` transmite sin depender de `kTapeOutput`, y delimitar qué firmware se
  usa antes de T9b;
- **H3:** separar el estado vigente del histórico.

## Ronda 2 (corrección)

El responsable respondió: acepta solo R1 (M3a), mantiene H4 pendiente y solo instala `menu-sio` antes de T9b.

- **T1:** el registro cita sus palabras exactas; `puertas.json` con M3a aprobada y H4 sin cambios.
- **T2:** perfil con `motorGateApproved = true` y `sioGateApproved = false`. Las pruebas de H4 vuelven a las de `main`.
- **T3b:** `tapeGatesOpen` sin cambios; las pruebas usan una copia con las dos puertas aprobadas.
- **T3:** cada variante declara `gates = (H4, M3a)`: `cassette` `(True, True)` y `menu-sio` `(False, True)`.
  `check_gate_record` exige JSON `false` para una puerta no aprobada. `tools/tests`: 295 OK.
  - Mutante 1 (`menu-sio` con `(True, True)`): 10 pruebas caen.
  - Mutante 2 (H4 no aprobada sin exigir JSON `false`): 2 pruebas caen. Restaurado.
- **T4:** sin job de `cassette`; solo `firmware-package-menu-sio`.
- **T5:** bloque «Estado vigente» y marcas de lo histórico en los documentos de hardware.
- Host: `ctest` 100/100.
- **Punto abierto para el responsable:** `menu-sio` responde por DATA IN con H4 sin aprobar (R2, R6, R7).
