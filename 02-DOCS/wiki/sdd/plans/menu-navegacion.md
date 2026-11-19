---
type: plan
title: Plan — Cursor y páginas en el menú del Atari
description: CÓMO se construye la spec menu-navegacion: códigos de navegación del 6502, cursor y páginas en el host, listas de hasta 999 entradas.
tags: [sdd, plan, host, menu, ux]
timestamp: 2026-11-19T13:00:00Z
topic: sdd
slug: menu-navegacion
spec: 02-DOCS/wiki/sdd/specs/menu-navegacion.md
status: approved
---

# Plan — Cursor y páginas en el menú del Atari

## 0. Global constraints (literales, para cada tarea)

- C++17 con `-fno-exceptions -fno-rtti -Werror` (ADR-004); reservar solo con `new (std::nothrow)`.
- `MenuController::key()` no reserva memoria y contesta en 250 ms como máximo (ADR-011, M41-1).
- Respuesta del menú: 962 bytes, acción 0/1/2, diseño 0/1, 960 bytes de pantalla (ADR-017).
- AUX1: `$00`-`$3F` tecla real tras `AND #$3F`; `$FE` sondeo; `$FF` redibujo. Los códigos nuevos van en
  `$40`-`$44` (abajo) y nunca coinciden con esos.
- Página: 20 filas. Carpeta o lista navegable: hasta 999 entradas filtradas.
- Joystick: puerto 1 (`STICK0` `$0278`, `STRIG0` `$0284`). Repetición: primera a los 30 cuadros, luego cada 9.
- Código propio (ADR-012); sin FujiNet ni emuladores. Las pruebas de #40 y `menu-tnfs` que la spec no cambia
  siguen iguales.

## 1. Context & constraints

- Spec §What the user can do, §What result they get (boceto aprobado), §Behaviour, §Acceptance (13 criterios).
- Hoy (main `68c9d9f`): el 6502 manda `CH AND #$3F` (M41-3), así que Control + `=` llega como `=`; nadie lee el
  joystick; `MenuCatalog` y `TnfsFolder` guardan 20 entradas en arreglos fijos; `View::Pick`, `Unmount` y
  `FolderPick` piden la unidad con «UNIDAD 1-8» en la línea de mensaje.
- M40-2 (AUX2 por tecla) cubre también los movimientos: cada uno es una tecla más.

## 2. Architecture

```text
 Atari (6502, make_menu.py)                 Host (MenuController)
 CH / STICK0 / STRIG0 --> código AUX1 ---->  key(code) --> Cursor (fila, página por vista)
   flecha  -> $40-$43                          |           Pila de posiciones (volver con Esc)
   joystick-> $40-$43, fuego -> $44            |--> Listas: MenuCatalog (local), TnfsFolder (TNFS)
   RETURN  -> $0C (tecla real)                 |--> render: fila del cursor en inverso, «PAGINA n DE m (t)»
   resto   -> CH AND #$3F (igual que hoy)      '--> MenuReply 962 bytes (sin cambio de formato)
```

- **Decisión principal: el host dibuja el cursor** (enfoque 1 de la spec). El 6502 solo traduce entradas a
  códigos. Defensa: ADR-017 (un solo dueño de la pantalla) y las pruebas del host ya cubren cada vista. Costo:
  una transacción SIO por movimiento (decenas de ms en NetSIO), dentro de los 250 ms de `key()`.
- **6502:** traduce antes de enmascarar. Si `CH` tiene Control (bit 7) y su código bajo es `-` `=` `+` `*`,
  manda `$40` arriba, `$41` abajo, `$42` izquierda, `$43` derecha; si no, `CH AND #$3F` como hoy. Además,
  mientras espera tecla (acción 0) y durante la espera de la acción 2, mira `STICK0` y `STRIG0`: una dirección
  nueva o repetida manda `$40`-`$43`; el fuego, al pulsarse, manda `$44`. Enmienda M41-3.
- **Host:** `RETURN` (`$0C`) y `$44` son «confirmar». Confirmar aplica la tecla de la fila marcada (letra o
  dígito), así que reutiliza el camino actual en lugar de duplicarlo.

## 3. Interfaces & contracts

```text
kNavUp=$40 kNavDown=$41 kNavLeft=$42 kNavRight=$43 kNavFire=$44   (menu_program.inc, generados)
MenuController::key(code) -> MenuReply            // contrato igual; códigos nuevos
  - $40/$41: mueve una fila; en el borde de página pasa a la siguiente/anterior (spec §Result)
  - $42/$43: cambia de página en listas; cursor a la fila 1; en el extremo no cambia nada
  - $0C/$44: equivale a la letra o dígito de la fila marcada; sin fila marcada, nada
  - letra A-T: entrada (página*20 + índice) de la página visible
Cursor { uint8_t row; uint16_t page; }            // por vista; sin reservas
PositionStack: hasta 32 niveles de {page,row}     // entrar empuja, Esc saca; más hondo: fila 1
TnfsFolder::capacity = 999; entry(i) para i < shown(); total() sin cambio
MenuCatalog::kMaxEntries = 999
pageLine(total, page) -> "PAGINA n DE m (t)" o "PAGINA n DE m (999 DE t)"; vacío si total <= 20
```

## 4. Data model & flow

- `TnfsFolder`: hasta 999 entradas; cada una con su nombre para mostrar (38) y su nombre en el servidor
  (hasta 255). La ruta se arma al elegir, no se guarda por entrada. El espacio se reserva una vez al crear el
  controlador y el cargador (fuera de la ruta SIO); `poll()` intercambia los contenidos sin reservar.
- `MenuCatalog`: los nombres locales pasan a 999 (arreglo fijo, unos 40 KB).
- Flujo de una tecla: AUX1 -> `key()` -> vista actual -> cursor/página -> render 960 bytes -> respuesta. Los
  movimientos no tocan la red; abrir carpeta sigue por `TnfsLoader` (M41-1).
- Elegir unidad (`Pick`, `FolderPick`, `Unmount`): se dibuja la pantalla principal con el cursor sobre D1-D8 y
  la línea «UNIDAD PARA: <nombre>»; los dígitos siguen valiendo.

## 5. Testing strategy

| Criterio de la spec | Nivel | Qué afirma | Qué se finge |
| --- | --- | --- | --- |
| Joystick abre la fuente 3 | 6502 + host | `Menu6502` con `STICK0`=13 dos veces y `STRIG0`=0 manda `$41 $41 $44`; el host abre la fuente 3 | memoria de `STICK0`/`STRIG0` en `Menu6502` |
| 205 entradas, página 1 | host | 20 filas, fila 1 en inverso, «PAGINA 1 DE 11 (205)» | `FakeTnfsServer` con 205 `.atr` |
| Derecha 10 veces, extremo | host | página 11 con 5 filas; otra derecha no cambia el byte de pantalla | igual |
| Bajar desde la fila 20 | host | página 2, cursor fila 1 | igual |
| `C` en la página 3 | host | elige la entrada 43 | igual |
| RETURN + D2 con cursor | host | mismo evento `menu_mount` que con letra y `2` | servidor falso y buzón |
| Esc vuelve a página 2 fila 7 | host | posición restaurada | igual |
| Control + `=` frente a `=` | 6502 | `$8F` en CH manda `$41`; `$0F` manda `$0F` | `CH` |
| 250 ms y sin reservas | host | cada `key()` medido y con el contador de `operator new` | como en `test_menu_tnfs` |
| Lista local paginada | host | 25 `.atr` locales: «PAGINA 1 DE 2 (25)» | carpeta de prueba |
| «CARGANDO» y carpeta vacía | host | movimientos y RETURN no cambian nada; Esc cancela | `GatedPort` |
| Pruebas previas | suite | `test_host_menu`, `test_menu_tnfs` en verde con los textos de página cambiados a propósito | — |
| Repetición del joystick | 6502 | dirección sostenida: un código, otro a los 30 cuadros, luego cada 9 | `RTCLOK` en `Menu6502` |

Demo en Altirra contra `pulgarcito` (`OMS_ATR`, página 11, solo joystick), evidencia en `D:\rb-e0\m42`.

## 6. Sequencing & dependencies

1. Códigos de navegación en el 6502 (flechas, joystick, fuego, repetición) y `menu_program.inc` regenerado —
   paralelo.
2. Pie de teclas nuevo en las plantillas del generador — junto con 1 (mismo archivo).
3. `TnfsFolder` y `MenuCatalog` con 999 entradas, sin reservas en `poll()` — paralelo con 1.
4. Cursor, páginas y pila de posiciones en `MenuController` (listas) — depende de 1 y 3.
5. Cursor en la principal y en la elección de unidad — depende de 4.
6. Docs: ADR-017 enmienda 3 (códigos `$40`-`$44`, M41-3 enmendada), README del host, decisiones M42 — tras 5.
7. Demo en Altirra — al final.

## 7. Risks & open decisions

| # | Riesgo | Disparador | Impacto | Mitigación |
| --- | --- | --- | --- | --- |
| 1 | El cursor va detrás del joystick con NetSIO lento | Repetición cada 9 cuadros y respuesta lenta | Saltos de fila de más | El 6502 no manda otro código hasta tener la respuesta (ya es así: `SIOV` es síncrono) |
| 2 | El programa 6502 crece y pasa de 11 sectores o mueve la pantalla | Código del joystick | Cambian direcciones de ADR-017 | El generador ya comprueba 4 KB y la página de las display lists; la enmienda 3 anota el nuevo tamaño |
| 3 | 999 entradas TNFS con nombres de 255 bytes | Carpeta enorme | Unos 300 KB por copia, dos copias | Reserva única fuera de SIO; falta de memoria al crear: el host no arranca, con causa en el registro |
| 4 | Cambiar el texto de página rompe pruebas de `menu-tnfs` | AC9 y M41-21 | Pruebas rojas | Cambio a propósito, en la misma tarea, anotado en M42 |
| 5 | `STICK0` lo actualiza el VBI del SO | El menú corre con el SO activo | Ninguno esperado | La demo en Altirra lo confirma; si falla, leer `PORTA` (`$D300`) |

- Decisión abierta: ninguna. Las suposiciones de la spec quedan como valores en §0.
- Decisiones a anotar en `decisions.md` al implementar: M42-1 (códigos `$40`-`$44`), M42-2 (host dibuja el
  cursor), M42-3 (999 entradas y pila de 32 niveles), M42-4 (elegir unidad sobre la pantalla principal).

## 8. Precisiones del responsable antes de aprobar (2026-11-19)

Se mantienen el cursor en el host y las páginas de 20. Cada punto se comprobó en main `68c9d9f`.

1. **Índice global separado de la fila de pantalla.** Hoy `chosen_` (`menu_controller.hpp:165`),
   `Shown::entry` (`:119`), `assignD1` y `mountOn` (`:137-138`) usan `int8_t`: no llegan a 999. Pasan a un tipo
   propio `EntryIndex` (`uint16_t`, `kNoEntry = 0xFFFF`) en todo el camino: selección, `Pick`, `FolderPick`,
   montaje, asignación de D1, `previous_`/`incoming_` y arranque. La fila de pantalla (0-19) y la página son otro
   dato del cursor. Pruebas: paso de página de la entrada 20 a la 21 (índices 19 a 20), entradas visibles 128 a 129
   (índices globales 127 a 128, el borde de `int8_t`; la numeración visible empieza en 1) y selección y montaje de la entrada 999 (índice 998).
2. **Códigos `$40`-`$44` antes de la máscara.** Hoy `key()` hace `code & kMenuKeyMask` (`menu_controller.cpp:244`)
   después de comparar solo `$FE` y `$FF`: `$40`-`$43` se volverían `$00`-`$03` y `$44` sería `$04`. El host
   compara `$40`-`$44` junto con `$FE` y `$FF`, antes de la máscara. Repetición, a cargo del 6502: una dirección
   nueva manda su código al instante; sostenida, otro a los 30 cuadros y luego uno cada 9; al soltar, se reinicia.
   El botón rojo manda `$44` solo en la transición de suelto a pulsado: sostenido no repite. Pruebas en
   `Menu6502` y en el host; en Altirra se comprueba que mantener el botón rojo da una sola confirmación por
   pulsación (un solo `menu_select` en el registro).
3. **Paginación liviana dentro de SIO.** Hoy `TnfsFolder::swap` (`tnfs_folder.cpp:159`) recorre las entradas
   una a una bajo el cerrojo del cargador. Con 999 entradas, el resultado se entrega intercambiando el puntero al
   almacenamiento (dos bloques reservados una vez, fuera de SIO): coste constante, sin recorrer entradas ni
   reservar. El render dibuja solo las 20 filas visibles de la página. Prueba: tiempo y reservas de `poll()` con
   999 entradas iguales a los de 20 (0 reservas), y `Esc` desde una subcarpeta restaura página y fila.
4. **Montar por referencia no entra en esta entrega.** Es la spec B `tnfs-montaje-directo`, posterior y aparte
   (decisión del responsable del 2026-11-19). En esta entrega, una imagen TNFS se sigue trayendo entera a
   memoria (M41-2) y el cursor solo cambia cómo se elige.

## Tasks

Rama `feat/menu-navegacion` (worktree `.worktrees/feat-menu-navegacion`, desde main `68c9d9f`). Riesgo previsto
por tarea (0/1/2); `rsc risk` decide el real.

| # | [P] | Tarea | Done-check | Depende de | Cubre | Riesgo |
|---|---|---|---|---|---|---|
| N001 | [P] | Códigos de navegación en el 6502: flechas con Control, joystick del puerto 1 con repetición, fuego; pie de teclas nuevo; `menu_program.inc` regenerado | py `test_menu_program.py`: `CH`=$8F/$8E/$86/$87 mandan $41/$40/$42/$43; $0F manda $0F; `STICK0`=13 manda $41 una vez, otra a los 30 cuadros y luego cada 9; `STRIG0`=0 manda $44 una vez por pulsación; acción 2 también mira el joystick; `STRIG0`=0 sostenido 120 cuadros manda un solo $44; acciones 0 y 1 iguales; generador sin diferencias con `menu_program.inc`; tamaño y 4 KB comprobados | — | AC1, AC8, M42-1 | 1 |
| N002 | [P] | `TnfsFolder` y `MenuCatalog` con 999 entradas; reserva única; `poll()` sin reservas | ctest `test_tnfs_folder`, `test_tnfs_loader`: 205 y 1200 entradas (las 999 primeras y el total); `poll()` intercambia el almacenamiento: 0 reservas y sin recorrer entradas (mismo coste con 20 y 999); ctest `test_menu_catalog` con 25 `.atr` | — | AC2, AC10, M42-3 | 1 |
| N003 |  | Cursor, páginas, pila de posiciones y «PAGINA n DE m (t)» en las listas local y TNFS; RETURN y $44 confirman | `EntryIndex` de 16 bits en todo el camino; host reconoce $40-$44 antes de `& $3F`; ctest nuevo `test_menu_navegacion`: criterios AC2 a AC7, AC10, AC12 y AC13; paso de la entrada visible 20 a la 21, entradas visibles 128 a 129 (índices 127 a 128), selección y montaje de la 999 (índice 998); Esc restaura página y fila; cada `key()` ≤ 250 ms y 0 reservas; `test_menu_tnfs` y `test_host_menu` en verde con los textos de página cambiados a propósito | N001, N002 | AC2 a AC7, AC9 a AC13 | 1 |
| N004 |  | Cursor en la principal (8 fuentes) y en la elección de unidad sobre la pantalla principal | ctest `test_menu_navegacion`: AC1 por códigos, fuentes vacías, D2 por cursor = letra y `2`, desmontar con cursor; `test_host_menu` en verde | N003 | AC1, AC6, M42-4 | 1 |
| N005 | [P] | Docs: ADR-017 enmienda 3, README del host, M42-1 a M42-4 en `decisions.md` | archivos existen y enlazan; sin emoji; `python -I tools/check_notices.py` en verde | N004 | R: ADR-017 | 0 |
| N006 |  | Demo en Altirra contra `pulgarcito`: `OMS_ATR` hasta la página 11 y montar solo con joystick | capturas y `host.jsonl` en `D:\rb-e0\m42`; página 1 y 11; imagen de la página 11 montada en D2; un solo `menu_select` al mantener el botón rojo; ninguna escritura en el registro | N004 | AC1 a AC3, AC6 | 0 |
