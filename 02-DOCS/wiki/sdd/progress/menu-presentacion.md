---
type: progress
title: Progreso — menu-presentacion
description: Avance por tarea de la spec menu-presentacion (carga sin barrido y pie con todas las teclas).
tags: [sdd, progress, menu, 6502]
timestamp: 2026-11-20T07:00:00Z
topic: sdd
slug: menu-presentacion
status: in-progress
---

# Progreso — menu-presentacion

Rama `feat/menu-presentacion` (worktree `.worktrees/feat-menu-presentacion`, desde main `e73b3e7`).

## P001 — Pie principal nuevo (hecha, 2026-11-20)

- `tools/make_menu.py`, `main_footer()`: fila 1 `[1-8] FUENTE`, `[E] SERVIDOR`, `[N] NUEVA ATR`; fila 2
  `[A] ARRANCAR D1`, `[U] DESMONTAR UNIDAD`. Cada fila cabe en 40 columnas (la fila 2 termina con 3 ceros).
- `host/src/menu_program.inc` regenerado; solo cambian los 80 bytes del pie en `kMenuMainTemplate` y en
  `kMenuBlock`.
- Prueba: `test_main_template_has_title_subtitle_and_the_reference_footer` con el texto nuevo. Primero en rojo
  (1 fallo de 47), después en verde.
- Controles:
  - pruebas: PASS. `tools/tests` 141 de 141; `ctest` 65 de 65.
  - compilación: PASS. MinGW ucrt64, `-Werror`.
  - seguridad: NO-APLICA. Solo texto de pantalla; ninguna entrada nueva.
  - trazabilidad: PASS. Pie = AC3; pruebas anteriores en verde = AC8.
  - `check_layering` y `check_notices`: OK. `make_menu.py --check`: al día.
- Riesgo `rsc risk`: tier 1, un revisor (correctitud): sin hallazgos. El host y sus pruebas no dependen del texto
  del pie, solo de sus posiciones (`menu_image.cpp:12`, `test_host_menu.cpp:294-298`); las 5 teclas del pie las
  atiende la vista principal (`menu_controller.cpp:312-421`). Nota menor: la fila 1 ocupa las 40 columnas, sin
  margen.

## P002 — Búfer oculto, aviso y copia (hecha, 2026-11-20)

- `tools/make_menu.py`:
  - `send`: salvo `POLL`, `JSR notice` escribe ` CARGANDO...` en la fila de mensajes del layout visible
    (`main_message` o `list_message`). `DBUFLO/DBUFHI` apuntan a `hidden`.
  - `hidden`: RAM en la primera página tras el bloque (`$1000`-`$13C2`), no se carga del disco. El generador falla si
    no cabe bajo `$4000`.
  - `answered`: espera un cambio de `RTCLOK` y `JSR show`: lista de visualización del layout nuevo en `SDLSTL` y en
    `DLISTL` (`$D402`), y copia `hidden` -> `reply` página a página, de arriba hacia abajo (`copy_pages()`).
  - Error de SIOV: no se copia nada; `nohost` igual que antes, y además pone `layout` = 0 (hallazgo del revisor).
- `host/src/menu_program.inc` regenerado: `kMenuLoadSectors` pasa de 16 a 17.
- Pruebas (`test_menu_program.py`), 7 nuevas: búfer oculto en su página y bajo `$4000`; durante la carga de la lista
  se ve la principal con el aviso; aviso en la fila de la lista; `POLL` sin aviso; copia tras el borrado vertical con
  `DLISTL` y `SDLSTL`; respuesta cortada que nunca llega a la pantalla; aviso en la fila principal tras un error con
  la lista. Primero en rojo (6 fallos y un error de búfer), después en verde.
- Intérprete: la copia se cuenta a la velocidad real (`REAL_STEPS_PER_FRAME` = 6000); fuera de `show` el reloj es
  el de antes. El simulador acepta `("cut", datos)`: bytes escritos y error de SIOV.
- **Cambio de tiempo medido:** cada respuesta del host se muestra tras un borrado vertical, un cuadro que la
  repetición del joystick no cuenta. La repetición que va al host pasa de 30/9 a 31/10 cuadros (6 movimientos por
  segundo en vez de 6,7). Dos pruebas ajustadas, con el motivo en un comentario. El cursor local de la lista no
  cambia: no pasa por el host.
- Controles:
  - pruebas: PASS. `tools/tests` 148 de 148; `ctest` 65 de 65.
  - compilación: PASS. MinGW ucrt64, `-Werror`, 0 avisos; `retrobridge_host.exe` compila (P003).
  - seguridad: NO-APLICA. Sin entradas nuevas; el búfer tiene tamaño fijo y su límite lo comprueba el generador.
  - trazabilidad: PASS. Carga sin barrido = AC1, AC2; `POLL` = AC4; edición = AC5 (pruebas de edición en verde);
    respuesta cortada = AC6; arranque = AC7; pruebas = AC8; fila de la lista = AC9.
- Riesgo `rsc risk`: tier 1 (previsto 2), un revisor (correctitud). Sin bloqueos. Dos notas:
  1. Tras un error con la lista en pantalla, el aviso iba a la fila de la lista, que no se ve. Corregido con prueba.
  2. La copia cuesta 14 ciclos por byte; podría ir justa contra el haz y mostrar la fila de mensajes vieja un
     cuadro. El intérprete no lo ve: se valida en Altirra (P004).

## P003 — Host (hecha, 2026-11-20)

- `ctest` 65 de 65 con el `.inc` nuevo (imagen del menú con 17 sectores); `retrobridge_host.exe` compila.

## Pendiente

P004 (demo en Altirra), P005 (docs).
