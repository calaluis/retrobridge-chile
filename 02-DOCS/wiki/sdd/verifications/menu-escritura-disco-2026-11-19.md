---
type: verification
title: Verificación — menu-escritura-disco — 2026-11-19
description: Veredicto de cierre de la rama feat/menu-escritura-disco (W001 a W007 y corrección del re-montaje).
tags: [sdd, verification, menu-escritura-disco]
timestamp: 2026-11-19T23:59:00Z
topic: sdd
slug: menu-escritura-disco
source_state: 039ef94 (dirty: prueba de AC8 y corrección del re-montaje sin commit)
---

# Verificación — menu-escritura-disco — 2026-11-19

Rama `feat/menu-escritura-disco`, 0 commits detrás de `origin/main` (57b8b0a). Una corrida después de la
última edición.

## Gate local

- `cmake --build build/host` + `ctest --test-dir build/host`: 64 de 64 en verde (`test_host_write`: 11 casos).
- `retrobridge_host.exe` compila (`cmake --build $TEMP/rb-host-w005`).
- `python -I -m unittest discover -s tools/tests`: 141 en verde.
- `tools/check_layering.py`: OK. `tools/check_notices.py`: OK.
- `rsc risk` de la rama: tier 1, un revisor.

## Tareas

- [x] W001 a W006: controles en `progress/menu-escritura-disco.md`.
- [x] W007: ADR-019, enmiendas de ADR-016 y ADR-017, README, `fuentes.md` (commit 039ef94).

## Criterios de aceptación (spec, líneas 110-125)

- [x] AC1 `P` y lectura — `test_put_and_write_are_read_back_and_survive_a_restart`; demo W006.
- [x] AC2 `W` con COMPLETE — la misma prueba; demo W006 (8 escrituras `complete`).
- [x] AC3 persiste tras relanzar — la misma prueba; demo W006 (`PRUEBA.BAS`).
- [x] AC4 ATR de solo lectura — `test_read_only_file_is_protected_marked_and_unchanged`; demo W006 (SHA igual).
- [x] AC5 imagen TNFS — `test_tnfs_image_is_protected`.
- [x] AC6 menú en D1 — `test_menu_on_d1_refuses_writes`.
- [x] AC7 marca « SL» — `test_read_only_file_is_protected_marked_and_unchanged` y `test_menu_tnfs`.
- [x] AC8 reintento — `test_retried_write_leaves_the_file_as_one_write` (nueva en el cierre).
- [x] AC9 fallo al guardar — `test_failed_save_answers_error_with_cause_io`.
- [ ] AC10 firmware ESP32 — los componentes compartidos pasan en ctest con `writes = false` por defecto; el
  build ESP-IDF lo corre solo el CI. Pendiente hasta el push.
- [x] AC11 pruebas de los menús anteriores — en verde dentro de los 64.

## Revisión de cierre

- Un revisor (refuter-correctness) sobre toda la rama.
- **Corregido** (should-fix): volver a montar la misma ATR en la misma unidad la dejaba protegida. El host abría la
  fuente nueva con la vieja aún abierta como escritora. Opción A, aprobada por el responsable: `mountOn` pide
  primero desmontar y monta al aplicarse el desmontaje. Prueba `test_remounting_the_same_atr_keeps_it_writable`:
  falló antes (marca « SL») y pasa después. Revisión de la corrección: sin hallazgos.
- **Aceptado** (nit): los bits D1 a D3 de STATUS siguen tras cambiar de disco, hasta la siguiente `R`/`W`/`P`.
- **Desviación anotada**: un `W`/`P` con sector fuera de rango recibe NAK al comando, como la lectura; la spec
  (línea 106) dice ERROR. Ningún criterio de aceptación lo cubre.
- **Consecuencia de la opción A**: si el montaje que sigue al desmontaje falla, la unidad queda vacía con
  « ERROR: ...» en pantalla.

## Plan de vuelta atrás

- `git revert` del merge del PR. No hay migraciones ni datos. Las ATR escritas no vuelven atrás: se recuperan
  de una copia.

## Capas no ejecutadas

- **HERRAMIENTA-AUSENTE** — build del firmware ESP-IDF (AC10): sin `idf.py` en este PC; lo corre el CI.
- **SUSTITUIDA** — sanitizers (`-DRB_SANITIZE=ON`): no se corrieron en este PC; corrió el build normal, que no detecta
  accesos fuera de rango ni conducta indefinida. Los corre el CI.

## Hallazgos descartados

- Mailbox ocupado al montar desde `collect()`: imposible; `takeReply()` deja el buzón libre
  (`inline_drive_mailbox.hpp:34`).
- Índice del catálogo que apunta a otro archivo: el catálogo local se llena una sola vez (`host/src/main.cpp:378`).

## Veredicto: FALLA por ahora — falta AC10 (firmware) y los sanitizers, que solo corre el CI. El resto pasa.
Con el CI en verde tras el push, el veredicto pasa a PASA.
