---
type: spec
title: Spec — Cursor y páginas en el menú del Atari
description: QUÉ y POR QUÉ de elegir con un cursor (flechas o joystick, RETURN o botón rojo) y de recorrer por páginas las carpetas y listas de más de 20 entradas.
tags: [sdd, spec, host, menu, ux]
timestamp: 2026-11-19T04:00:00Z
topic: sdd
slug: menu-navegacion
status: approved
---

# Spec — Cursor y páginas en el menú del Atari

Origen: pedidos del responsable tras la demo de `menu-tnfs` (PR #44, fusionado). El montaje TNFS por referencia
es otra spec (`tnfs-montaje-directo`), decidido por el responsable el 2026-11-19.

## Problem & why

- En una carpeta con más de 20 entradas, el menú muestra solo las 20 primeras y «MOSTRANDO 20 DE n». Las demás no
  se pueden elegir. En `pulgarcito`, `OMS_ATR` tiene 205 imágenes: 185 quedan fuera de alcance.
- Elegir exige el teclado: letras `A`-`T` y dígitos `1`-`8`. Un Atari con solo joystick (por ejemplo un XEGS sin
  teclado) no puede usar el menú.

## Cost of not building it

- Toda imagen después de la vigésima en una carpeta grande es inaccesible desde el Atari. El único camino es
  reorganizar las carpetas del servidor, que no es nuestro (`pulgarcito` es de solo lectura).
- El menú sigue sin servir a quien juega con joystick.

## The cheapest alternative

Paginar con dos teclas y sin cursor (por ejemplo `<` y `>`). Resuelve el alcance de las carpetas grandes, pero no
sirve con joystick, y el responsable pidió el cursor de forma explícita. Por eso no basta.

## Goals

- Toda entrada de una carpeta o lista es alcanzable, hasta el límite de la sección «Qué debe respetar».
- Todo lo que hoy se elige con una tecla se puede elegir también con el cursor, desde el teclado o el joystick.

## Non-goals / out of scope

- El montaje TNFS por referencia, sin traer la imagen entera (spec `tnfs-montaje-directo`).
- `E` (editar servidor), que sigue en `PENDIENTE`.
- Buscar o filtrar por nombre, ordenar de otra forma, desplazar nombres largos, ratón, segundo joystick.
- Cambiar la sustitución de caracteres que el Atari no tiene (la `ñ` se ve como `?`).
- Sonidos, colores o animaciones del cursor.

## Users & context

El responsable y cualquier usuario del menú en Altirra o en un Atari real, con teclado o solo con joystick en el
puerto 1, frente a carpetas de servidores TNFS con cientos de imágenes.

## What the user can do

- Mover un cursor por las elecciones de cada pantalla con las flechas (Control + `-` `=` `+` `*`) o el joystick
  del puerto 1: arriba y abajo mueven una fila; izquierda y derecha cambian de página en las listas.
- Confirmar con RETURN o con el botón rojo. Confirmar equivale a pulsar la letra o el dígito de esa fila.
- Seguir eligiendo con `A`-`T`, `1`-`8`, `A`, `U` y Esc, como hoy. Las letras eligen dentro de la página visible.
- Pantallas con cursor: fuentes 1 a 8 en la principal; carpetas e imágenes en las listas (local y TNFS); la
  unidad D1 a D8 al montar o desmontar.

## What result they get

Pantalla de referencia, aprobada por el responsable con la spec (2026-11-19) (boceto, 40 columnas; `>` marca la fila en vídeo inverso):

```text
 FUENTE 2: ..OMS_ATR
 A 11.ATR
>B ABRA.ATR                     <- fila en vídeo inverso
 C ALLEY CAT.ATR
 ...
 T BOOT101.ATR
 A-T ELEGIR  RETURN OK  <> PAGINA  ESC VOLVER
 PAGINA 1 DE 11 (205)
```

- La fila del cursor se ve en vídeo inverso, de borde a borde de la lista.
- Al abrir una pantalla, el cursor está en la primera fila. Al volver con Esc a una lista, vuelve a la fila y
  página desde donde se entró.
- Bajar desde la última fila de una página muestra la página siguiente con el cursor en su primera fila; subir
  desde la primera fila muestra la anterior con el cursor en su última fila. En la primera y la última página,
  el cursor no pasa del borde.
- Con más de 20 entradas, debajo del pie aparece «PAGINA n DE m (total)» en lugar de «MOSTRANDO 20 DE n».

## What it must respect

- ADR-017 y sus enmiendas: terminal delgado, respuesta de 962 bytes, acción 2 y sondeo `$FE` (M41-1, M41-16).
- M41-3: hoy el 6502 borra Control con `AND #$3F`, así que una flecha llega igual que `-`. La spec exige que las
  flechas y el joystick se distingan de las teclas sin Control; cómo se logra es del plan, y M41-3 se enmienda.
- M40-2: un reintento de `SIOV` no aplica dos veces el mismo movimiento.
- ADR-011: la ruta SIO no reserva memoria; `key()` contesta en 250 ms como máximo (M41-1).
- M41-21: diseño de la carpeta TNFS (título, filas, «CARGANDO nn%», errores), salvo la línea de mensaje de páginas.
- Las teclas y los textos de #40 y `menu-tnfs` que esta spec no cambia siguen iguales, con sus pruebas.

## How it proves it

- Pruebas del programa 6502 en `Menu6502`: cada flecha y cada dirección del joystick salen como un código propio,
  distinto de `-` `=` `+` `*`; el botón rojo sale como RETURN; una dirección sostenida se repite.
- Pruebas del host: movimiento, borde, cambio de página, vuelta con Esc a la misma fila, RETURN igual a la letra,
  textos de página, cada pantalla con cursor; cada `key()` en 250 ms y sin reservar memoria.
- Demo en Altirra contra `pulgarcito`: recorrer `OMS_ATR` hasta la página 11 y montar una imagen de esa página
  solo con el joystick. Evidencia: capturas y registro en `D:\rb-e0\m42`.

## Behaviour

- Principal: el cursor recorre las fuentes 1 a 8. RETURN abre la fuente, como su dígito. `A` y `U` no cambian.
- Lista o carpeta: arriba y abajo mueven el cursor; izquierda y derecha cambian de página y dejan el cursor en
  la primera fila. RETURN sobre una carpeta entra; sobre una imagen pasa a elegir la unidad. Esc vuelve.
- Elegir unidad: el cursor recorre D1 a D8 con su contenido actual. RETURN monta (D2 a D8) o asigna (D1), igual
  que el dígito. Esc cancela.
- Error: una tecla o dirección sin efecto en esa pantalla no cambia nada.
- Principal: el cursor pasa por las 8 fuentes, también las vacías; RETURN sobre una vacía muestra
  `FUENTE n SIN CONFIGURAR`, igual que su dígito.
- Durante «CARGANDO nn%», los movimientos y RETURN no hacen nada; solo Esc cancela, como hoy.
- Carpeta vacía (`CARPETA VACIA`): no hay fila marcada; RETURN no hace nada; Esc vuelve.
- Una carpeta que cambia en el servidor se vuelve a listar al entrar; la página guardada se ajusta al nuevo total.

## Acceptance criteria

- Given la principal, When el usuario baja el joystick dos veces y pulsa el botón rojo, Then se abre la fuente 3,
  igual que con `3`.
- Given `/Atari_Chile/OMS_ATR` (205 entradas), When se abre, Then se ven 20 filas, el cursor en la primera y
  «PAGINA 1 DE 11 (205)» debajo del pie.
- Given la página 1, When el usuario mueve a la derecha diez veces, Then se ve la página 11 con 5 filas y
  «PAGINA 11 DE 11 (205)»; When mueve a la derecha otra vez, Then nada cambia.
- Given el cursor en la fila 20 de la página 1, When baja una vez, Then se ve la página 2 con el cursor en su
  primera fila.
- Given la página 3, When el usuario pulsa `C`, Then elige la tercera entrada de la página 3.
- Given una imagen marcada con el cursor, When pulsa RETURN y luego elige D2 con el cursor y RETURN, Then D2 queda
  montada como con la letra y `2`.
- Given una carpeta abierta desde la fila 7 de la página 2, When el usuario entra en ella y pulsa Esc, Then vuelve
  a la página 2 con el cursor en la fila 7.
- Given el teclado, When el usuario pulsa Control + `=` (flecha abajo), Then el cursor baja; When pulsa `=` solo,
  Then el cursor no se mueve.
- Given cualquier tecla o movimiento, When el host contesta, Then lo hace en 250 ms como máximo y sin reservar
  memoria.
- Given la fuente local con más de 20 `.atr`, When se abre, Then se pagina con las mismas reglas.
- Given «CARGANDO nn%» en pantalla, When el usuario mueve el cursor o pulsa RETURN, Then la carga sigue y nada
  cambia; When pulsa Esc, Then se cancela como hoy.
- Given una carpeta vacía, When el usuario pulsa RETURN, Then nada cambia y sigue `CARPETA VACIA`.
- Given las pruebas de #40 y `menu-tnfs` que esta spec no cambia, When corren, Then siguen en verde.

## Enfoques considerados

1. **El host dibuja el cursor (recomendado).** El 6502 envía cada movimiento como una tecla más; el host mueve el
   cursor y dibuja la pantalla entera, como hoy. Mantiene ADR-017. Cada movimiento cuesta una transacción SIO.
2. **El 6502 mueve el cursor en pantalla.** Respuesta inmediata, pero el 6502 tiene que saber qué filas son
   elegibles y la pantalla deja de tener un solo dueño. Rompe el terminal delgado.
3. **No construir el cursor; solo paginar con teclas.** Es «La alternativa más barata»: resuelve el alcance y no
   el joystick.

## Points to clarify

- **suposición tomada** — el joystick es el del puerto 1 y el botón rojo equivale a RETURN. *Base:* pedido del
  responsable. *Riesgo:* si se quiere el puerto 2, cambia una prueba.
- **suposición tomada** — una dirección sostenida se repite: primera repetición a los 0,5 s y luego cada 0,15 s.
  *Base:* ritmo habitual de repetición. *Riesgo:* con NetSIO lento, el cursor puede ir detrás del joystick.
- **suposición tomada** — una carpeta navegable tiene hasta 999 entradas filtradas; más allá se muestran las 999
  primeras y la línea dice «PAGINA n DE m (999 DE total)». *Base:* la carpeta más grande vista tiene 205.
  *Riesgo:* si hay carpetas mayores, sube el límite y la memoria del host.
- **suposición tomada** — el texto de página reemplaza a «MOSTRANDO 20 DE n» de M41-21 y del AC9 de `menu-tnfs`.
  *Base:* la línea de mensaje es una sola. *Riesgo:* cambian esas pruebas, a propósito.
- **suposición tomada** — el boceto de «What result they get» es la referencia. *Base:* aprobado por el
  responsable con la spec («Approve spec», 2026-11-19). *Riesgo:* ninguno nuevo.
- **área no formulable** — cómo se combina el cursor con el montaje por referencia de la spec B (por ejemplo, una
  imagen todavía no verificada). Se formula en esa spec.

## Clarifications

- 2026-11-19 — clarify, sin preguntas nuevas: ninguna brecha cambia lo que se construye. Resultado por punto:
  joystick en el puerto 1, validada (hoy ningún código lee el joystick; `Menu6502` lee memoria por `read()`, así
  que la prueba es posible); repetición 0,5 s y 0,15 s, validada; límite de 999 entradas, validada (hoy
  `TnfsFolder` guarda 20 en un arreglo fijo; el cambio es del plan); texto de página en lugar de M41-21 y AC9,
  validada; boceto como referencia, validada por la aprobación; cursor con montaje por referencia, sigue como
  área no formulable de la spec B. Brechas nuevas cerradas como comportamiento: fuentes vacías, «CARGANDO nn%» y
  carpeta vacía (sección Behaviour y dos criterios nuevos).

## Revisions

- 2026-11-19 — borrador. Decisiones del responsable: dos specs (A primero), letras y cursor juntos, cursor en
  todas las elecciones, izquierda y derecha cambian de página, la fuente local también se pagina.
- 2026-11-19 — aprobada por el responsable («Approve spec»), con el boceto como referencia.
- 2026-11-19 — tras clarify, el responsable aprueba los tres comportamientos nuevos («Approve, go to plan»).
