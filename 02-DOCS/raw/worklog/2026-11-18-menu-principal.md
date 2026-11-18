---
type: worklog
title: Pantalla principal del menú del Atari (Issue #40, PR #41)
description: Terminal delgado con la pantalla de la maqueta, montaje en D1 a D8, mensajes bajo el pie y entrega del PR #41 con CI en verde.
timestamp: 2026-11-18T02:45:00Z
topic: sdd
status: processed
sources:
  - https://github.com/calaluis/retrobridge-chile/issues/40
  - https://github.com/calaluis/retrobridge-chile/pull/41
---

## What we did

- El menú de #38 pasó a ser un terminal delgado: el programa 6502 envía cada tecla al host (`$5A`, comando `K`,
  AUX1 = tecla, AUX2 = secuencia) y muestra la pantalla de 960 bytes que le devuelve el host.
- Pantalla principal de la maqueta de Display List Lab: título, fuentes 1 a 8, «ESTADO DE UNIDADES», D1 a D8,
  «C: SIN CINTA» y pie de dos filas con las teclas en inverso entre `$42` y `$56`.
- Teclas implementadas: `1` (fuente local de `--images`), `A`-`T` y `1`-`8` (imagen y unidad), `A` (arranca D1),
  `U` (desmonta). Pendientes visibles: `2`-`8` (TNFS) y `E` (entrada de texto).
- Nueva opción `--sources` (hasta 7 nombres) y un archivo de fuentes protegido frente a `--log`.
- Revisión humana: la línea de mensajes se movió debajo del pie de teclas, en la pantalla principal y en la lista.
- Entrega: commit `0473338`, push, PR #41 y un comentario con las capturas y el registro de Altirra.

## Why

- Dibujar en el host deja el programa 6502 pequeño y permite cambiar la pantalla sin regenerar la ATR del menú.
- AUX2 evita que una tecla reintentada por `SIOV` se aplique dos veces (corrección de la revisión de tier 1).
- D1 queda asignada hasta `A` para que el menú siga servido desde D1 mientras el usuario elige.
- TNFS y `E` quedan pendientes por decisión del usuario: una tecla dibujada no cuenta como acción implementada.
- El mensaje bajo el pie lo pidió el usuario al ver la captura: encima del pie «se ve mal».

## Files touched

- `tools/make_menu.py`, `tools/make_atr.py`, `host/src/menu_program.inc`: generador 6502 y display lists.
- `host/src/menu_controller.{hpp,cpp}`, `menu_image.{hpp,cpp}`, `menu_sources.{hpp,cpp}`,
  `menu_sio_transport.{hpp,cpp}`, `host_image_repository.hpp`, `main.cpp`: lógica del host.
- `test/host/menu/test_host_menu.cpp`, `tools/tests/test_menu_program.py`, `host/tests/test_log_collision.py`.
- [`docs/adr/0017-menu-de-arranque.md`](../../../docs/adr/0017-menu-de-arranque.md) (enmienda 1, referencia de las
  decisiones del producto), [`host/README.md`](../../../host/README.md).

## Outcome

Shipped and verified in emulation; PR #41 open, not merged.

- tools 106 OK; `test/host` 49/49 (`test_host_menu` 31); `retrobridge_host` 15/15; `rsc risk` tier 1.
- Altirra 4.31 + hub v0.16, `D:\rb-e0\m40\run4` (carpeta local, no versionada): 16 comandos de menú, 0 reintentos, 0 NAK.
  `10-error-rota.png` y `03-lista.png` muestran el mensaje debajo del pie; están adjuntas en el
  [comentario del PR #41](https://github.com/calaluis/retrobridge-chile/pull/41#issuecomment-6967350204).
- CI del commit `0473338`: `pull_request` 5 de 5 en verde; `push` 3 en verde y 2 paquetes de firmware omitidos.
- Rama local igual a la publicada (mismo SHA, `git diff` vacío).

## Open questions / next

- Revisión y merge del PR #41 por el responsable.
- Cliente TNFS propio y servidor de prueba local; entrada de texto para `E`; cassette; escritura en disco.
- No ejercido: que el Atari lea sectores de D2 a D8; hardware real.
- Review-after Q1 a Q4 en [`wiki/sdd/progress/menu-principal.md`](../../wiki/sdd/progress/menu-principal.md).

## Commands

- `python -I -m unittest discover -s tools/tests`: Ran 106 tests, OK.
- `ctest --test-dir build/host-tests`: 100% tests passed out of 49.
- `python -I host/tests/test_log_collision.py build/retrobridge_host/retrobridge_host.exe`: 15 OK.
- `D:\rb-e0\m40\run_main.ps1 -Out D:\rb-e0\m40\run4`: recorrido completo, 11 capturas.
