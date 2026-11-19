---
type: progress
title: Progreso — Menú de arranque (Issue #38)
description: Registro del bucle de implementación del menú de arranque, con evidencia y cola de decisiones.
tags: [sdd, progress, host, netsio, menu]
timestamp: 2026-11-17T22:20:00Z
topic: sdd
slug: menu-arranque
branch: feat/38-menu-arranque
status: in-progress
---

# Progreso — Menú de arranque (Issue #38)

Spec aprobada 2026-11-17. Plan: `plans/menu-arranque.md`. Modo: autopiloto hasta el PR.

## Tareas

- T0 spike (hecho): COLDSV por software no envía FF. 13 arranques, 1 `COLD_RESET` (el del Shift+F5).
  `D:\rb-e0\m38\spike`.
- T1 `make_atr.py --demo` (hecho): `demo-uno`, `demo-dos` (mensajes distintos), `rota` (magia ATR en cero).
  `rb-boot.atr` sigue con sha256 9bd67a98...; juego de la Meta A sin cambios.
- T2 `make_menu.py` + `menu_program.inc` (hecho): bloque de 10 sectores en $0700; display list en $078D-$07AE
  (sin cruzar 1 KB); texto en $07C4-$0B84 (sin cruzar 4 KB). Intérprete 6502 propio en
  `tools/tests/test_menu_program.py`: tecla válida, Shift, letra fuera de lista, rechazo con línea del host y
  segunda elección, SIOV sin respuesta, archivo versionado igual al generado.
- T3/T4 catálogo, disco del menú, repositorio, controlador y decorador (hecho): `test/host/menu/test_host_menu.cpp`,
  19 casos.
- T5 `main.cpp --images`, README, ADR-017 (hecho): `host/tests/test_log_collision.py` con 5 casos nuevos de
  `--images`.
- T6 demo en Altirra (hecho): ver Evidencia.
- T7 controles y revisión (en curso).

## Evidencia

- `python -I -m unittest discover -s tools/tests`: Ran 102 tests, OK (84 previas + 18 nuevas).
- `ctest --test-dir build/host-tests` (build nuevo): 49/49, incluidos `test_meta_a` y `test_host_menu`.
- `ctest --test-dir build/retrobridge_host`: 1/1 (11 casos: 3 previos de colisión de log, 5 de `--images` y la clase
  base sin casos).
- `check_layering` OK, `check_notices` OK. `rsc risk --base origin/main`: tier 1, un revisor, sin aprobación humana
  obligatoria.
- Truncamiento de `snprintf` como en GCC/glibc (copia con `__builtin_snprintf`, `-O2 -Wall -Wextra -Werror`): falló
  en `menu_controller.cpp` (« ARRANCANDO » + 30 columnas no cabían en 40). Corregido: el nombre usa lo que deja el
  prefijo. Repetido: sin avisos.
- Demo en Altirra 4.31 + hub v0.16 + `retrobridge_host --images build/menu-demo --peer netsiohub` lanzado una vez
  (`D:\rb-e0\m38\run1`, 2026-11-17, 22:08 a 22:12 UTC), teclas enviadas a Altirra, ningún disco montado en Altirra:

  | Paso | Captura | Log del host |
  |------|---------|--------------|
  | Shift+F5 → menú con A, B, C | `01-menu.png` | `menu_mount D1=menu` (al inicio); FF 00 sin cambio |
  | A → arranca DEMO-DOS | `02-arranca-A.png` («DEMO DOS OK») | `menu_select index=0 demo-dos.atr accepted`, `D1=image` |
  | Shift+F5 → menú | `03-menu.png` | FF 37, `menu_return`, `D1=menu` |
  | B → arranca DEMO-UNO | `04-arranca-B.png` («DEMO UNO OK») | `menu_select index=1 demo-uno.atr accepted`, `D1=image` |
  | Shift+F5 → menú | `05-menu.png` | FF 6E, `menu_return`, `D1=menu` |
  | C → error en el menú | `06-error-C.png` («ERROR: ATR INVALIDA») | `menu_select index=2 rota.atr refused: error 5` (InvalidAtrHeader); nada montado |
  | A → arranca DEMO-DOS | `07-arranca-A.png` | `menu_select index=0 accepted`, `D1=image` |

  Resumen: 49 tramas, 49 respuestas 81, 0 vacías, 0 malformadas, 3 `COLD_RESET`, 4 comandos de menú, 0 NAK,
  0 tramas descartadas tras FF. sha256: `menu.jsonl` fe793a2d..., `hub.log` cb279f83..., `demo-dos.atr` aa96d7dd...,
  `demo-uno.atr` b39fe314..., `rota.atr` 682c22c4....
- No ejercido en Altirra: el descarte de una trama tras FF (0 en la corrida; el SO tarda unos 1,8 s desde el FF
  hasta su primer comando). Lo cubre `test_frame_after_a_cold_reset_is_dropped_until_the_menu_is_back`.
- No probado: hardware, modalidad física, POSIX real del host (la CI no compila `host/`; las piezas del menú sí se
  compilan y prueban en `test/host`).

## Revisión (tier 1, un revisor de contexto nuevo, lente de corrección)

- Sin hallazgos bloqueantes ni «should-fix». Decodificó a mano el bloque 6502, la propiedad de las fuentes, el
  orden FF/servicio/paso, el modo `--image` y los límites.
- Corregidos: nombres con `:` o `\` ya no entran al catálogo; el log escapa bytes >= 0x80 (rutas ANSI) para seguir
  siendo UTF-8; la prueba 6502 fija las 20 teclas y llena el DCB con basura para detectar un store olvidado;
  comentario en `serve()` sobre por qué `afterStep()` corre también tras pasos Idle.
- No corregidos, quedan como límites conocidos: ninguna prueba ejercita `serve()` ni `ResetWatch` (están en
  `main.cpp`; los cubre solo la demo en Altirra); sin prueba de un reinicio con montaje en vuelo ni de un montaje
  aplicado que falle (trazados como correctos por el revisor).
- Tras las correcciones: tools 102 OK, `test/host` 49/49, `retrobridge_host` 1/1, `check_layering` y
  `check_notices` OK.

## Entrega

- Commit 7bdc5e7 en `feat/38-menu-arranque`, push con aprobación del responsable; PR #39
  (https://github.com/calaluis/retrobridge-chile/pull/39). Al crearlo: 4 checks en verde, otros aún en curso.
- Las capturas no se adjuntaron al PR (el cuadro de comentarios de GitHub solo admite archivos por el selector
  nativo); quedan en `D:\rb-e0\m38\run1` y el PR lo dice.
- Antes del commit: `make_menu.py --check` tolera CRLF (core.autocrlf en Windows convertiría el `.inc`).

## Corrección pedida en el PR #39 (2026-11-17): proteger todas las ATR de la carpeta

- Pedido del responsable: antes de abrir el log, proteger todas las `.atr` de la carpeta, incluidas las que quedan
  fuera de las 20 del menú; regresión con 21 imágenes; límite de 20 en pantalla; dejar constancia en el PR de que
  la constitución ASDD sigue en borrador.
- Cambio: `findAtrIdenticalTo()` en `host/src/file_identity.cpp` recorre todas las `.atr` de la carpeta (cualquier
  nombre) y compara por identidad con `--log` antes de `fopen`. En Windows usa la API ancha (nombres fuera de la
  página ANSI incluidos) y, si el volumen no da `FileIdInfo` (FAT32/exFAT), el índice clásico. Si no puede listar
  la carpeta o identificar una `.atr` con el log ya existente, rechaza (falla cerrado).
- Pruebas: `test_log_naming_the_atr_beyond_the_menu_limit_is_refused` (21 imágenes, `--log images/img19.atr`, la 21.a):
  código 2, bytes y mtime intactos, ningún archivo nuevo. Falla con el `main.cpp` de 7bdc5e7 (no rechazaba).
  `test_the_menu_still_shows_twenty_of_twenty_one`: 20 `menu_entry`, `entries=20 atr_seen=21`.
  `test_log_as_8dot3_alias_of_a_non_ansi_atr_is_refused` (Windows): falla con la primera versión de la corrección
  (API ANSI; salió con 1 y escribió el log) y pasa ahora.
- Revisión: ronda 1 encontró el agujero de nombres no ANSI (reproducido); ronda 2, sin bloqueantes, pidió el respaldo
  para FAT y una aserción más precisa; ambos hechos (ronda 3, la última permitida, sin nueva revisión).
- Controles: `retrobridge_host` 11/11, `test/host` 49/49, tools 102 OK, `check_layering` y `check_notices` OK,
  truncamiento estilo glibc sin avisos, rama POSIX de `file_identity.cpp` compilada con el g++ de MSYS (POSIX,
  sin `_WIN32`) sin avisos. `rsc risk`: tier 1.
- No probado: el respaldo FAT (no hay volumen FAT montado; trazado), la rama POSIX ejecutada en Linux, un flujo
  alternativo de NTFS (`x.atr:stream`) como `--log` (añadiría un flujo a la imagen sin tocar sus datos; anotado).

## Decision queue

- [review-after] Q1 — Constitución en borrador aplicada como ratificación provisional, `constitution.md`, todas
- [review-after] Q2 — Pantalla de referencia del menú escrita por el agente y aprobada con la spec, `specs/menu-arranque.md`, T2
- [review-after] Q3 — Límites: 20 imágenes, letras A a T, nombres a 30 columnas en la lista, `specs/menu-arranque.md`, T3
- [review-after] Q4 — ID de dispositivo propio $5A sin referencia externa que lo reserve, `docs/adr/0017-menu-de-arranque.md`, T4
