---
type: progress
title: Progreso — menu-xex
description: Avance por tarea de la spec menu-xex (arrancar archivos .xex desde el menú, en D1).
tags: [sdd, progress, menu, xex, 6502]
timestamp: 2026-11-20T16:00:00Z
topic: sdd
slug: menu-xex
status: in-progress
---

# Progreso — menu-xex

Rama `feat/menu-xex` (worktree `.worktrees/feat-menu-xex`, desde main `ff9ff63`).

## X001 — Fuente y cargador 6502 (hecha, 2026-11-20)

- Fuentes en `docs/fuentes.md`:
  - formato `.xex` (gury.atari8.info; cc65 dice lo mismo);
  - `PORTB` bit 1 (*Altirra Hardware Reference Manual*, §2.6).
  - La fuente del formato cubre `INITAD` entre segmentos, así que no se activó la regla de parada del plan.
- Desviación 1 del plan, aprobada («Solo PORTB»):
  - el cargador no toca `RAMTOP` ni reabre `E:`, porque el manual no cubre las variables del SO;
  - lee los sectores con `SIOV`, no con `DSKINV`;
  - muestra el aviso de error con una lista de visualización propia.
- `tools/make_xex_loader.py` genera `host/src/xex_loader.inc`:
  - 3 sectores de arranque en `$0700`, más un búfer de 128 bytes sin cargar;
  - zona prohibida `$0700`-`$08FF`;
  - el largo del `.xex` va en el desplazamiento 336 (3 bytes);
  - sin página cero: escribe cada byte con un `STA` cuyo operando reescribe.
- Pruebas en `tools/tests/test_xex_loader.py` (intérprete 6502 propio, `SIOV` simulado sobre el disco virtual):
  - se escribieron primero, y fallaron porque el módulo no existía;
  - después, 11 de 11 en verde; tras la revisión, 13 de 13.
- Mutantes:
  - BASIC sin apagar: lo detecta la prueba del BASIC;
  - `INITAD` sin reiniciar: lo detectan las pruebas de `INITAD`;
  - error de lectura ignorado: lo detecta la prueba del error.
- Controles:
  - pruebas: PASS. `tools/tests` 161 de 161.
  - compilación: NO-APLICA. El `.inc` nuevo todavía no lo incluye ningún `.cpp` (lo hará X002); `ctest` no cambia.
  - seguridad: NO-APLICA. Es código 6502 generado; no hay entrada nueva en el host.
  - trazabilidad: PASS.
    - `INITAD` entre segmentos y `RUNAD` = AC6.
    - BASIC apagado = AC7.
    - El error de lectura cumple *Behaviour/Fallos* de la spec.
  - `check_layering`, `check_notices`, `make_menu --check` y `make_xex_loader --check`: OK.
- Riesgo `rsc risk`: nivel 1, un revisor (correctitud). No encontró nada obligatorio; dejó 5 detalles menores:
  - 1, basura tras el texto de error: descartado. `screen_codes` rellena con espacios hasta 40 columnas
    (`tools/make_menu.py:146`); la prueba ahora lo comprueba.
  - 2, la prueba no miraba la lista de visualización: corregido, ahora comprueba sus bytes y el salto `JVB`.
  - 3, faltaban casos: se agregaron dos pruebas, una cabecera `$FFFF` partida entre sectores y un segmento que
    termina en `$FFFF`.
  - 4, un `.xex` cortado salta a `RUNAD` sin aviso: queda para X002, donde `wrap` lo rechaza como `Invalid`
    (AC8).
  - 5, un segmento en la página 1 (la pila) rompería el cargador: decisión abierta para X002 (¿se agrega
    `$0100`-`$01FF` a la zona prohibida?).
