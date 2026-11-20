---
type: spec
title: Spec — Presentación del menú: carga sin barrido y teclas a la vista
description: QUÉ y POR QUÉ de que el menú del Atari no muestre el barrido al cambiar de pantalla y de que el pie muestre todas las teclas de la pantalla principal.
tags: [sdd, spec, menu, 6502, presentacion]
timestamp: 2026-11-20T00:00:00Z
topic: sdd
slug: menu-presentacion
status: approved
---

# Spec — Presentación del menú: carga sin barrido y teclas a la vista

## Problem & why

Dos defectos de la capa de presentación del menú (el programa 6502 que dibuja las pantallas):

1. **Barrido.** Al pasar de una pantalla a otra, la pantalla nueva se dibuja de arriba hacia abajo mientras llega.
   Durante ese tiempo se ve una mezcla de la pantalla vieja y la nueva. Se ve feo y parece un fallo.
2. **Teclas ocultas.** Las funciones nuevas no figuran en pantalla. La tecla `N` (crear una ATR) no aparece en el
   pie de la pantalla principal. El usuario tiene que leer el README para saber que existe.

Disparador: el responsable lo pidió el 2026-11-20, tras la demo de `menu-formatear-disco`.

## Cost of not building it

- Cada cambio de pantalla muestra el barrido. El usuario lo ve decenas de veces por sesión.
- `N` sigue invisible. Quien no leyó el README no crea discos desde el Atari.
- Cada tecla nueva futura agrava el problema del pie.
- El costo es de imagen y de uso, no de datos: nada se pierde ni se corrompe.

## The cheapest alternative

- **Solo el pie.** Cambiar el texto del pie y dejar el barrido. Es un cambio pequeño en el programa 6502. No resuelve
  el punto 1, que el responsable pidió de forma expresa.
- **Documentar en el README.** Ya está hecho para `N`. No resuelve nada en pantalla.
- **No construirlo.** El menú funciona igual. El barrido y las teclas ocultas quedan.

Ninguna cubre los dos puntos, así que se construye la versión completa.

## Objeción registrada

El cambio toca el programa 6502, que dibuja **todas** las pantallas. Un error ahí rompe el menú entero, no una sola
función. Además, el programa se genera (`tools/make_menu.py`) y las pruebas comparan el resultado; cada cambio
obliga a regenerar y revisar. El pie de la pantalla principal ya está lleno: agregar `N` exige rediseñarlo y
acortar los textos. El responsable decidió seguir (2026-11-20): el barrido es visible en cada uso.

## Goals

- G1. Al cambiar de pantalla, el usuario nunca ve una pantalla a medio dibujar.
- G2. Mientras llega la pantalla nueva, el usuario ve un aviso ` CARGANDO...`.
- G3. El pie de la pantalla principal muestra todas sus teclas: `1-8`, `E`, `N`, `A`, `U`.
- G4. El comportamiento del menú (qué hace cada tecla) no cambia.

## Non-goals / out of scope

- Firmware del ESP32, hardware real, flasheo. Se prueba con Altirra y el host.
- Una pantalla de ayuda aparte.
- Cambiar el pie de la pantalla de lista (`A-T`, `RET`, `<>`, `ESC`): ya muestra sus teclas.
- Cambiar la lógica del host (qué hace cada tecla, eventos, registro). Solo cambia lo que se dibuja.
- Animaciones, colores, sonidos, barra de progreso nueva.
- Teclas futuras (proteger, doble densidad): entran en sus propias specs.

## Users & context

El usuario del Atari (800XL o Altirra) frente al menú de RetroBridge, servido por `retrobridge_host` en D1. Navega
entre la pantalla principal, la lista de imágenes y los pasos de editar o crear. No lee la documentación del PC.

## What the user can do

- Pulsar cualquier tecla del menú, como hoy. Nada nuevo que aprender.
- Ver en el pie de la pantalla principal qué teclas existen y qué hacen.

Condición: el menú arrancó en D1. Cualquier usuario del Atari puede hacerlo; no hay permisos.

## What result they get

1. **Carga.** Tras una tecla que cambia la pantalla:
   - la pantalla actual queda quieta;
   - la fila de mensajes muestra ` CARGANDO...`;
   - cuando llega la pantalla nueva entera, aparece de una vez, sin barrido.
2. **Pie principal.** Referencia aprobada el 2026-11-20 (2 filas de 40 columnas; la tecla va en inverso):

   ```text
    1-8  FUENTE   E  SERVIDOR   N  NUEVA ATR
    A  ARRANCAR D1   U  DESMONTAR UNIDAD
   ```

   Los espacios pueden ajustarse en plan para que todo quepa; los textos no cambian.

## What it must respect

- ADR-017 (menú de arranque) y sus enmiendas: el host arma las pantallas de 960 bytes; el 6502 las dibuja y manda
  teclas (terminal delgado). Esta spec no lo cambia.
- El mismo programa 6502 lo sirve hoy el host y después el ESP32. No debe depender de nada propio del PC.
- Acción 2 (espera): el host ya muestra ` CARGANDO nn%` en los trabajos TNFS. Debe seguir viéndose.
- Acción 3 (editar un campo): el editor de servidores y el nombre de `N` deben seguir funcionando.
- El chequeo del 6502 de «respuesta cortada o mezclada» debe seguir funcionando.
- Arranque con `A`: la pantalla pasa al programa arrancado, como hoy.
- ADR-012: sin código de FujiNet ni de emuladores.
- Las pruebas actuales (`ctest`, pruebas de `tools/`) siguen en verde.

## How it proves it

- Pruebas de `tools/` sobre el programa generado: el pie contiene las 5 teclas; el programa cabe en su espacio.
- Prueba del host: el programa servido es el generado.
- Demo en Altirra con un solo host y las unidades del host en Off: cambiar de pantalla (principal → lista →
  principal, editar servidor, `N`), capturas de pantalla durante la carga y del pie. Evidencia guardada en una
  carpeta `D:\rb-e0\m47-...`.
- Registro de verificación en `02-DOCS/wiki/sdd/verifications/`.

## Behaviour

- **Petición normal.** Al mandar una tecla al host, el 6502 escribe ` CARGANDO...` en la fila de mensajes de la
  pantalla que se ve: la de la principal o la de la lista, cada una en su lugar. Recibe la respuesta fuera de la vista. Si llega entera y válida, la muestra de una vez.
- **Respuesta cortada o mezclada.** Como hoy: el 6502 la descarta y reintenta. La pantalla vieja queda visible con
  el aviso; nunca se muestra la respuesta mala.
- **Espera (acción 2).** La pantalla que manda el host (con ` CARGANDO nn%`) se muestra entera, como cualquier otra.
- **Edición (acción 3).** El campo se edita sobre la pantalla ya mostrada, como hoy.
- **Arranque (A).** Igual que hoy.
- **Pie.** La pantalla principal muestra el pie nuevo. La de lista, el de hoy.

## Acceptance criteria

- **AC1** — Dado el menú en la pantalla principal, cuando el usuario abre una fuente (`1`), entonces durante la
  carga se ve la pantalla principal con ` CARGANDO...` en la fila de mensajes, y después la lista entera; ninguna
  captura durante la carga muestra filas de las dos pantallas mezcladas.
- **AC2** — Dado el menú en la lista, cuando el usuario pulsa Esc, entonces vuelve a la principal sin barrido (como
  AC1).
- **AC3** — Dado el menú en la pantalla principal, cuando se muestra, entonces el pie lista `1-8 FUENTE`,
  `E SERVIDOR`, `N NUEVA ATR`, `A ARRANCAR D1` y `U DESMONTAR UNIDAD`, en 2 filas de 40 columnas.
- **AC9** — Dado el menú en la lista, cuando el usuario pulsa una tecla que va al host, entonces ` CARGANDO...` se
  ve en la fila de mensajes de la lista, no en otra fila.
- **AC4** — Dada una fuente TNFS que tarda, cuando el host responde con la acción 2, entonces se ve
  ` CARGANDO nn%` como hoy.
- **AC5** — Dado el editor de servidores o el nombre de `N`, cuando el usuario escribe, entonces el campo se edita
  como hoy.
- **AC6** — Dada una respuesta cortada, cuando el 6502 la recibe, entonces no la muestra y reintenta.
- **AC7** — Dado `A` en la principal, cuando arranca D1, entonces el programa arranca como hoy.
- **AC8** — Las pruebas de `ctest` y de `tools/` pasan; el pie del programa generado contiene las 5 teclas.

## Enfoques considerados

1. **Pantalla quieta + aviso** (elegido). Recibir fuera de la vista y mostrar de una vez. Cuesta cerca de 1 KB más de
   memoria en el Atari. Resuelve el barrido del todo.
2. **Pantalla en blanco + CARGANDO.** Borrar y mostrar solo el aviso mientras llega. Menos memoria, pero la pantalla
   parpadea en cada tecla.
3. **Solo ocultar el barrido** (apagar la imagen durante la carga). Lo más barato, pero la pantalla queda negra un
   momento y no dice qué pasa.

## Points to clarify

- **suposición tomada** — los requisitos se numeran M47-n. *Base:* la última spec usó M46. *Riesgo:* ninguno.
- **suposición tomada** — el Atari tiene memoria libre para una segunda copia de la respuesta (cerca de 1 KB).
  *Base:* el responsable eligió el enfoque 1 sabiendo ese costo. *Riesgo:* si no cabe, plan lo dice y se vuelve a
  clarify.
- **suposición tomada** — el aviso ` CARGANDO...` se muestra en toda petición que manda una tecla, aunque la
  respuesta sea la misma pantalla. *Base:* el 6502 no sabe de antemano si la pantalla cambia. *Riesgo:* en
  respuestas rápidas el aviso parpadea un instante.
- **decisión diferida** — teclas futuras (proteger, doble densidad) en el pie: sus specs.

## Clarifications

- 2026-11-20 — ¿Qué se ve durante la carga? → pantalla actual quieta + ` CARGANDO...` en la fila de mensajes; la
  nueva aparece entera → evita el barrido sin parpadeo; cuesta ~1 KB.
- 2026-11-20 — ¿Cómo se documentan las teclas? → pie principal rediseñado con todas las teclas, textos más cortos;
  el pie de lista no cambia → el usuario ve todo sin otra pantalla.
- 2026-11-20 — ¿Qué se toca? → solo el programa 6502 del menú; se prueba en Altirra, sin hardware ni flasheo → es el
  mismo programa que después servirá el ESP32.
- 2026-11-20 — ¿Dónde se trabaja? → rama nueva `docs/spec-menu-presentacion` desde main, PR solo con la spec.

- 2026-11-20 (clarify) — ¿Texto del pie? → `1-8 FUENTE`, `E SERVIDOR`, `N NUEVA ATR` / `A ARRANCAR D1`,
  `U DESMONTAR UNIDAD` → nombra lo que hace cada tecla y cabe en 2 filas.
- 2026-11-20 (clarify) — ¿Dónde va el aviso en la lista? → en la fila de mensajes de cada pantalla → el usuario lo
  busca donde ya ve los otros mensajes (AC9).

## Revisions

- 2026-11-20 — borrador (specify).
- 2026-11-20 — clarify: 2 preguntas resueltas, AC3 precisado, AC9 nuevo.
- 2026-11-20 — aprobada por el responsable.
