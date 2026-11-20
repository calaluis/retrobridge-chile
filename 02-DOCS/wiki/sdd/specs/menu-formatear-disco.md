---
type: spec
title: Spec — Formatear y crear discos desde el Atari
description: QUÉ y POR QUÉ de que el DOS formatee una ATR local y de que el menú cree una ATR nueva en blanco, en retrobridge_host con --images.
tags: [sdd, spec, host, menu, sio, formateo]
timestamp: 2026-11-20T00:00:00Z
topic: sdd
slug: menu-formatear-disco
status: approved
---

# Spec — Formatear y crear discos desde el Atari

Origen: decisión diferida de `menu-escritura-disco` («formatear y crear imágenes vacías desde el menú»). Meta del
producto: un D1 usable. IDs de requisito: M46-n.

## Problem & why

- Desde `menu-escritura-disco` el DOS guarda archivos, pero no puede formatear: el host no acepta las órdenes de
  formateo y el DOS informa un error.
- Para tener un disco vacío hay que fabricar la ATR en el PC con otra herramienta. La meta es manejar los discos
  desde el Atari.

## Cost of not building it

- Cada disco nuevo exige salir del Atari: copiar una ATR vacía a mano o crearla con Altirra. Un disco dañado no se
  puede recuperar formateándolo desde el DOS.

## The cheapest alternative

- Dejar en la carpeta unas ATR vacías ya formateadas y copiarlas en el PC. Funciona hoy, pero obliga a usar el PC
  para cada disco nuevo.
- Solo formatear, sin crear desde el menú. Cubre el disco dañado, no el disco nuevo.

## Objeción registrada

- Crear archivos desde el menú es nuevo: hasta hoy el host solo abría las ATR que ya estaban en la carpeta, y la
  lista de la fuente 1 se lee una vez al arrancar. Crear exige que la lista muestre el archivo nuevo y que un nombre
  escrito en el Atari no pueda salir de la carpeta ni pisar un archivo. El responsable eligió incluir las dos cosas.

## Goals

- El DOS formatea una ATR local escribible en densidad simple (720 sectores de 128 bytes) o media (1040 sectores de
  128 bytes) (M46-1).
- El menú crea una ATR nueva en blanco en la carpeta de `--images`, con el nombre que se escribe en el Atari y la
  densidad elegida, y la monta en una unidad (M46-2).

## Non-goals / out of scope

- Doble densidad (256 bytes por sector) y las órdenes de configuración de la unidad (PERCOM).
- Cambiar la densidad de una ATR al formatearla.
- Formatear o crear en un servidor TNFS, en `--image` o en el disco del menú.
- Borrar o renombrar ATR desde el menú.
- Escribir el DOS en el disco nuevo (eso lo hace el propio DOS, como en un disco real).
- Firmware ESP32 y hardware real. Casete.

## Users & context

- La persona que usa el Atari emulado en Altirra con `retrobridge_host --images`: necesita un disco vacío para
  guardar, o quiere limpiar uno.

## What the user can do

1. Desde el DOS, formatear una unidad que tiene una ATR local escribible (opción de formatear del DOS).
2. Desde el menú, crear una ATR nueva: escribir el nombre, elegir densidad simple o media y elegir la unidad
   D2..D8 donde montarla.
3. Después, formatear esa unidad desde el DOS y guardar en ella.

## What result they get

- Formatear con la densidad que la ATR ya tiene: COMPLETE. Todos los sectores quedan en cero en el archivo y el DOS
  ve un disco vacío. El tamaño del archivo no cambia.
- Formatear con otra densidad, o una unidad protegida (atributo de solo lectura, TNFS, menú): ERROR. El archivo no
  cambia.
- Crear: aparece el archivo `<nombre>.ATR` en la carpeta, con la cabecera ATR y la densidad elegida, se monta en la
  unidad elegida y la lista de la fuente 1 lo muestra.
- Crear con un nombre que ya existe, vacío o no válido: un mensaje de error en pantalla y nada cambia en la carpeta.
  Un nombre válido tiene de 1 a 25 caracteres entre A-Z, 0-9, `-` y `_`; el archivo se llama `<NOMBRE>.ATR`, en
  mayúsculas (M46-3).
- El registro JSONL anota cada formateo (unidad, densidad, resultado) y cada creación (nombre, densidad,
  resultado).

## What it must respect

- ADR-012: código propio, sin FujiNet ni emuladores. Las órdenes de formateo salen de documentación pública,
  registrada en `docs/fuentes.md`.
- ADR-011: sin reservar memoria en la ruta SIO.
- ADR-014: cada origen se cierra una sola vez; el montaje sigue por el buzón.
- ADR-019: un solo escritor por ATR; las unidades protegidas siguen rechazando, ahora también el formateo.
- ADR-017: el menú sigue siendo un terminal delgado; cada acción nueva va en una enmienda.
- ADR-002: `check_layering` sigue en verde.
- Un nombre escrito en el Atari no puede crear un archivo fuera de la carpeta de `--images`.
- Las pruebas de `menu-escritura-disco` y de los menús anteriores siguen en verde.

## How it proves it

- Pruebas de componentes y `test/host`: formatear en simple y en media; densidad distinta; unidad protegida;
  crear con nombre nuevo, repetido, vacío y con caracteres no válidos; la lista muestra el archivo nuevo.
- Demo en Altirra: crear `NUEVO.ATR` desde el menú en D2, formatearlo con el DOS, guardar un archivo, relanzar el
  host y leerlo. Evidencia: registro JSONL y SHA-256 del archivo.

## Behaviour

- Principal: el DOS pide formatear D2 con la densidad de su ATR; el host deja todos los sectores en cero y contesta
  COMPLETE con la lista de sectores malos vacía.
- Borde: la ATR está montada en dos unidades: solo la que escribe puede formatear (ADR-019).
- Error: falla al guardar en el PC durante el formateo o la creación: ERROR o mensaje en pantalla; la causa queda
  en el registro.

## Acceptance criteria

- Given una ATR escribible de 720 sectores en D2, When el DOS formatea en densidad simple, Then COMPLETE y todos los
  sectores del archivo quedan en cero.
- Given una ATR escribible de 1040 sectores en D2, When el DOS formatea en densidad media, Then COMPLETE y el mismo
  resultado.
- Given una ATR de 720 sectores, When el DOS formatea en densidad media, Then ERROR y el SHA-256 no cambia.
- Given una ATR protegida, una imagen TNFS o el menú, When el DOS formatea, Then ERROR y nada cambia.
- Given el menú, When se crea `NUEVO` en densidad simple y se elige D2, Then existe `NUEVO.ATR` de 720 x 128,
  queda montada en D2 y la lista de la fuente 1 la muestra.
- Given un archivo `NUEVO.ATR` ya existente, When se crea `NUEVO`, Then un mensaje de error y el archivo no cambia.
- Given un nombre vacío, de más de 25 caracteres o con un carácter fuera de A-Z, 0-9, `-` y `_`, When se crea, Then
  un mensaje de error y la carpeta no cambia.
- Given la lista de la fuente 1 con 999 entradas, When se crea, Then un mensaje de error y la carpeta no cambia.
- Given una ATR creada y formateada, When se guarda un archivo y se relanza el host, Then el archivo sigue ahí.
- Given las pruebas de `menu-escritura-disco` y de los menús anteriores, When corren, Then siguen en verde.

## Enfoques considerados

1. **Formatear y crear desde el menú (elegido por el responsable).** Cubre el disco nuevo y el dañado sin salir del
   Atari.
2. **Solo formatear.** Más chico; el disco nuevo sigue saliendo del PC. Descartado.
3. **Plantillas vacías en la carpeta.** Sin código; obliga a copiar en el PC. Descartado.
4. **No construirlo.** Descartado.

## Points to clarify

- **suposición tomada** — formatear deja todos los sectores en cero y la lista de sectores malos vacía. *Base:* un
  disco recién formateado no tiene datos; el DOS escribe después su estructura. *Riesgo:* bajo.
- **suposición tomada** — con la lista llena (999 entradas) no se crea: el archivo nuevo podría quedar fuera de la
  lista. *Riesgo:* bajo; la carpeta tendría que tener casi mil imágenes.
- **suposición tomada** — sin `--images` (solo `--image` o solo fuentes TNFS), «crear» muestra un mensaje y no hace
  nada. *Riesgo:* bajo.
- **suposición tomada** — formatear escribe los ceros en el archivo antes de contestar COMPLETE. *Base:* como la
  escritura de sectores (ADR-019). *Riesgo:* unos 130 KB por formateo; dentro del tiempo que el DOS espera.
- **suposición tomada** — la ATR nueva se crea en cero, sin formatear; el DOS la formatea después. *Base:* igual que
  un disco nuevo real. *Riesgo:* bajo.
- **pregunta abierta** — qué tecla abre «crear» en la pantalla principal y cómo se elige la densidad. Lo propone el
  plan dentro de las 40 columnas.

## Clarifications

- 2026-11-20 — Alcance: ¿formatear, crear o las dos? → las dos → crear sin formatear no le sirve al DOS.
- 2026-11-20 — Densidades: → simple (720 x 128) y media (1040 x 128) → las del DOS 2.5; doble queda fuera (PERCOM).
- 2026-11-20 — Otra densidad al formatear: → ERROR sin cambios → el archivo no cambia de tamaño.
- 2026-11-20 — Nombre: → se escribe en el Atari, con el editor de la tecla E → un nombre repetido es error.
- 2026-11-20 — clarify, nombre: → de 1 a 25 caracteres A-Z, 0-9, `-` y `_`, en mayúsculas, más `.ATR` → cabe en
  la línea de la unidad y Windows lo acepta.
- 2026-11-20 — Tras crear: → se elige la unidad D2..D8 y se monta, como al elegir una imagen.

## Revisions

- 2026-11-20 — borrador de `specify`, con las respuestas del responsable. Base origin/main a38aeaf.
- 2026-11-20 — `clarify`: una pregunta (nombre) y tres suposiciones nuevas (lista llena, sin `--images`, ceros
  antes de COMPLETE).
- 2026-11-20 — aprobada por el responsable («Apruebo»), con las seis suposiciones y la pregunta del plan a la
  vista.
