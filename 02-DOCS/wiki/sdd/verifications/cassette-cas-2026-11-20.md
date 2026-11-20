---
type: verification
title: Verification — cassette-cas — 2026-11-20
description: Veredicto con evidencia del cierre de la fase 1 de cassette-cas (C100–C105) — gate del host, tareas y criterios AC1–AC8 y AC13.
tags: [sdd, verification, cassette, cas, fsk]
timestamp: 2026-11-20T21:30:00Z
topic: sdd
slug: cassette-cas
source_state: b6e86c8 + pruebas de cierre sin commit (dirty: solo test/host/cassette)
---

# Verification — cassette-cas — 2026-11-20

Cierre de la **fase 1** (C108 sobre C100–C105). El plan lo permite así: M3a y H4 siguen cerrados, así que C106 y
C107 esperan. `main` sigue en b3183dc, la base de la rama: no hubo nada que integrar.

## Stack gate

Una sola corrida después del último commit, con carpetas de compilación nuevas (`build/host-c108`,
`build/exe-c108`), MSYS2 ucrt64, Ninja:

- `ctest` del host: 72 de 72 con `RB_CAS_SAMPLES=D:/GitHub/Respaldos/MicroSD`; compilación con `-Werror`, 0 avisos.
- `ctest` del ejecutable: 1 de 1; `retrobridge_host.exe` enlaza, 0 avisos.
- `python -I -m unittest discover -s tools/tests`: 167 de 167.
- `tools/check_layering.py` y `tools/check_notices.py`: OK.

## Task done-checks

- [x] C100 — `ctest` verde; `cassette_run.hpp` ya no está. Commit 8192992.
- [x] C101 — `test_cas_index` verde; AC7 pasa con la carpeta; capas OK. Commit 34321e3.
- [x] C102 — `test_fsk_wave` verde, con Karate y una turbo software reales. Commit 4cb7fa1.
- [x] C103 — `test_fsk_loader` verde, sin reservas, con Zorro real. Commit aa349c3.
- [x] C104 — `test_rb_cassette_player` verde, con Alien Ambush real entera. Commit fe0df1e.
- [x] C105 — `test_cassette_commands` verde, con Alien Ambush real. Commit b6e86c8.
- [ ] C106 — espera la puerta M3a.
- [ ] C107 — espera la puerta H4.

## Acceptance criteria (spec)

- [x] AC1 — `test_real_international_karate` (`test_cas_index`) y `test_real_international_karate_runs`
  (`test_fsk_wave`).
- [x] AC2 — `test_zero_gap_fsk_chunks_join_one_run`, `test_other_chunk_ends_a_run`, `test_run_stops_at_64_chunks`.
- [x] AC3 — `test_levels_alternate_from_space`, `test_long_value_splits_into_portions`, pruebas reales de Karate y
  turbo software en `test_fsk_wave`.
- [x] AC4 — `test_reads_one_block_at_a_time`, `test_gate_waits_for_runway_and_prefill`, `test_gate_needs_the_runway_too`.
- [x] AC5 — registros: pruebas de MOTOR de C002 conservadas en `test_rb_cassette_player`. Tiradas `fsk `: la señal para
  y la tirada se repite desde su pausa, como el código propio. **Desviación del texto** («sin repetir»): en la cola de
  decisiones como review-after; el responsable decide si se ajusta AC5.
- [x] AC6 — `test_cassette_commands`: sin `FUJI`, bloque cortado, `baud` 0, `pwm*`, demasiados bloques y archivo que
  no abre; la cinta montada, su posición y su reproducción no cambian.
- [x] AC7 — `test_every_real_tape_reads` con la carpeta (se salta sin ella).
- [~] AC8 — **parcial**. Los cuatro comandos y sus respuestas: `test_cassette_commands` (27 casos, uno real). La
  consola del ESP32 (`rb_console`) todavía no traduce `cas ...`: el plan lo pone en C106, tras M3a.
- [x] AC13 — gate de arriba en verde; las pruebas de `.ATR` y `.xex` del menú siguen en `ctest`.
- [ ] AC9–AC12 — fase 2 y 3: esperan M3a y H4. No se pueden verificar en el host.

## Rollback plan

- La rama no está en `main`. Para deshacer después de un merge: `git revert` del merge. No hay migraciones ni banderas.
- `rb_cassette` no está enlazado aún a `main/app_main.cpp`: quitarlo no cambia el firmware actual.

## Capas no ejecutadas

- **HERRAMIENTA-AUSENTE** — `idf.py build` (CI lo corre): ESP-IDF no está instalado en este equipo. No se compiló
  `rb_cassette` con el compilador del ESP32 (xtensa); el CI de la PR lo hará.
- **HERRAMIENTA-AUSENTE** — `RB_SANITIZE=ON` (CI lo corre en Linux): MinGW no trae los sanitizadores.
- **HERRAMIENTA-AUSENTE** — `rsc risk` de C105: permiso denegado en la sesión; se usó el tier del plan (1).
- **NO-APLICA** — hardware: fase 1 es solo host por diseño.

## Revisión de cierre

Un revisor (lente de pruebas como evidencia), sobre `fa3c084..b6e86c8`. Sin defectos ni deriva de alcance. 25
mutantes propios: 9 sobrevivían. Cerrados con pruebas, sin tocar código de producción (cada mutante comprobado: ahora
falla):

- `cas_index`: cabecera sin datos que termina justo al final del archivo → `test_header_ending_at_eof_is_ok`;
  primer tipo distinto de `FUJI` exacto → `test_other_first_type_is_invalid` (AC6).
- `cassette_player`: tiempo entre un tick con MOTOR apagado y el siguiente encendido →
  `test_time_after_a_motor_off_tick_does_not_count` (AC5).
- `fsk_loader`: archivo cambiado bajo el índice (bloque más largo que la tirada, o con pausa) →
  `test_changed_chunk_length_or_gap_fails_the_run`; tirada que llena el banco exacto →
  `test_run_filling_the_bank_exactly_fits` (AC4).

Quedan como detalles, sin prueba: tamaño de lectura con varios bloques de largo impar (siempre ≤ 512); ruta de
exactamente 256; texto extra tras `status` (ya da el uso; el mutante que lo acepta sobrevive).

Después: `ctest` 72 de 72 (`test_cas_index` 23, `test_fsk_loader` 16, `test_rb_cassette_player` 26), `tools/tests`
167, capas y avisos OK.

## Hallazgos descartados

- «`tools/tests` no corrió» (revisor) — descartado: `python -I -m unittest discover -s tools/tests` → `Ran 167 tests`,
  `OK`, dos veces en este cierre. El revisor buscaba `pytest` y leyó solo la salida de las pruebas.

## Verdict: PASS para la fase 1, con AC8 parcial

AC1–AC7 y AC13 con evidencia. AC5 sigue el código propio y su texto espera la decisión del responsable (cola de
decisiones). AC8: comandos verificados; el enganche a la consola del ESP32 es C106. AC9–AC12 esperan M3a y H4.
No probado aquí: `idf.py build` y sanitizadores; los corre el CI de la PR.
