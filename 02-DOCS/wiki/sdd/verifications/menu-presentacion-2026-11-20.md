---
type: verification
title: Verification — menu-presentacion — 2026-11-20
description: Veredicto con evidencia de menu-presentacion — gate del host y de tools, tareas P001-P005 y criterios AC1-AC9.
tags: [sdd, verification, menu, 6502]
timestamp: 2026-11-20T06:30:00Z
topic: sdd
slug: menu-presentacion
source_state: 22d569b (dirty: solo texto de la spec y de ADR-017 tras el review de cierre; sin código)
---

# Verification — menu-presentacion — 2026-11-20

Rama `feat/menu-presentacion` contra main `e73b3e7` (main sin commits nuevos; nada que integrar). Una corrida
nueva, después del último cambio de código.

## Stack gate

- `ctest --test-dir build/host-close` (compilación desde cero, MinGW ucrt64, `-Werror`): PASS, 65 de 65, 0 avisos.
- `retrobridge_host.exe` (`build/host-exe-close`): compila.
- `python -I -m unittest discover -s tools/tests`: PASS, 148 de 148.
- `python -I tools/make_menu.py --check host/src/menu_program.inc`: al día.
- `python -I tools/check_layering.py`: OK. `python -I tools/check_notices.py`: OK.
- `rsc spec gate`: PASS (4 puntos, todos tipados). `rsc risk` de la rama: tier 1.

## Task done-checks

- [x] P001 pie nuevo — `test_main_template_has_title_subtitle_and_the_reference_footer` en verde; `--check` al día.
- [x] P002 búfer oculto, aviso y copia — 7 pruebas nuevas del intérprete en verde; las de antes en verde (dos de
  repetición del joystick con el tiempo nuevo, motivo en comentario).
- [x] P003 host — `ctest` 65 de 65; el exe compila.
- [x] P004 demo — `D:\rb-e0\m47-presentacion\host.jsonl`, `version.txt`; un solo host; observación del responsable.
  Sin capturas (ver Desviación).
- [x] P005 docs — ADR-017 enmienda 10, README, `docs/fuentes.md`, AC6 precisado; `check_notices` OK.

## Acceptance criteria (spec)

- [x] AC1 principal → lista sin barrido — prueba `test_while_the_list_loads_the_main_screen_stays_with_the_notice`
  y `test_the_new_screen_is_copied_after_a_vertical_blank_with_its_display_list`; en pantalla, observación del
  responsable.
- [x] AC2 lista → principal — `test_while_a_key_on_the_list_loads_the_notice_is_on_the_list_message_row`;
  observación del responsable.
- [x] AC3 pie — prueba del pie (texto exacto); observación del responsable.
- [x] AC4 `CARGANDO nn%` — `test_a_poll_shows_no_notice_over_the_progress_of_the_host`; demo: 1701 `tnfs_request`,
  carpetas TNFS abiertas.
- [x] AC5 edición — pruebas de edición del intérprete en verde (sin cambios). La demo no deja evento al cancelar.
- [x] AC6 respuesta cortada — `test_a_cut_reply_never_reaches_the_screen`; no provocada en la demo.
- [x] AC7 arranque — `test_boot_action_cold_starts` y demás de arranque en verde; demo: `menu_boot D1
  name=11.ATR`.
- [x] AC8 pruebas — 148 y 65 en verde.
- [x] AC9 aviso en la fila de la lista — prueba de la fila de la lista y
  `test_after_no_answer_on_the_list_the_notice_goes_to_the_main_message_row`; observación del responsable.

## Desviación aprobada

La spec (§How it proves it) pide capturas durante la carga. La demo no guardó capturas. El responsable aceptó su
observación en pantalla («Sin barrido, todo bien») en lugar de ellas, el 2026-11-20 («Acepto la observación»).
AC1, AC2, AC3 y AC9 quedan probados en memoria por el intérprete y en pantalla solo por esa observación.

## Rollback plan

- `git revert` del merge del PR. El programa vuelve a 16 sectores y a recibir en la pantalla; el host no tiene
  otros cambios. Sin migraciones ni datos.

## Capas no ejecutadas

- **SUSTITUIDA** — sanitizadores: el CI compila con `-DRB_SANITIZE=ON` (Linux GCC); aquí MinGW sin ellos. No detecta
  aquí errores de memoria del host; el cambio de código del host es solo el `.inc` regenerado.
- **HERRAMIENTA-AUSENTE** — firmware ESP-IDF: sin `idf.py` en este PC; lo compila el CI. El firmware no usa el menú.
- **SUSTITUIDA** — tiempo real del haz: el intérprete no dibuja; la copia contra el haz (nota del revisor de P002)
  solo se vio en la demo, por observación.
- **NO-APLICA** — seguridad: sin entradas nuevas; el tamaño del búfer es fijo y su límite lo comprueba el generador.

## Hallazgos descartados

- Review de cierre, nota: el orden de la copia (de arriba hacia abajo) no tiene prueba propia — aceptado: el
  intérprete no modela el haz; `copy_pages()` (`tools/make_menu.py`) recorre las páginas en orden y la demo no
  mostró barrido.
- Review de cierre, nota: AC7 sin prueba nueva — descartado: `test_boot_action_cold_starts`,
  `test_boot_after_a_wait_still_cold_starts` y `test_an_unknown_action_still_cold_starts` pasan por la ruta nueva
  (`answered` → `show` → acción).
- Review de cierre, should-fix 2 (cambio de tiempo y reset del layout fuera de la spec) — corregido: spec G4 y
  Revisions, ADR-017 enmienda 3 con nota cruzada.

## Verdict: PASS — todas las tareas y criterios con evidencia; una desviación aprobada por el responsable (capturas).
