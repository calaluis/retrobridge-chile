---
type: progress
title: Progreso — cassette-cas
description: Avance por tarea de la spec cassette-cas (cargar cintas .cas de nivel 1 desde el menú).
tags: [sdd, progress, menu, cassette, cas]
timestamp: 2026-11-20T22:00:00Z
topic: sdd
slug: cassette-cas
status: in-progress
---

# Progreso — cassette-cas

Rama `feat/cassette-cas` (worktree `.worktrees/feat-cassette-cas`, desde main `b3183dc`). Plan en `d2650e0`.

## C001 — Fuente A8CAS y CasImage (hecha, 2026-11-20)

- Fuentes en `docs/fuentes.md`:
  - formato A8CAS (a8cas.sourceforge.net, *CAS file format - description*);
  - código propio adaptado (FujiNet PR #1756, commit 6ea2070, `casFSK`), ADR-012 enmienda 1.
- `host/src/cas_image.{hpp,cpp}`: `CasImage::index` → `Ok` con bloques, `Invalid` o `Pulses`.
- Desviación 1 del plan (menor, sin cambio de alcance):
  - los bloques `pwm*` también dan `Pulses`: la fuente los describe como señales de pulsos que el nivel 1 no puede
    reproducir; el plan solo nombraba `fsk `.
  - la fuente no dice si un bloque desconocido se puede saltar; saltarlo es decisión propia (anotada en
    `fuentes.md`).
- Controles:
  - pruebas: `test_cas_image` 17/17 (RED 7 fallos contra el esqueleto, luego verde; 5 más tras la revisión); `ctest` 68/68;
  - compilación: sin avisos con `-Werror`;
  - seguridad: función pura sin E/S; límites comprobados antes de cada lectura;
  - trazabilidad: AC6 (índice de velocidad y pausa), AC8 (`fsk ` solo y `pwm*`), AC9 (sin `FUJI`, cabecera y
    bloque cortados, `fsk ` impar, sin datos).
  - `check_layering` OK; `check_notices` OK; `rsc risk`: tier 1, un revisor.
- Revisión (1 revisor, ronda 1), 4 hallazgos, todos corregidos:
  - la suma de un silencio `fsk ` largo daba la vuelta en 32 bits → `silenceUs` de 64 bits; prueba con 656 × `$FFFF`;
  - `baud` 0 se aceptaba → `Invalid` (decisión propia: una velocidad 0 no se puede reproducir);
  - un `data` vacío contaba como dato → queda como silencio de su pausa; una cinta solo con `data` vacíos es
    `Invalid`, y con `fsk ` es `Pulses`;
  - dos pruebas de fallo no comprobaban la cinta vacía → ahora sí.

## Decision queue

- (vacía)
