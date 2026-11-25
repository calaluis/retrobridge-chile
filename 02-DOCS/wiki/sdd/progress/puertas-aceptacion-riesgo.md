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

## Ronda 3 (segunda revisión independiente, `6061902919`, sobre `76db969`)

- **H4:** el aviso de `menu_sio.cpp` decía «H4 and M3a NOT approved» y pedía el adaptador sin pin 8. Ahora dice H4
  sin aprobar (R2, R6, R7 no aceptados), M3a aprobada solo por R1 y la cinta apagada. Se conserva el prefijo que
  busca el empaquetador. También `menu_sio.hpp`, `Kconfig.projbuild`, `sdkconfig.menu-sio`, `sdkconfig.bench-c8` y
  `bench_c8.hpp`.
- **H5:** `procedimiento-demo-menu-sio.md` separa §0 (vigente, desde `76db969`) de §1–§9 (histórico, `b8e6152`).
  §0: sin adaptador sin pin 8 (R1 aceptado); instalar no acepta R2, R6 ni R7; conectar al 65XE requiere la decisión
  aparte del responsable.
- **H6:** `procedimiento-prueba-banco-c8.md` documenta cómo recuperar el banco C8 cuando el responsable decida
  validar DATA IN. No se restaura ahora: antes de T9b solo se instala `menu-sio`.
- Host 100/100; herramientas 295 OK; capas y avisos OK.
