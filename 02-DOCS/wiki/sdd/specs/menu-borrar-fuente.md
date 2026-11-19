---
type: spec
title: Spec — Borrar una fuente desde el menú del Atari
description: QUÉ y POR QUÉ de borrar un servidor TNFS de --sources desde el Atari, con E y el texto vacío.
tags: [sdd, spec, host, menu, tnfs]
timestamp: 2026-11-19T22:00:00Z
topic: sdd
slug: menu-borrar-fuente
status: approved
---

# Spec — Borrar una fuente desde el menú del Atari

Origen: pendiente del README del host («borrar una fuente») y `menu-editar-servidor` (borrar quedó para otra spec).
IDs de requisito: M44-n.

## Problem & why

- `E` agrega y cambia servidores, pero no los quita. Una fuente que sobra exige editar `--sources` en el PC.
- Con 7 fuentes llenas no se puede agregar otra desde el Atari.

## Cost of not building it

- La gestión de fuentes desde el Atari queda a medias. El PC sigue haciendo falta.

## The cheapest alternative

Es la elegida: reusar `E`. Borrar todo el texto con Delete y pulsar RETURN quita la fuente. Sin tecla nueva y sin
cambios en el 6502.

## Goals

- Con `E` sobre una fuente 2 a 8 configurada, texto vacío + RETURN la quita de `--sources`.
- Vale sin relanzar el host.

## Non-goals / out of scope

- La fuente 1 (local).
- Confirmación aparte y deshacer desde el Atari (decisión del responsable: E con texto vacío, sin pregunta extra).
- Reordenar fuentes.
- Hardware real (solo Altirra).

## What the user can do

1. Marcar una fuente 2 a 8 y pulsar `E` (como hoy).
2. Borrar todo el texto con Delete.
3. RETURN borra la fuente. Esc cancela, como hoy.

## What result they get

- Fuente configurada: desaparece de la principal, las siguientes suben un número y sale `FUENTE n BORRADA`.
  Registro: `menu_source_deleted | source=n name=<nombre>`.
- Fila vacía + texto vacío + RETURN: nada cambia, como Esc (igual que hoy).
- Error al escribir: `NO SE PUDO GUARDAR EL ARCHIVO`; el archivo y la pantalla no cambian.
- La fila de la edición dice `RETURN GUARDA, ESC CANCELA`; cambia a `RETURN GUARDA (VACIO BORRA), ESC CANCELA`
  si cabe en 40 columnas, o a otro texto corto que lo avise (decisión de plan).

## What it must respect

- Cambia una decisión aprobada de `menu-editar-servidor` (texto vacío = Esc). Ahí se anota la revisión.
- Formato de `--sources` (M40): una fuente por línea, `#` comenta, líneas vacías se saltan. La línea se quita
  (decisión del responsable); las fuentes siguientes se renumeran.
- La escritura es atómica como en M43 (`.tmp` y luego reemplazo); conserva comentarios y las otras líneas.
- Las sesiones TNFS de la fuente borrada y de las que cambian de número se cierran (UMOUNT); la próxima apertura
  usa el servidor que quedó en ese número.
- Las imágenes ya montadas en D1..D8 siguen montadas: están en la memoria del PC.
- Si una carga TNFS está en curso, `E` no se acepta (como hoy).
- Solo cambia el host: el 6502 ya envía el texto vacío y `$E0`. `menu_program.inc` no cambia (`--check`).
- ADR-017: se enmienda (Enmienda 7). M40-2 (reintento sin efecto doble), M41-1 (250 ms) y ADR-011 siguen.
- ADR-012: sin código de FujiNet ni de emuladores.

## Riesgo aceptado

- Un RETURN con el texto vacío borra sin preguntar. Esc antes de RETURN es la única salida. El responsable lo
  eligió frente a una tecla `B` con confirmación.

## How it proves it

- `test/host/menu`: texto vacío borra la línea y renumera; fila vacía no cambia; Esc no cambia; archivo de solo
  lectura; comentarios intactos; sesiones cerradas y la próxima apertura usa el servidor nuevo del número;
  reintento de `SIOV` sin doble borrado.
- `tools/tests`: siguen en verde sin cambios.
- Demo en Altirra con fuentes 2 y 3: borrar la 2, ver la 3 subir a 2 y abrirla. Evidencia en `D:\rb-e0\m47`.

## Acceptance criteria

- Given fuentes 2 (`a`) y 3 (`b`), When marca la 2, pulsa `E`, borra el texto y pulsa RETURN, Then la principal
  muestra `b` como 2, el archivo ya no tiene `a` y sale `FUENTE 2 BORRADA`.
- Given la edición con texto vacío, When pulsa Esc, Then pantalla y archivo quedan como antes.
- Given la fila 5 vacía, When pulsa `E` y RETURN sin texto, Then nada cambia.
- Given un archivo de solo lectura, When borra, Then sale `NO SE PUDO GUARDAR EL ARCHIVO` y nada cambia.
- Given comentarios en el archivo, When borra, Then los comentarios siguen.
- Given la fuente 3 abierta antes, When borra la 2 y abre la nueva 2, Then lista la carpeta del servidor `b`.
- Given un reintento de `SIOV` con el mismo `K` de guardar, When llega, Then se borra una sola fuente.
- Given las pruebas de #40, `menu-tnfs`, `menu-navegacion` y `menu-editar-servidor`, When corren, Then siguen
  en verde (la que esperaba «vacío = Esc» en una fuente configurada cambia a borrar).

## Enfoques considerados

1. **`E` + texto vacío + RETURN borra (elegido por el responsable).** Solo host; sin tecla nueva.
2. **Tecla `B` con confirmación.** Más segura ante un toque accidental; descartada por el responsable.
3. **Comentar la línea en vez de quitarla.** Recuperable a mano; descartada: el archivo queda limpio.

## Points to clarify

- **suposición tomada** — el aviso de la fila de edición cambia para decir que vacío borra. *Riesgo:* bajo.
- **suposición tomada** — el reintento de `SIOV` se detecta como hoy (M40-2): el `K` repetido no se aplica dos
  veces. *Riesgo:* un reintento tras borrar no puede borrar la fuente siguiente; lo cubre una prueba.

## Clarifications

- 2026-11-19 — clarify, dos preguntas al responsable: borrar con `E` y texto vacío (no tecla `B`); quitar la línea
  (no comentarla).

## Revisions

- 2026-11-19 — borrador con clarify. Aprobada por el responsable («Apruebo ambas»), base origin/main.
- 2026-11-19 — demo en Altirra aprobada («Demo hecha, bien»). `D:\rb-e0\m47\host.jsonl`: `menu_source_deleted`
  de la 3 y de la 2, luego `menu_source_saved` de la 2; `sources.txt` conserva el comentario.
