---
type: progress
title: Progreso — menu-presentacion
description: Avance por tarea de la spec menu-presentacion (carga sin barrido y pie con todas las teclas).
tags: [sdd, progress, menu, 6502]
timestamp: 2026-11-20T07:00:00Z
topic: sdd
slug: menu-presentacion
status: in-progress
---

# Progreso — menu-presentacion

Rama `feat/menu-presentacion` (worktree `.worktrees/feat-menu-presentacion`, desde main `e73b3e7`).

## P001 — Pie principal nuevo (hecha, 2026-11-20)

- `tools/make_menu.py`, `main_footer()`: fila 1 `[1-8] FUENTE`, `[E] SERVIDOR`, `[N] NUEVA ATR`; fila 2
  `[A] ARRANCAR D1`, `[U] DESMONTAR UNIDAD`. Cada fila cabe en 40 columnas (la fila 2 termina con 3 ceros).
- `host/src/menu_program.inc` regenerado; solo cambian los 80 bytes del pie en `kMenuMainTemplate` y en
  `kMenuBlock`.
- Prueba: `test_main_template_has_title_subtitle_and_the_reference_footer` con el texto nuevo. Primero en rojo
  (1 fallo de 47), después en verde.
- Controles:
  - pruebas: PASS. `tools/tests` 141 de 141; `ctest` 65 de 65.
  - compilación: PASS. MinGW ucrt64, `-Werror`.
  - seguridad: NO-APLICA. Solo texto de pantalla; ninguna entrada nueva.
  - trazabilidad: PASS. Pie = AC3; pruebas anteriores en verde = AC8.
  - `check_layering` y `check_notices`: OK. `make_menu.py --check`: al día.
- Riesgo `rsc risk`: tier 1, un revisor (correctitud): sin hallazgos. El host y sus pruebas no dependen del texto
  del pie, solo de sus posiciones (`menu_image.cpp:12`, `test_host_menu.cpp:294-298`); las 5 teclas del pie las
  atiende la vista principal (`menu_controller.cpp:312-421`). Nota menor: la fila 1 ocupa las 40 columnas, sin
  margen.

## Pendiente

P002 (búfer oculto, aviso y copia), P003, P004, P005.
