---
type: progress
title: Progreso — Pantalla principal del menú (Issue #40)
description: Registro del bucle de implementación de la pantalla principal del menú, con evidencia y cola de decisiones.
tags: [sdd, progress, host, netsio, menu]
timestamp: 2026-11-18T01:50:00Z
topic: sdd
slug: menu-principal
branch: feat/40-menu-principal
status: in-progress
---

# Progreso — Pantalla principal del menú (Issue #40)

Spec aprobada 2026-11-17 y plan en `02-DOCS/wiki/sdd/specs/menu-principal.md` y `plans/menu-principal.md`:
solo en la copia local del responsable, no versionados. En GitHub, el alcance está en la [Issue #40](https://github.com/calaluis/retrobridge-chile/issues/40)
y las decisiones del producto en [ADR-017](../../../../docs/adr/0017-menu-de-arranque.md). Modo: autopiloto hasta el
PR (commit, push y PR con aprobación). Rama `feat/40-menu-principal` desde main 750ce10.

## Tareas

- T1 generador 6502 (hecho): `tools/make_menu.py` reescrito como terminal delgado; `make_atr.assemble` acepta
  `extra_labels` (salida de `rb-boot` sin cambios). 10 sectores desde $0700; display lists en $07AC-$07F4 (misma
  página); pantalla en $081F-$0BDE (mismo bloque de 4 KB).
- T2 `MenuSources` y `--sources` (hecho).
- T3 `MenuController::key()` (hecho): pantallas principal, lista, elegir unidad y desmontar.
- T4 transporte `'K'` en bloques de 256, `DriveBay{8}`, repositorio con 9 archivos (hecho).
- T5 demo en Altirra (hecho): ver Evidencia.
- T6 [ADR-017](../../../../docs/adr/0017-menu-de-arranque.md) enmienda 1, [`host/README.md`](../../../../host/README.md) (hecho).

## Evidencia

- `python -I -m unittest discover -s tools/tests`: Ran 106 tests, OK. `test_menu_program.py` comprueba la display
  list principal byte a byte contra la estructura de la maqueta, la de la lista, colores, plantilla y pie (40
  columnas, $42/$56, teclas en inverso), la tecla enviada, la acción de arranque y el host caído.
- `ctest --test-dir build/host-tests` (build nuevo): 49/49; `test_host_menu`: 31 casos, 0 fallos.
- `python -I host/tests/test_log_collision.py build/retrobridge_host/retrobridge_host.exe`: 15 casos OK (4 nuevos
  de `--sources`).
- `check_layering` OK, `check_notices` OK. `rsc risk --base origin/main`: tier 1, un revisor, sin aprobación humana
  obligatoria.
- Truncamiento estilo glibc (`__builtin_snprintf`, `-O2 -Wall -Wextra -Werror -Wformat-truncation=1`) en
  `menu_controller`, `menu_sources`, `main`, `menu_image`, `menu_sio_transport`: sin avisos. Rama POSIX (g++ de
  MSYS, sin `_WIN32`) de `menu_sources`, `menu_controller`, `menu_image`, `menu_sio_transport`: sin avisos.
- Demo en Altirra 4.31 + hub v0.16 + `retrobridge_host --images build/menu-demo --sources sources.txt --peer
  netsiohub`, lanzado una vez, con el ejecutable definitivo (`D:\rb-e0\m40\run3`, carpeta local del laboratorio, no versionada; 2026-11-18, 01:51 a 01:56 UTC),
  teclas enviadas a Altirra, ningún disco montado en Altirra. `sources.txt` tiene nombres de ejemplo
  (`tnfs.ejemplo.cl`, `localhost`); no se contactó ningún servidor. Las corridas `run1` y `run2` (antes de las
  correcciones de la revisión) dieron el mismo recorrido.

  | Paso | Captura | Log del host |
  |------|---------|--------------|
  | Shift+F5: pantalla principal | `01-principal.png` | `menu_mount D1=menu` |
  | `2`: servidor | `02-pendiente-tnfs.png` | `menu_pending source=2 ... needs a TNFS client` |
  | `1`: fuente local | `03-lista.png` | `menu_open source=1 local entries=3` |
  | `B`: elegir unidad | `04-elegir-unidad.png` | (sin evento: solo cambia la vista) |
  | `2`: DEMO-UNO en D2 | `05-d2-montada.png` | `menu_select D2 ... accepted`, `menu_mount D2=mounted` |
  | `1`, `A`, `1`: DEMO-DOS asignada a D1 | `06-d1-asignada.png` | `menu_select D1 name=demo-dos.atr assigned` |
  | `A`: arranca D1 | `07-arranca-d1.png` («DEMO DOS OK») | `menu_boot D1 name=demo-dos.atr`, `D1=image` |
  | Shift+F5: vuelta al menú, D1 y D2 conservadas | `08-vuelta-al-menu.png` | `menu_return`, `D1=menu` |
  | `U`, `2`: D2 desmontada | `09-d2-desmontada.png` | `menu_unmount D2 requested`, `D2=empty` |
  | `1`, `C`, `3`: ATR inválida | `10-error-rota.png` («ERROR: ATR INVALIDA») | `D3 name=rota.atr refused: error 5` |
  | `E`: editar | `11-pendiente-editar.png` | `menu_pending edit server: ...` |

  Resumen del host: 42 tramas, 42 ACK/NAK, 0 vacías, 32 pedidos de crédito, 2 reinicios, 16 comandos de menú,
  0 reintentos, 0 NAK de menú, 0 tramas descartadas tras FF. Cada pantalla (964 bytes) llegó en 4 bloques.
  sha256: `menu.jsonl` 4064fa6d..., `hub.log` dc8f1ad9..., `01-principal.png` 10271c75..., `07-arranca-d1.png`
  2eed8327..., `sources.txt` a197e056....
- No ejercido: que el Atari lea sectores de D2..D8 (ningún programa de la demo los pide; las pruebas de host
  comprueban lo montado en `DriveBay`); el flujo TNFS (pendiente por decisión Q1); hardware.

## Revisión (tier 1, un revisor de contexto nuevo, lente de corrección)

- Ronda 1: sin bloqueantes. Un «should-fix»: si el envío de la respuesta falla a mitad, `SIOV` reintenta la misma
  tecla y el host la aplicaba dos veces (un segundo montaje, o el dígito leído en la pantalla siguiente).
  Corregido: el programa 6502 numera cada tecla en AUX2; el host reenvía la respuesta ya armada si llegan el mismo
  AUX1 y AUX2, sin aplicar la tecla otra vez; un redibujo (`$FF`) borra esa memoria. Pruebas nuevas:
  `test_a_key_repeated_by_siov_is_answered_again_but_applied_once`, `test_a_redraw_forgets_the_last_key` y AUX2 en
  `test_menu_program.py`. Nits: la pantalla «host no responde» puede mostrar líneas viejas (cosmético, anotado);
  Shift+Control+A = $FF inalcanzable; error de arranque de D1 solo en el log (el Atari ya está arrancando); fallo
  al montar el menú en memoria sin disparador real; «la misma imagen en dos unidades con escritura»: falso, las
  imágenes se abren con `"rb"`.
- Ronda 2 (solo la corrección): sin bloqueantes ni «should-fix». Nit aplicado: una trama descartada tras un `FF`
  también borra la última tecla (`test_a_cold_reset_forgets_the_last_key_too`). Sin ronda 3.
- Tras las correcciones: tools 106 OK, `test/host` 49/49 (`test_host_menu` 31), `retrobridge_host` 15/15,
  demo `run3` con el ejecutable definitivo.

## Corrección pedida en la revisión humana (2026-11-18)

- Pedido: «el mensaje de error, quede debajo de las opciones». La línea de mensajes pasa debajo del pie de teclas
  en la pantalla principal (`MAIN_MESSAGE = MAIN_FOOTER + 80`) y, por coherencia, en la lista
  (`LIST_MESSAGE = LIST_FOOTER + 40`). Cada display list gana una línea con LMS propia para el mensaje; la lista
  queda con 20 entradas contiguas (LMS + 19). El generador exporta `kMainFooter` y `kListFooter`;
  `menu_image.cpp` comprueba con `static_assert` que el mensaje va justo debajo del pie y cabe en la pantalla.
- Evidencia: tools 106 OK (display lists byte a byte con la línea nueva); `test/host` 49/49; `retrobridge_host`
  15/15; `make_menu.py --check` al día. Demo `run4` (`D:\rb-e0\m40\run4`, carpeta local, no versionada; 2026-11-18, 02:22 a 02:28 UTC), mismo
  recorrido que `run3`: `10-error-rota.png` muestra «ERROR: ATR INVALIDA» debajo del pie; `03-lista.png` muestra
  «PULSA LA LETRA DE LA IMAGEN» debajo del pie de la lista. `menu_stats`: 16 comandos, 0 reintentos, 0 NAK, 0
  descartadas. sha256: `menu.jsonl` 9a0bf82f..., `hub.log` 539c10cd..., `10-error-rota.png` 2f7a2d3b...,
  `03-lista.png` 83bcfcc8..., `01-principal.png` f55495ad....
- No ejercido en `run4`: la pantalla «host no responde» (la cubre `test_no_answer_shows_the_no_host_line...` en el
  intérprete, que escribe en `MAIN_MESSAGE`).

## Entrega

- 2026-11-18: commit `0473338` en `feat/40-menu-principal`, push y PR #41 (cierra #40), con aprobación del usuario.
  `rsc risk`: tier 1.
- Rama local igual a la publicada: mismo SHA `0473338`, `git diff` vacío, nada adelante ni atrás.
- [Comentario en el PR #41](https://github.com/calaluis/retrobridge-chile/pull/41#issuecomment-6967350204) con `10-error-rota.png`, `03-lista.png` y el registro del recorrido de
  `run4`: es la copia pública de esa evidencia.
- CI de `0473338`: `pull_request` 5 de 5 en verde (pruebas del host con ASan/UBSan, capas y avisos, ESP-IDF esp32,
  dos paquetes de firmware); `push` 3 en verde y los 2 paquetes de firmware omitidos en esa corrida.
- Pendiente: revisión y merge del responsable. Worklog: [2026-11-18-menu-principal.md](../../../raw/worklog/2026-11-18-menu-principal.md).
  Decisiones: [decisions.md](../decisions.md).

## Decision queue

- [review-after] Q1 — Fuente 1 rotulada `SD` también en el host, aunque sea una carpeta, `specs/menu-principal.md` (solo local), T3
- [review-after] Q2 — Mensajes en pantalla redactados por el agente (`PENDIENTE: ...`, `UNIDAD 1-8 (ESC VUELVE)`), `host/src/menu_controller.cpp`, T3
- [review-after] Q3 — Esc al elegir unidad vuelve a la lista, no a la pantalla principal, `host/src/menu_controller.cpp`, T3
- [review-after] Q4 — Pantalla de la lista (título en modo 7, 20 entradas, pie de una fila) diseñada por el agente: la maqueta solo define la principal, `tools/make_menu.py`, T1
