---
type: plan
title: Plan — Presentación del menú: carga sin barrido y teclas a la vista
description: CÓMO se construye la spec menu-presentacion: búfer de recepción oculto, aviso CARGANDO y pie principal nuevo en el programa 6502.
tags: [sdd, plan, menu, 6502, presentacion]
timestamp: 2026-11-20T06:00:00Z
topic: sdd
slug: menu-presentacion
spec: 02-DOCS/wiki/sdd/specs/menu-presentacion.md
branch: feat/menu-presentacion
status: approved
---

# Plan — Presentación del menú: carga sin barrido y teclas a la vista

## 0. Global constraints (literales, para cada tarea)

- Código propio (ADR-012). Fuente: *Altirra Hardware Reference Manual* (ya en `docs/fuentes.md`), capítulos de
  ANTIC (lista de visualización, `DLISTL`) y del SO (`SDLSTL`, `RTCLOK`). Sin leer código de FujiNet ni de
  emuladores.
- El programa 6502 solo cambia en `tools/make_menu.py`; `host/src/menu_program.inc` se regenera con
  `python tools/make_menu.py --out host/src/menu_program.inc`. Nunca se edita a mano.
- El protocolo con el host no cambia (ADR-017): mismo dispositivo `$5A`, órdenes `K` y `T`, respuesta de 962 bytes,
  acciones 0..3, AUX1 y AUX2 iguales.
- La lógica del host no cambia. Solo cambia lo que trae `kMenuMainTemplate` (el pie) y `kMenuBlock`.
- Un solo `retrobridge_host` por hub; en Altirra, D1..D8 en Off.

## 1. Context & constraints

- Spec: §What result they get, §Behaviour, 9 criterios de aceptación, 4 puntos tipados.
- Hoy (main `e73b3e7`), `tools/make_menu.py`:
  - el programa carga en `$0700`; el bloque ocupa 16 sectores (2048 bytes) y termina en `$0ED6`;
  - `DBUFLO/DBUFHI` apuntan a `reply` (`$0B14`): acción, layout y la pantalla de 960 bytes que muestran las dos
    listas de visualización. SIOV escribe ahí mismo: **esa es la causa del barrido**;
  - tras SIOV, `SDLSTL` (sombra) toma la lista del layout nuevo; el SO la copia a ANTIC en el siguiente borrado
    vertical;
  - si SIOV falla (tras sus propios reintentos), `nohost` borra todo bajo el título, escribe
    ` ERROR: EL HOST NO RESPONDE` en la fila de mensajes de la principal y espera una tecla;
  - el cursor local (`cur_*`) y el campo de edición (`edit_*`) se leen de la pantalla visible, en direcciones fijas.
- Pruebas: `tools/tests/test_menu_program.py` corre el programa en un intérprete 6502 (`Menu6502`); SIOV es una
  función de Python que hace de host. 47 pruebas.
- **Precisión de la spec (AC6).** Hoy no hay un «reintento» propio del 6502: SIOV reintenta solo y, si falla, el
  6502 muestra el error. AC6 se lee así: una respuesta fallida nunca se muestra; la pantalla vieja queda y sale el
  error de hoy. Se corrige la redacción de AC6 en la spec, en esta misma rama.

## 2. Architecture

Tres cambios, todos dentro del programa 6502:

1. **Búfer oculto.** `DBUFLO/DBUFHI` apuntan a `hidden`, un búfer de 962 bytes fuera del bloque que se carga del
   disco (RAM sin inicializar justo después del bloque, alineada a página). El bloque no crece por él.
2. **Aviso.** En `send`, antes de SIOV, si el código **no** es `POLL`, el 6502 copia ` CARGANDO...` (40 columnas) en
   la fila de mensajes del layout visible: `main_message` si `layout` = 0, `list_message` si es 1.
   - `POLL` no escribe el aviso: la pantalla de la acción 2 ya trae ` CARGANDO nn%` del host (AC4).
   - La ruta de edición (`ed_save` → órdenes `T` → `send` con `EDIT_SAVE`) también lo muestra: el campo ya se mandó.
3. **Mostrar de una vez.** Si SIOV termina bien:
   1. esperar a que cambie `RTCLOK` (comienza el borrado vertical);
   2. escribir la lista de visualización del layout nuevo en `SDLSTL` y en `DLISTL` (`$D402`) de ANTIC, para que
      el cuadro siguiente ya use la lista correcta;
   3. copiar `hidden` → `reply` de arriba hacia abajo (962 bytes). La copia va por delante del haz: tarda unos
      6 ms y la primera fila visible se dibuja después del borrado vertical y de 8 líneas en blanco;
   4. seguir como hoy con `action`.
   Si SIOV falla: no se copia nada; `nohost` sigue igual.

El pie principal cambia en `main_footer()`:

```text
 [1-8] FUENTE   [E] SERVIDOR   [N] NUEVA ATR
 [A] ARRANCAR D1   [U] DESMONTAR UNIDAD
```

Cada fila mide 40 columnas; los espacios se reparten para que quepan (la spec permite ajustarlos).

## 3. Interfaces & contracts

- Protocolo host ↔ 6502: sin cambios (ADR-017).
- `kMenuMainTemplate`: mismo tamaño y mismas posiciones; solo cambian las 2 filas del pie (`MAIN_FOOTER`).
- `kMenuLoadSectors`: puede pasar de 16 a 17 (el código crece unos 100 bytes). `menu_image.cpp` ya lo toma del
  include (`static_assert` de sectores enteros).
- Nuevas comprobaciones del generador (fallan la generación si no se cumplen):
  - `hidden` empieza en un límite de página, justo después del bloque;
  - `hidden + 962` queda por debajo de `$4000` (cabe incluso en un Atari de 16 KB, lejos de la pantalla del SO).

## 4. Data model & flow

```text
tecla → [aviso en fila de mensajes visible] → SIOV → hidden
      ├─ falla  → nohost (como hoy)
      └─ bien   → esperar RTCLOK → lista nueva (SDLSTL + DLISTL) → copiar hidden→reply → acción
```

## 5. Testing strategy

TDD en `tools/tests/test_menu_program.py`, con el intérprete `Menu6502`. El SIOV simulado puede mirar la memoria
visible mientras «recibe». Casos nuevos:

- `DBUFLO/DBUFHI` apuntan a `hidden`, no a `reply`.
- Durante SIOV, la pantalla visible es la anterior, salvo la fila de mensajes, que dice ` CARGANDO...` (principal y
  lista, cada una en su fila) (AC1, AC2, AC9).
- Tras SIOV correcto, la pantalla visible es la respuesta entera y la lista de visualización es la del layout, en
  `SDLSTL` y en `DLISTL` (AC1, AC2).
- La copia empieza después de un cambio de `RTCLOK`.
- Con `POLL` no se escribe el aviso (AC4).
- SIOV falla: la respuesta no llega a la pantalla; sale el error de hoy y el cursor local se apaga (AC6).
- Pie principal: el texto nuevo en 2 filas de 40 (AC3); la prueba actual del pie se actualiza.
- Las pruebas de hoy (teclas, espera, cursor local, edición, arranque) siguen en verde sin cambiar lo que comprueban
  (AC5, AC7). Si alguna lee `reply` durante SIOV, se ajusta solo esa lectura.
- `ctest` del host en verde (imagen del menú con 16 o 17 sectores) (AC8).

El intérprete no dibuja la pantalla: el barrido real solo se ve en Altirra (P004).

## 6. Sequencing & dependencies

P001 (pie) y P002 (búfer y aviso) tocan el mismo archivo y el mismo `.inc`: van en serie. P003 (host) después.
P004 (demo) y P005 (docs) al final.

## 7. Risks & open decisions

- **Un cuadro con lista vieja.** Si la lista de visualización cambia un cuadro después de la copia, se ve un cuadro
  con la pantalla nueva en la lista vieja. Se mitiga escribiendo `DLISTL` en el borrado vertical. Se valida en
  Altirra avanzando cuadro a cuadro.
- **Copia más lenta que el haz.** Si el 6502 pierde ciclos (DMA de ANTIC), la copia podría alcanzar al haz en la
  parte baja. Efecto máximo: un cuadro con media pantalla vieja. Se valida en la demo. Si se ve, la copia se parte
  en dos cuadros (decisión de implement, sin cambiar la spec).
- **Memoria.** `hidden` en `$0F00`–`$1300` aprox. Sin DOS no hay nada ahí; el SO no usa esa zona.
- **Aviso en respuestas rápidas.** Se ve un instante (suposición tomada de la spec).

## Tasks

Rama `feat/menu-presentacion` (desde main `e73b3e7`). Riesgo previsto por tarea (0/1/2); `rsc risk` decide el real.

| # | [P] | Tarea | Done-check | Depende de | Cubre | Riesgo |
|---|---|---|---|---|---|---|
| P001 |  | Pie principal nuevo en `main_footer()`; regenerar `.inc` | `python -I -m unittest discover -s tools/tests`: prueba del pie con el texto nuevo, 2 filas de 40; `--check` del `.inc` en verde | — | AC3, AC8 | 1 |
| P002 |  | Búfer `hidden`, aviso ` CARGANDO...` (salvo `POLL`), espera de `RTCLOK`, lista en `SDLSTL` + `DLISTL`, copia; comprobaciones del generador; regenerar `.inc` | casos de §5 en verde; las 47 pruebas de hoy en verde; `--check` en verde | P001 | AC1, AC2, AC4–AC7, AC9 | 2 |
| P003 |  | Host: compilar y `ctest` con el `.inc` nuevo | `ctest --test-dir build/host`: todo en verde; `retrobridge_host.exe` compila | P002 | AC8 | 0 |
| P004 |  | Demo en Altirra: principal → lista → principal, `E`, `N`, una fuente TNFS lenta, `A`; capturas durante la carga y del pie | carpeta `D:\rb-e0\m47-presentacion`: capturas (una durante cada carga), `host.jsonl`, `version.txt`; un solo host en el hub | P003 | AC1–AC7, AC9 | 0 |
| P005 | [P] | Docs: ADR-017 enmienda 10 (búfer oculto, aviso, pie), `host/README.md` (pie), `docs/fuentes.md` (consulta del manual de Altirra), corrección de AC6 en la spec | archivos existen y enlazan; sin emoji; `python -I tools/check_notices.py` en verde | P002 | R: ADR-017 | 0 |

- Aprobado por el responsable («Sí, rama nueva») el 2026-11-20.
