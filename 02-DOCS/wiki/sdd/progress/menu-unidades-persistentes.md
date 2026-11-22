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
- T5 BREAK (completada): `4e157c3`, en main por el PR #78 (`ef77ccf`); CI #306 verde, 85/85 con ASan/UBSan. Código reservado `$F0` (`kMenuBreakKey`); `onBreak()` en el
  controlador; `resend`, `brk` y `brk_hold` en `make_menu.py`. Pruebas: `test_menu_break.cpp` (C7, 7 casos),
  `test_a_mount_whose_reply_was_lost_to_break_is_applied_once_then_break_shows_main` (C11 en el transporte) y
  9 pruebas de Break en `test_menu_program.py`. Altirra: `02-DOCS/raw/2026-11-22-t5-break-altirra.md`.
- **T6 pegamento ESP (evidencia de banco registrada, cierre pendiente):** rama `feat/menu-unidades-esp` desde `ef77ccf`; `1efe7d4`
  y corrección posterior. `beginRestore()` en `start()`; `finish()` lee catálogo y ranuras una vez, arma el plan
  (`planDriveMemory`, `drive_memory_link`) y lo publica con release; la tarea SIO solo toca el plan y las colas tras
  verlo con acquire, y las colas solo si el guardado está activo. Guardado activo solo con `rb_drive_save` en marcha y
  primera lectura Loaded o Empty; tras Error o fallo de microSD queda desactivado toda la sesión (`NO SE GUARDO`).
  Regresión `test_a_failed_first_read_keeps_saving_off_and_e0_survives` (E0 ilegible al arrancar, la tarjeta se
  recupera, cambio de unidad: funciona, `NO SE GUARDO`, E0 al reiniciar); el mutante «guardar tras Error» la hace
  fallar. `check_sio_calls` en 45; sin `nvs_*`. Banco en la FN32ROV-1.3-Q24 con `0679621` (`test/menu-unidades-banco` =
  `main` + #80 + #81): confirmados el arranque USB con microSD sin pánicos, el guardado y la restauración solo
  por SIO (A arranca la ATR restaurada); NVS intacta. Falta la traza de guardado en consola, y C10 (regreso al
  menú) es parcial: falta confirmar que la FujiNet siguió en USB y que D1 quedó lista antes de la primera trama.
  Registro:
  `docs/mediciones/menu-unidades-persistentes-banco.md`. El paquete `7a1ebf0` hacía pánico por desborde de pila
  de `app_main`; el arreglo está en #81, pendiente de fusión.
- T7 empaquetador y banco: guarda `nvs_*` en #80 y guarda de pila en #81, ambas en verde y probadas en el banco;
  pendiente la fusión de los dos PR.

## Evidencia de T2 y T3

- CI #302, ejecución https://github.com/calaluis/retrobridge-chile/actions/runs/44981659165 sobre `b9ce82c`:
  pruebas de host con ASan/UBSan, 84/84 verdes; compilación ESP-IDF verde; capas y avisos de terceros verdes.

## Cola de decisiones

- Ninguna abierta.
