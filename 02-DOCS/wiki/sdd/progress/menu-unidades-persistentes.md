---
type: progress
title: Progreso — Unidades montadas que sobreviven al apagado
description: Registro del bucle de implementación de menu-unidades-persistentes, con evidencia por tarea.
tags: [sdd, progress, menu, esp32, microsd]
timestamp: 2026-11-22T12:00:00Z
topic: sdd
slug: menu-unidades-persistentes
branch: feat/menu-unidades-persistentes
status: in-progress
---

# Progreso — Unidades montadas que sobreviven al apagado

Spec: `specs/menu-unidades-persistentes.md`. Plan: `plans/menu-unidades-persistentes.md` (aprobado 2026-11-22).

## Tareas

- T1 `DriveMemory` y `DriveMemoryCodec` (hecho): `e1e4890`.
- T4 `SioPowerWatch` (hecho): `e1e4890`.
- T2 `SlotFileStore` (completada): `bd7f929`; corrección de la prueba C8 en `b9ce82c`. La prueba guarda E0,
  bloquea solo la ranura destino, comprueba que `save()` da error, que la ranura de E0 no cambia y que una
  instancia nueva recupera E0 con `load()`. Dos mutantes de `save()` (error dado por bueno, escritura sobre E0)
  hacen fallar la prueba.
- T3 `MenuController` (completada): `753a823`; enlace de pruebas en `c19ede7`; corrección de la restauración
  prematura en `c3c9e2b`. Entre `beginRestore()` y `restore()` no se aplica nada: D1 sirve el menú, D2–D8 quedan
  vacías y la pantalla muestra `RESTAURANDO UNIDADES` con `A` deshabilitada. Regresión
  `test_service_before_restore_waits_and_mounts_nothing`.
- **T5 BREAK (en revisión del responsable):** código reservado `$F0` (`kMenuBreakKey`); `onBreak()` en el
  controlador; `resend`, `brk` y `brk_hold` en `make_menu.py`. Pruebas: `test_menu_break.cpp` (C7, 7 casos),
  `test_a_mount_whose_reply_was_lost_to_break_is_applied_once_then_break_shows_main` (C11 en el transporte) y
  9 pruebas de Break en `test_menu_program.py`. Altirra: `02-DOCS/raw/2026-11-22-t5-break-altirra.md`.
- T6 pegamento ESP y T7 empaquetador y banco: pendientes.

## Evidencia de T2 y T3

- CI #302, ejecución https://github.com/calaluis/retrobridge-chile/actions/runs/44981659165 sobre `b9ce82c`:
  pruebas de host con ASan/UBSan, 84/84 verdes; compilación ESP-IDF verde; capas y avisos de terceros verdes.

## Cola de decisiones

- Ninguna abierta.
