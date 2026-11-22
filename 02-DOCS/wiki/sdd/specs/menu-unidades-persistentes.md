---
type: spec
title: Spec — Unidades montadas que sobreviven al apagado del 65XE
description: QUÉ y POR QUÉ de recordar D1..D8 entre encendidos cuando la FujiNet se alimenta solo por el SIO.
tags: [sdd, spec, firmware, sio, menu]
timestamp: 2026-11-21T23:30:00Z
topic: sdd
slug: menu-unidades-persistentes
branch: fix/menu-sio-arranque-temprano
status: approved
---

# Spec — Unidades montadas que sobreviven al apagado del 65XE

> Slug: `menu-unidades-persistentes` · Estado: **aprobada** por el responsable el 2026-11-21 («Apruebo, commitea») · Creada: 2026-11-21
> Rama: `fix/menu-sio-arranque-temprano` (PR #77), por decisión del responsable.
> Base: `menu-sio-fisico`, `menu-principal`, `menu-arranque`.

## Problema y por qué

Desde el PR #77, la FujiNet funciona alimentada solo por el SIO del 65XE. Al apagar el Atari, la placa también se
apaga y olvida lo montado. Cada encendido empieza con D1..D8 vacías. El responsable lo observó en el banco del
2026-11-21: tras arrancar `DEMO-DOS.ATR`, apagar y encender perdió la unidad montada.

La spec `menu-sio-fisico` aprobó que «apagar y encender el 65XE arranca la misma ATR otra vez». Eso solo se cumple
con la FujiNet alimentada por USB. Con SIO solo, ese contrato se rompe. Esta spec lo sustituye.

## ¿Qué pasa si no lo construimos?

Cada encendido obliga a elegir otra vez cada imagen en el menú: unas pocas teclas por unidad. Con D2..D8 en uso
(un disco de datos, por ejemplo), hay que volver a montarlas todas. El coste es pequeño pero se repite en cada sesión.

## La alternativa más barata

No hacer nada y elegir otra vez desde el menú. Resuelve el caso de una sola unidad. No basta cuando se usan varias
unidades, ni cuando el usuario espera que la FujiNet se comporte como disqueteras con sus discos puestos.

## Objetivos

- Lo montado en D1..D8 sigue asignado tras apagar y encender el 65XE, sin USB.
- Al encender, siempre aparece el menú. Muestra lo asignado y deja arrancar D1 con `A`.

## Usuarios y contexto

El dueño del 65XE con la FujiNet conectada solo al SIO, sin PC ni consola USB.

## Qué podrá hacer el usuario

- Montar imágenes de la microSD en D1..D8 desde el menú, como hoy.
- Salir de cualquier carpeta del menú a la pantalla principal con Break.
- Apagar el 65XE, encenderlo después y ver en el menú las mismas asignaciones.
- Arrancar D1 con `A` sin volver a elegirla.
- Cambiar o desmontar cualquier unidad desde el menú; el cambio también se recuerda.

## Qué resultado obtiene

- Al encender, D1 sirve el menú dentro del plazo del PR #77 (D1 listo < 1 s desde EN). El menú muestra
  `RESTAURANDO UNIDADES` mientras la FujiNet recupera lo recordado.
- Las unidades restauradas están disponibles cuando aparece la pantalla principal operativa y se habilita `A`. Esa
  pantalla muestra en la línea de cada unidad la imagen recordada, igual que tras un Shift+F5 en Altirra
  (`menu-principal`: «vuelve D1 al menú y conserva lo asignado»). Desde ese momento, D2..D8 sirven sus imágenes.
- Si la microSD falla (no está, no se monta o no se lee), el menú queda operativo, sin unidades restauradas, y
  muestra el motivo.
- Al montar o desmontar, el menú dice `GUARDANDO` hasta que la escritura y la sincronización en la microSD terminan
  bien; entonces dice `GUARDADO`. Si fallan, dice `NO SE GUARDO: <motivo>`: la unidad funciona en esta sesión, pero
  no se recordará.
- Si una imagen recordada ya no existe o no es válida, la unidad queda vacía y el menú lo dice con el motivo.

## Qué debe respetar

- Cómo funciona el 65XE físico: desde un juego o DOS ya cargado, la única forma de volver al menú es apagar y
  encender el Atari. Break la atiende el programa que corre y no viaja por el SIO.
- El menú sigue apareciendo en el primer encendido, solo con SIO (criterio del PR #77). Recordar no puede retrasar
  D1 hasta perder ese arranque.
- Nunca se borra ni se reescribe en bloque el NVS: puede tener datos antiguos de FujiNet.
- Sin código de FujiNet ni de terceros.
- `menu-principal`: una ATR inválida se rechaza con el motivo y la unidad conserva lo que tenía.
- ADR-019: la misma ATR elegida otra vez se desmonta antes de montarse.
- Con USB, el comportamiento es el mismo: el menú aparece al encender aunque la FujiNet siga alimentada. D1 vuelve
  al menú antes del primer intento de lectura del Atari, también en un ciclo de apagado y encendido corto.
- Un montaje o desmontaje cuenta como guardado solo cuando termina bien la escritura y la sincronización efectiva
  de la microSD.
- La garantía de recuperación se limita a los casos en que sobrevive una copia válida de lo recordado y la tarjeta
  puede leerse. Un corte durante el guardado pierde, como mucho, el cambio que no llegó a `GUARDADO`.
- Una orden repetida por el Atari (por ejemplo, tras Break durante una orden SIO) no duplica un montaje ni un
  desmontaje. El menú termina la orden pendiente antes de atender Break.
- Tras un corte, se recupera la copia válida con la secuencia más reciente. Si la escritura nueva quedó incompleta
  y la anterior sigue legible, vuelve el estado anterior. Si la nueva quedó completa y válida aunque el menú no
  llegara a confirmarla, vuelve el estado nuevo.

## Cómo lo demuestra

- Banco físico con SIO solo: montar una ATR en D1 y otra en D2, apagar el 65XE, esperar 10 s, encender. Foto del
  menú con ambas asignaciones; `A` arranca D1 hasta su mensaje.
- Banco con la microSD sin la imagen de D2: el menú muestra D2 vacía y el motivo.
- Tests de host del menú: lo recordado se carga, se muestra y se reemplaza al cambiar una unidad.
- Registro de arranques del PR #77: D1 listo sigue por debajo de 1 s desde EN.

## Comportamiento

- Montar o desmontar una unidad desde el menú actualiza lo recordado, con `GUARDANDO`, `GUARDADO` o
  `NO SE GUARDO: <motivo>`.
- Al encender, D1 sirve el menú enseguida y se ve `RESTAURANDO UNIDADES`. Cuando la FujiNet recupera lo recordado,
  aparece la pantalla principal operativa: D2..D8 sirven sus imágenes y `A` queda habilitada.
- Si la microSD falla, la pantalla principal aparece igual, sin unidades restauradas y con el motivo.
- `A` arranca la imagen asignada a D1, como hoy.
- Una imagen recordada que falta o no valida deja su unidad vacía; el menú indica cuál y por qué.
- El botón A de la FujiNet y la orden `menu` siguen devolviendo D1 al menú; no borran lo recordado.
- Break, dentro de cualquier carpeta del menú, vuelve a la pantalla principal. No cambia ninguna unidad.
- Desde un juego o DOS ya cargado, se vuelve al menú apagando y encendiendo el 65XE.

## Criterios de aceptación

1. **Dado** D1 = `DEMO-DOS.ATR` y D2 = otra ATR, **cuando** se apaga y enciende el 65XE con SIO solo, **entonces** el
   menú aparece y muestra ambas asignaciones.
2. **Dado** el menú tras ese encendido, **cuando** se pulsa `A`, **entonces** arranca `DEMO-DOS.ATR` hasta
   `DEMO DOS OK` sin volver a elegirla.
3. **Dado** D2 recordada, **cuando** aparece la pantalla principal operativa y el Atari lee D2, **entonces** recibe la
   imagen recordada; antes de esa pantalla, `A` no está habilitada.
4. **Dado** una imagen recordada borrada de la microSD, **cuando** se enciende, **entonces** esa unidad queda vacía y
   el menú muestra el motivo; las demás unidades conservan lo suyo.
5. **Dado** que se desmonta D2 desde el menú, **cuando** se apaga y enciende, **entonces** D2 aparece vacía.
6. **Dado** cualquier estado recordado, **cuando** se enciende con SIO solo, **entonces** el menú aparece en el primer
   arranque, sin `RESET` ni `POKE`.
7. **Dado** el menú dentro de una carpeta (la lista de la microSD o una carpeta de una fuente, a cualquier
   profundidad), **cuando** se pulsa Break, **entonces** se ve la pantalla principal y las unidades no cambian.
8. **Dado** un montaje en D2, **cuando** la escritura en la microSD falla, **entonces** el menú muestra
   `NO SE GUARDO: <motivo>`, D2 funciona en esta sesión y tras apagar y encender D2 tiene lo anterior.
9. **Dado** dos cambios seguidos (montar D2 y luego D3) sobre un estado E0 ya confirmado:
   - **9a.** **Cuando** el primer cambio ya está confirmado (estado E1) y se corta la alimentación durante el guardado
     del segundo, **entonces** al encender se recupera E1: D2 montada, D3 como en E0.
   - **9b.** **Cuando** ninguno de los dos está confirmado y el corte deja incompleta su escritura, **entonces** al
     encender se recupera E0: ni D2 ni D3 cambian.
   - **9c.** **Cuando** la escritura nueva quedó completa y válida, pero el corte llega antes de que el menú pueda
     confirmar el guardado, **entonces** al encender se recupera el estado nuevo.
   Regla: al reiniciar se recupera la copia válida con la secuencia más reciente. Confirmado significa que la
   escritura y la sincronización en la microSD terminaron bien y el menú lo mostró.
10. **Dado** la FujiNet alimentada por USB, **cuando** se apaga el 65XE unos 2 s y se enciende, **entonces** D1 sirve
    el menú antes del primer intento de lectura del Atari y las asignaciones se conservan.
11. **Dado** Break pulsado durante una orden SIO de montaje cuya respuesta se pierde, **cuando** el menú repite la
    misma orden, **entonces** el montaje se aplica una sola vez, el menú recibe la respuesta de esa orden y solo
    después Break lleva a la pantalla principal.
12. **Dado** la microSD sin insertar, **cuando** se enciende, **entonces** aparece la pantalla principal operativa
    con el motivo y `A` habilitada.

## Fuera de alcance

- Arrancar directamente la imagen de D1 al encender (el responsable eligió ver siempre el menú).
- Imágenes de red (TNFS) recordadas.
- Recordar la página o el cursor del menú.
- Un menú de opciones para activar o desactivar el recuerdo.

## Puntos a clarificar

- **suposición tomada** — en la pantalla principal, Break no hace nada. *Base:* ya es el menú; el responsable la
  pidió para salir de una carpeta.
- **suposición tomada** — el botón A de la FujiNet y la orden `menu` no borran lo recordado. *Base:* hoy solo
  devuelven D1 al menú, y con el menú siempre al encender ya no hace falta borrar para verlo.
- **suposición tomada** — una imagen recordada inválida no bloquea el arranque: queda vacía. *Base:* `menu-principal`
  rechaza ATR inválidas con el motivo.
- **decisión diferida** — dónde se guarda lo recordado: el plan elige la microSD, sin tocar el NVS.
- **decisión diferida** — imágenes TNFS.

## Decisiones tomadas por el responsable (2026-11-21)

- Al encender aparece el menú con lo asignado; no arranca la imagen directamente.
- Se recuerdan D1..D8.
- Volver al menú: tecla Break. Aclarado el mismo día: «si el usuario está en una carpeta, sea cual sea, lo regrese
  al menú».
- La spec va en el PR #77.
- Contrato del arranque (aceptado en la revisión del plan): `RESTAURANDO UNIDADES` al encender; D1 dentro del plazo
  del PR #77; unidades disponibles con la pantalla principal operativa y `A`; microSD fallida → menú operativo con
  el motivo.

## Clarificaciones

- 2026-11-21 — ¿Cómo se vuelve al menú desde un juego cargado? → Solo apagando y encendiendo el 65XE. → No es una
  decisión: así funciona el 65XE físico (aclarado por el responsable). Con el menú siempre al encender, basta.
- 2026-11-21 — ¿Qué hace Break? → Dentro de cualquier carpeta del menú (lista de la microSD o carpeta de una
  fuente), vuelve a la pantalla principal. → Salir de una carpeta profunda con una tecla, sin recorrerla hacia atrás.

- 2026-11-21 — ¿«D2..D8 desde el primer instante»? → Disponibles cuando aparece la pantalla principal operativa y
  se habilita `A`. → D2..D8 viven en la microSD, que se monta después de servir D1; el Atari solo las pide desde un
  programa cargado, y ninguno existe antes de esa pantalla.
- 2026-11-22 — ¿Qué estado se recupera tras un corte con dos cambios? → El último confirmado: E1 si el primero
  terminó de guardarse, E0 si ninguno. → Se prueban por separado (9a y 9b).
- 2026-11-22 — ¿Y si la escritura nueva terminó pero el menú no llegó a confirmarla? → Se recupera el estado
  nuevo: manda la copia válida más reciente (9c). → La confirmación es aviso al usuario, no condición de validez.
- 2026-11-22 — ¿Qué pasa con la orden en curso al pulsar Break? → El menú la repite idéntica hasta terminarla y
  después atiende Break. → Un montaje cuya respuesta se perdió se aplica una sola vez.
- 2026-11-22 — ¿Y Break mientras se restauran las unidades? → Se descarta, como toda tecla durante
  `RESTAURANDO UNIDADES`. → La pantalla principal ya está a la vista; no hay carpeta de la que salir.
- 2026-11-21 — ¿Cuándo cuenta un cambio como guardado? → Solo tras escribir y sincronizar bien la microSD; el menú
  lo dice. → Que el usuario sepa qué sobrevivirá a un apagado.

## Objeción registrada

El coste de no construirlo es pequeño: elegir otra vez cuesta pocas teclas. El responsable decidió seguir.
