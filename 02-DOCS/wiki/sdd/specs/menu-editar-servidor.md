---
type: spec
title: Spec — Editar o agregar un servidor desde el menú del Atari
description: QUÉ y POR QUÉ de la tecla E: escribir en el Atari el nombre o la IPv4 de un servidor TNFS y guardarlo en el archivo --sources.
tags: [sdd, spec, host, menu, tnfs]
timestamp: 2026-11-19T12:00:00Z
topic: sdd
slug: menu-editar-servidor
status: approved
---

# Spec — Editar o agregar un servidor desde el menú del Atari

Origen: pendiente del README del host («editar un servidor con E»). IDs de requisito: M43-n.

## Problem & why

- Hoy `E` solo muestra `PENDIENTE: E NECESITA ENTRADA DE TEXTO`. El host registra
  `menu_pending | edit server: needs text input on the Atari and a writable source list`.
- Cambiar un servidor exige cerrar el host, editar `--sources` en el PC y relanzar. Desde el Atari no se puede.

## Cost of not building it

- El menú no sirve sin el PC a mano. Probar otro servidor TNFS cuesta un reinicio del host.

## The cheapest alternative

El host edita el texto con el `K` de hoy: cada tecla es una transacción con 962 bytes (unos 0,6 s). 32 letras
serían unos 20 s, con eco lento. La enmienda 4 ya rechazó ese costo para el cursor. No basta.

## Goals

- Desde la principal, con el cursor en una fuente 2 a 8, `E` permite escribir un nombre o una IPv4 y guardarlo.
- Sirve igual en una fila vacía: así se agrega una fuente (por ejemplo la 2 con `--sources` vacío).
- Lo guardado queda en el archivo `--sources` y vale sin relanzar el host.

## Non-goals / out of scope

- Puerto distinto de 16384, usuario o ruta inicial en la fuente.
- La fuente 1 (local, `--images`).
- Editar desde la lista o la carpeta.
- Hardware real (solo Altirra, como el resto del menú).

## Users & context

El responsable, en Altirra, con el host en el PC. Teclado del Atari; el joystick no escribe texto.

## What the user can do

1. Marcar una fuente 2 a 8 en la principal (cursor o dígito) y pulsar `E`.
2. Escribir hasta 32 caracteres sobre la fila de la fuente. La fila parte con el nombre actual (vacía si no hay).
3. Borrar con Backspace (Delete). Cancelar con Esc. Guardar con RETURN.

## What result they get

- RETURN con texto válido: la fila muestra el nombre nuevo y el mensaje `FUENTE n GUARDADA`. El archivo
  `--sources` cambia. Abrir esa fuente usa el servidor nuevo.
- RETURN con texto no válido: el mensaje `NOMBRE O IPV4 NO VALIDO`; el texto sigue en edición para corregirlo.
- Esc: nada cambia, ni la pantalla ni el archivo.
- Registro del host: `menu_source_saved | source=n name=<nombre>` o `menu_source_rejected | source=n reason=...`.

## What it must respect

- Depende de `fix/menu-fila-en-daux2` (d2795ac, ADR-017 enmienda 5): AUX2 bits 4-0 traen la fila de la barra.
  `E` edita la fuente de esa fila, no la que el host dibujó. Hallazgo en Altirra: barra en la fila 2 vacía, `E`
  mostró `PENDIENTE` sin dejar escribir.
- ADR-017 y sus enmiendas. Cambia el contrato de `$5A`: se enmienda (Enmienda 6). M40-2 (reintentos no aplican
  dos veces), M41-1 (`key()` en 250 ms) y ADR-011 (la ruta SIO no reserva memoria) siguen.
- `rb_sio`, `rb_netsio` y el puerto `SioTransport` no cambian (ADR-016). El texto viaja en tramas de comando.
- ADR-012: programa 6502 propio, sin código de FujiNet ni de emuladores. `menu_program.inc` coincide con
  `tools/make_menu.py --check`.
- Formato de `--sources` (M40): una fuente por línea, `#` comenta, líneas vacías se saltan, máximo 7, 1 a 32
  caracteres ASCII imprimibles. El host conserva comentarios y líneas vacías al escribir.
- La escritura no deja el archivo a medias: si falla, el archivo queda como estaba y el mensaje lo dice.
- ADR-018: el cliente TNFS sigue de solo lectura. Escribir `--sources` es un archivo local del host.
- Sin `--sources`, `E` muestra `SIN ARCHIVO DE FUENTES` y no edita.

## Validación (M43-1)

- IPv4: cuatro números decimales 0-255 separados por `.`, sin ceros a la izquierda.
- Nombre: etiquetas de 1 a 63 caracteres (`a-z`, `A-Z`, `0-9`, `-`) separadas por `.`; ninguna empieza ni
  termina con `-`. Un texto con solo dígitos y puntos tiene que ser una IPv4 válida.
- Largo total de 1 a 32. Mayúsculas y minúsculas se guardan como se escribieron.

## How it proves it

- `tools/tests` (`Menu6502`): `E` entra en edición; letras, dígitos, `.` y `-` hacen eco en la fila; Backspace
  borra; el caracter 33 no entra; Esc no envía texto; RETURN envía el texto entero y luego pide redibujar.
- `test/host/menu`: validación (casos de la tabla de arriba), escritura que conserva comentarios, fila vacía que
  agrega, archivo de solo lectura, sin `--sources`, reintento de `SIOV` sin efecto doble, 250 ms sin reservar.
- Demo en Altirra: con `--sources` vacío, agregar la fuente 2 (`pulgarcito`, nombre del laboratorio), abrirla y
  montar una imagen sin relanzar el host. Evidencia en `D:\rb-e0\m46` (`m43` ya estaba usado).

## Acceptance criteria

- Given `--sources` vacío y el cursor en la fuente 2, When el usuario pulsa `E`, escribe un nombre válido y RETURN,
  Then la fila 2 lo muestra, el archivo tiene esa línea y abrir la fuente 2 lista su carpeta raíz.
- Given solo la fuente 2 configurada y el cursor en la 5, When guarda un nombre válido, Then queda como fuente 3
  y el mensaje dice `GUARDADA COMO FUENTE 3`.
- Given la fuente 2 configurada, When borra todo el texto y pulsa RETURN, Then nada cambia, como con Esc.
- Given la fuente 3 configurada, When edita su nombre y pulsa RETURN, Then cambia solo esa línea del archivo;
  comentarios y otras líneas siguen iguales.
- Given `192.168.1.300`, When pulsa RETURN, Then sale `NOMBRE O IPV4 NO VALIDO` y el archivo no cambia.
- Given la edición abierta, When pulsa Esc, Then la pantalla y el archivo quedan como antes.
- Given 32 caracteres escritos, When escribe otro, Then no entra.
- Given un archivo `--sources` de solo lectura, When guarda, Then el mensaje dice que no se pudo y el archivo no
  cambia.
- Given el host sin `--sources`, When pulsa `E`, Then sale `SIN ARCHIVO DE FUENTES`.
- Given la fuente 1 marcada, When pulsa `E`, Then sale `LA FUENTE 1 ES LOCAL` y nada cambia.
- Given un reintento de `SIOV` con el mismo comando, When llega, Then el texto se aplica una sola vez.
- Given las pruebas de #40, `menu-tnfs` y `menu-navegacion`, When corren, Then siguen en verde.

## Enfoques considerados

1. **Edición local en el 6502 y envío por tramas de comando (recomendado).** El host responde a `E` con una
   acción nueva («editar») y el texto inicial en la fila. El 6502 edita sin SIO. Con RETURN envía un comando
   nuevo de `$5A`, uno por caracter (AUX1 posición, AUX2 caracter, sin datos de vuelta: ACK y COMPLETE), y luego
   `K` con un código de «guardar». Escribir en una posición es idempotente, así que un reintento no duplica.
   Unas 33 transacciones cortas al final, no 33 pantallas. No toca `SioTransport`.
2. **Trama de datos del Atari al host (escritura SIO).** Una sola transacción, pero `SioTransport` no recibe
   datos hoy: cambia un puerto de `rb_application` y choca con ADR-016.
3. **El host edita con `K` (la alternativa barata).** Sin cambios en el 6502 más allá de no borrar Shift; unos
   0,6 s por tecla. Rechazado por lentitud.

## Points to clarify

- **decidido** — fila vacía más abajo de la última configurada (por ejemplo la 5 con solo la 2): se agrega al
  final del archivo y toma el primer número libre (la 3); el mensaje dice `GUARDADA COMO FUENTE 3`. El formato
  de `--sources` no cambia.
- **decidido** — texto vacío + RETURN: no borra; equivale a Esc. Borrar fuentes queda para otra spec.
- **suposición tomada** — el cambio vale al instante: el host cierra la sesión TNFS vieja de esa fuente (UMOUNT)
  y la próxima apertura usa el nombre nuevo. *Riesgo:* si una carga de esa fuente está en curso, `E` no se acepta
  hasta que termine o se cancele.
- **suposición tomada** — caracteres admitidos al escribir: `A-Z`, `a-z` (con Caps), `0-9`, `.` y `-`. Los demás
  se ignoran en el 6502. *Riesgo:* ninguno; la validación del host decide.
- **suposición tomada** — se permite el mismo servidor en dos fuentes. *Riesgo:* ninguno.

## Clarifications

- 2026-11-19 — clarify, dos preguntas al responsable: fila vacía toma el primer número libre; texto vacío
  equivale a Esc. Las demás brechas quedan como suposiciones (arriba).

## Revisions

- 2026-11-19 — borrador; clarify con dos respuestas del responsable. Aprobada por el responsable («Apruebo ambas»), con base d2795ac.
- 2026-11-19 — tras la demo (pedido del responsable, «Arréglalo»): un servidor que no responde se vuelve a abrir
  y a resolver en el siguiente intento (un DNS corregido sin relanzar); «host no responde» deja solo el título y
  el aviso. Ambos en ADR-017 enmienda 6, puntos 5 y 6. Demo con un solo host: aprobada («Se ve bien»).
- 2026-11-19 — la spec `menu-borrar-fuente` (M44, ADR-017 enmienda 7) reemplaza «texto vacío = Esc»: sobre una
  fuente configurada, el texto vacío + RETURN la borra. Sobre una fila vacía sigue siendo Esc.
