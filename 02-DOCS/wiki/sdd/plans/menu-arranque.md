---
type: plan
title: Plan — Menú de arranque (Issue #38)
description: CÓMO se construye el menú de arranque de retrobridge_host; incluye las tareas.
tags: [sdd, plan, host, netsio, menu]
timestamp: 2026-11-17T22:20:00Z
topic: sdd
slug: menu-arranque
branch: feat/38-menu-arranque
status: approved
---

# Plan — Menú de arranque (Issue #38)

Spec: `specs/menu-arranque.md` (aprobada 2026-11-17). Modo: autopiloto hasta el PR.

## Spike (T0, hecho)

Pregunta: ¿un reinicio por software (`JMP $E477`, COLDSV del SO) envía FF por NetSIO?
Imagen de prueba propia que salta a COLDSV 3 s después de arrancar (`D:\rb-e0\m38\spike`).
Resultado (2026-11-17 21:55 UTC): 13 arranques (13 READ del sector 1, uno cada 4,5 s) y un solo `COLD_RESET`,
el del Shift+F5 inicial. El reinicio por software no envía FF. El riesgo de la spec queda cerrado.

## Arquitectura

Todo en `host/src`; ningún componente cambia (`rb_sio`, `rb_netsio`, `rb_application` intactos).

```text
hub UDP <-> NetSioSioTransport --observer--> ResetWatch (FF) --> JsonlLog
                    ^
            MenuSioTransport (decorador SioTransport)
              | marco para el dispositivo $5A -> lo atiende él (ACK, COMPLETE + 41 bytes)
              | cualquier otro marco          -> SioSession -> SioCommandDispatcher (D1)
                    |
            MenuController: catálogo, imagen del menú en memoria, MountDiskImage + CollectDriveReply
                    |
            HostImageRepository: "*menu*" -> MenuByteSource (memoria); el resto -> StdioImageRepository<2>
```

- **Protocolo propio (ADR-017):** dispositivo SIO `$5A`, comando `M` (`$4D`), AUX1 = índice (0..19), lectura de
  41 bytes: byte 0 = 0 (montada) o 1 (error), bytes 1..40 = una línea en códigos de pantalla. No usa los IDs de
  FujiNet (`$70`, `$71`) ni el de D1. Un comando distinto de `M` recibe NAK.
- **Catálogo:** se lee la carpeta una vez al arrancar: `.atr` sin distinguir mayúsculas, orden alfabético sin
  distinguir mayúsculas, como máximo 20, nombres válidos como `ImageLocation`. Carpeta sin `.atr` o ilegible:
  código 2.
- **Imagen del menú:** ATR de 720 x 128 generada en memoria: sector 1 a 9 = programa 6502 + display list + texto.
  El programa lo genera `tools/make_menu.py` (ensamblador de `make_atr.py`) en `host/src/menu_program.inc`; una
  prueba de `tools/tests` exige que el archivo versionado sea idéntico al generado.
- **Selección:** `MountDiskImage` abre y valida (`DiskImage::open`). Si falla, nada llega a D1 y la respuesta
  lleva el motivo. Si va bien, el montaje se aplica al comienzo del siguiente `step()`, antes de cualquier otro
  marco; el menú hace `JMP $E477` y el SO arranca desde la imagen nueva.
- **FF:** el observador marca un reinicio en frío. Si el marco devuelto en esa misma espera es posterior al FF,
  el decorador lo descarta (respuesta vacía; el Atari reintenta) para que nunca se sirva con la imagen anterior.
  El bucle principal monta el menú en cuanto el buzón está libre. FE (F5) no cambia nada.
- **Log:** `--images` compara `--log` por identidad con cada imagen del catálogo (protección de #36).
- **Programa 6502 del menú:** pone su display list (modo 2, 23 líneas), espera una tecla en `CH` ($02FC), la
  traduce con una tabla de 20 códigos de teclado (A..T, sin Shift ni Control), ignora letras fuera de la lista,
  llama a SIOV ($E459). Si SIOV falla, muestra «ERROR: EL HOST NO RESPONDE»; si el byte 0 es 1, muestra la
  línea del host; si es 0, `JMP $E477`.

## Pruebas

- `test/host` (CI): catálogo (filtro, orden, límite, nombres), imagen del menú (estructura y textos), decorador
  (bytes exactos de ACK/COMPLETE/datos/checksum, NAK, otros marcos pasan, descarte tras FF) y controlador
  (selección válida, inválida, índice fuera, vuelta al menú con FF, sin doble montaje del menú).
- `tools/tests` (CI): imágenes de demo (dos arrancables distintas, una inválida), `rb-boot` sin cambios, juego de la
  Meta A sin cambios, `menu_program.inc` al día, y el programa del menú ejecutado en el intérprete 6502 ampliado
  (tecla válida, tecla fuera de lista, error del host, fallo de SIOV).
- `host/tests` (ctest de `host/`): regresión de colisión de log, más `--images` con log dentro de la carpeta.
- Altirra: recorrido completo de la spec con capturas, JSONL y log del hub.

## Tareas

| # | Tarea | Criterio | Riesgo | Comprobación |
|---|-------|----------|--------|--------------|
| T0 | Spike COLDSV sin FF | Riesgo | 0 | hecho, arriba |
| T1 | `make_atr.py --demo`: demo-uno, demo-dos, rota | CA3, CA5 | 1 | tools/tests verde, rb-boot y Meta A idénticos |
| T2 | `make_menu.py` + `menu_program.inc` + intérprete ampliado | CA2, CA3, CA5 | 1 | tools/tests verde |
| T3 | Catálogo + imagen del menú + repositorio del host | CA1, CA2 | 1 | test/host verde |
| T4 | Decorador `$5A` + controlador + FF | CA3, CA4, CA5 | 1 | test/host verde |
| T5 | `main.cpp --images`, log, README, ADR-017 | CA1, CA6, CA7 | 1 | host ctest verde, `--image` igual |
| T6 | Demo en Altirra | CA1-CA6 | 0 | capturas + JSONL + hub |
| T7 | Controles y revisión de cierre | CA8 | 1 | ctest, tools, layering, notices, `rsc risk` |

Dependencias: T2 depende de T1 (comparten el ensamblador); T3 de T2; T4 de T3; T5 de T4; T6 de T5.
