---
type: spec
title: Spec — Escritura en disco desde el Atari (D1 a D8)
description: QUÉ y POR QUÉ de aceptar escrituras SIO (W y P) en las ATR locales que sirve retrobridge_host con --images.
tags: [sdd, spec, host, menu, sio, escritura]
timestamp: 2026-11-19T23:30:00Z
topic: sdd
slug: menu-escritura-disco
status: approved
---

# Spec — Escritura en disco desde el Atari (D1 a D8)

Origen: pendiente del README del host («escritura en disco (unidades de solo lectura)»). Meta del producto: un D1
usable. IDs de requisito: M45-n.

## Problem & why

- Las unidades D1 a D8 son de solo lectura. Un DOS no puede guardar un archivo, un programa en BASIC no se puede
  grabar y un juego no guarda su partida.
- Un D1 «usable» incluye guardar. Hoy el Atari recibe un error en cada escritura.

## Cost of not building it

- RetroBridge sirve para cargar y jugar, no para trabajar. Para guardar hay que montar discos en Altirra, que es
  justo lo que el menú quiere evitar.

## The cheapest alternative

- Que Altirra sirva el disco de trabajo y RetroBridge el resto. Funciona hoy, pero mezcla dos fuentes de discos y
  rompe la meta de manejar todo desde el menú.
- Escritura solo en memoria hasta desmontar. Es barata, pero lo guardado se pierde al cerrar el host sin aviso.

## Objeción registrada

- Escribir no es un cambio solo del host. `DiskImage` es de solo lectura en el dominio, el puerto de bytes de la
  imagen solo lee y el transporte NetSIO contesta `write_size 0` (ADR-016). Estas piezas también las usa el
  firmware ESP32. Por eso hace falta ADR-019 y una prueba de que el firmware sigue de solo lectura. El
  responsable eligió seguir.

## Goals

- El Atari escribe sectores con `W` (con verificación) y `P` (sin verificación) en una ATR local escribible.
- Lo escrito queda en el archivo `.atr` de `--images`, en su sitio, como en un disco real (M45-1).
- Una unidad protegida lo dice en STATUS y rechaza la escritura sin cambiar nada (M45-2, M45-3).

## Non-goals / out of scope

- Firmware ESP32 y hardware real (solo Altirra por NetSIO).
- Escribir en un servidor TNFS, o guardar una copia local de una imagen TNFS (M45-2).
- Copias de seguridad, copia de trabajo aparte, deshacer o historial de versiones.
- Una tecla del menú para cambiar la protección (M45-3).
- Formatear (`!`, `"`) y crear imágenes nuevas desde el menú.
- Modo de una sola imagen (`--image`): sigue de solo lectura.
- Casete.

## Users & context

- La persona que usa el Atari emulado en Altirra con `retrobridge_host --images`: arranca un DOS o un programa y
  quiere guardar en un disco de la carpeta.

## What the user can do

1. Montar una ATR local en D1 a D8 desde el menú, como hoy.
2. Desde el programa del Atari (DOS, BASIC, un juego), escribir sectores en esa unidad.
3. Proteger una ATR marcándola de solo lectura en Windows (atributo del archivo) antes de montarla.
4. Ver en la pantalla principal qué unidades están protegidas (forma exacta: decisión de plan, en 40 columnas).

## What result they get

- ATR local escribible: `W` y `P` responden COMPLETE y el sector queda en el archivo. Un READ posterior, también
  tras Shift+F5, desmontar y volver a montar, o relanzar el host, devuelve lo escrito.
- ATR local con atributo de solo lectura: STATUS marca la protección y `W`/`P` responden ERROR. El archivo no
  cambia.
- Imagen traída por TNFS: siempre protegida, igual que la anterior. ADR-018 no cambia.
- Disco del menú (`*menu*`) en D1: siempre protegido.
- El registro JSONL anota cada escritura aceptada o rechazada con unidad, sector, resultado y CRC32 de los datos.

## What it must respect

- ADR-012: código propio, sin FujiNet ni emuladores. Fuentes públicas en `docs/fuentes.md`.
- ADR-011: sin reservar memoria en la ruta SIO.
- ADR-014: cada origen se cierra una sola vez; el montaje sigue por el buzón.
- ADR-016: `write_size 0` y «`09` se responde vacía» se reemplazan en ADR-019 nuevo (M45-4).
- ADR-017: enmienda 8 para la marca de protección en la pantalla principal (M45-4).
- ADR-018: sin cambios. El cliente TNFS sigue sin órdenes de escritura.
- ADR-002: la regla de dependencias (`check_layering`) sigue en verde.
- Firmware ESP32: sigue de solo lectura y sus pruebas siguen en verde.
- `--log` no puede nombrar una imagen de la carpeta (ya vigente).
- Un reintento de `SIOV` de la misma escritura deja el archivo igual que una sola escritura.

## How it proves it

- Pruebas de componentes y `test/host`: escribir y leer un sector en densidad simple y doble (los 3 primeros de
  128 bytes); `W` y `P`; atributo de solo lectura; imagen TNFS protegida; menú protegido; reintento; sector fuera
  de rango; fallo al guardar.
- Pruebas del firmware ESP32 y `check_layering` en verde.
- Demo en Altirra: DOS en D1, guardar un archivo en D2, cerrar y relanzar el host, montar D2 otra vez y leer el
  archivo. Luego, la misma ATR con atributo de solo lectura: el DOS informa error de protección. Evidencia: registro
  JSONL y SHA-256 del archivo antes y después.

## Behaviour

- Principal: `W`/`P` a una unidad escribible: el Atari envía el sector, el host lo guarda en el archivo y contesta
  COMPLETE.
- Borde: sector fuera de rango: ERROR, nada cambia. La misma ATR en dos unidades: solo la primera montada escribe;
  la otra se monta protegida.
- Error: falla al guardar en el PC (disco lleno, archivo bloqueado): ERROR al Atari; la causa queda en el registro.

## Acceptance criteria

- Given una ATR escribible en D2, When el Atari escribe el sector 10 con `P` y luego lo lee, Then recibe lo escrito.
- Given lo mismo con `W`, When escribe, Then COMPLETE y el mismo resultado.
- Given un sector escrito en D2, When se cierra el host, se relanza y se monta la ATR otra vez, Then el sector
  conserva lo escrito.
- Given una ATR con atributo de solo lectura en D2, When el Atari pide STATUS, Then STATUS marca la protección;
  When escribe, Then ERROR y el SHA-256 del archivo no cambia.
- Given una imagen TNFS en D3, When el Atari escribe, Then ERROR y STATUS marca protección.
- Given el menú en D1, When el Atari escribe en D1, Then ERROR y el menú no cambia.
- Given unidades protegidas, When se dibuja la pantalla principal, Then cada una lleva la marca de protección.
- Given un reintento de `SIOV` de la misma escritura, When llega, Then el archivo queda igual que con una sola.
- Given un fallo al guardar en el PC, When el Atari escribe, Then ERROR y el registro guarda la causa.
- Given el firmware ESP32, When corren sus pruebas, Then siguen en verde y su unidad sigue de solo lectura.
- Given las pruebas de `menu-tnfs`, `menu-navegacion`, `menu-editar-servidor` y `menu-borrar-fuente`, When
  corren, Then siguen en verde.

## Enfoques considerados

1. **Escribir en la ATR local, en su sitio (elegido por el responsable).** Como un disco real. Riesgo aceptado: un
   programa del Atari con errores puede dañar el archivo; la protección es el atributo de solo lectura.
2. **Copia aparte la primera vez que se escribe.** Original intacto; dos archivos por juego. Descartado.
3. **Solo memoria hasta desmontar.** Lo guardado se pierde al cerrar el host. Descartado.
4. **No construirlo.** Seguir con Altirra para el disco de trabajo. Descartado.

## Points to clarify

- **suposición tomada** — la misma ATR en dos unidades: solo la primera montada escribe; la otra se monta
  protegida. *Base:* evitar dos escritores sobre un archivo. *Riesgo:* bajo.
- **suposición tomada** — lo escrito llega al archivo antes de contestar COMPLETE. *Base:* un disco real confirma
  después de grabar. *Riesgo:* más lento; dentro de los tiempos de NetSIO.
- **suposición tomada** — el atributo de solo lectura se lee al montar; cambiarlo con la imagen montada no vale
  hasta volver a montarla. *Riesgo:* bajo.
- **suposición tomada** — `W` (con verificación) y `P` (sin verificación) guardan igual; la verificación la cumple
  la escritura confirmada. *Riesgo:* bajo.
- **decisión diferida** — formatear y crear imágenes vacías desde el menú.
- **decisión diferida** — copia local de una imagen TNFS para poder escribirla.

## Clarifications

- 2026-11-19 — P1: ¿dónde queda lo escrito en una ATR local? → en la ATR, en su sitio → como un disco real; sobrevive
  a cerrar el host.
- 2026-11-19 — P2: ¿imágenes TNFS? → protegidas → nada se pierde en silencio y ADR-018 no cambia.
- 2026-11-19 — P3: ¿cómo se protege una ATR local? → atributo de solo lectura del archivo → sin tecla nueva ni
  cambios de teclado en el 6502.
- 2026-11-19 — P4: ¿registro de decisiones? → ADR-019 nuevo (escritura de sectores; reemplaza la parte de solo
  lectura de ADR-016) y enmienda 8 de ADR-017 (marca de protección) → ADR-018 sin cambios.

## Revisions

- 2026-11-19 — borrador de `specify`. Base origin/main 39c8ff5.
- 2026-11-19 — `clarify`: cuatro preguntas al responsable, respuestas incorporadas.
- 2026-11-19 — aprobada por el responsable («Apruebo»), con las cuatro suposiciones tomadas a la vista.
- 2026-11-19 — precisión del plan, aceptada por el responsable: «el bit de protección vale 1» pasa a «STATUS marca
  la protección» (byte 1 = `$BF`, H2b intacta). La marca en pantalla es ` SL`.
