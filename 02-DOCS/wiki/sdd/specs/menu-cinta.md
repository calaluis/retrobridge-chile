---
type: spec
title: Spec — Elegir una cinta .cas desde el menú del 65XE y cargarla
description: QUÉ y POR QUÉ de montar un .cas de la microSD en la línea C: del menú y cargarlo en el 65XE con la FujiNet.
tags: [sdd, spec, firmware, sio, menu, cassette]
timestamp: 2026-11-23T00:40:00Z
topic: sdd
slug: menu-cinta
branch: spec/menu-cinta
status: approved
---

# Spec — Elegir una cinta .cas desde el menú del 65XE y cargarla

> Slug: `menu-cinta` · Estado: **aprobada** por el responsable el 2026-11-23 («Apruebo, commitea») · Creada: 2026-11-23
> Rama: `spec/menu-cinta`, desde `main` `b3bd374`, por decisión del responsable.
> Base: `cassette-cas` (casetera en el ESP32, C100–C107), `menu-principal`, `menu-sio-fisico`,
> `menu-unidades-persistentes`. Cierra la «decisión diferida» de `cassette-cas`: el menú del Atari para cintas.

## Problema y por qué

La casetera ya existe en el firmware (`cassette-cas`, fusionada). Lee `.cas` estándar y turbo con el código FSK
propio del responsable, y se probó en la FujiNet por USB: `cas mount`, `status`, `rewind` y `eject`. Pero la cinta
solo se elige **desde la consola USB del ESP32**, con un PC conectado.

El menú del 65XE muestra la línea `C: SIN CINTA` desde `menu-principal`, sin ningún soporte detrás. El paquete
`menu-sio`, el que el responsable usa en el 65XE, **no arranca la casetera**: los dos comparten la UART2 y el pin
DATA IN (`main/app_main.cpp`). Con el Atari solo, sin PC, hoy no hay forma de cargar una cinta.

Disparador: el responsable, 2026-11-23: «es importante que el firmware de retrobridge pueda leer archivos .cas».
El objetivo del producto es que el firmware funcione al 100 % en la FujiNet con `.ATR`, `.XEX`, `.CAS` y Wi-Fi/TNFS.

## ¿Qué pasa si no lo construimos?

- Las cintas solo cargan con un PC conectado por USB a la FujiNet, escribiendo `cas mount` en la consola.
- El paquete del menú y el de la casetera son firmwares distintos: cambiar de discos a cintas obliga a flashear.
- El objetivo «`.CAS` al 100 % en la FujiNet» no se cumple.

## La alternativa más barata

- **Seguir con la consola USB** (`cas mount <ruta>`). Funciona, pero exige el PC y un paquete distinto del menú.
- **Un paquete «solo casetera»** que monte siempre la misma cinta. Sin menú no se elige cuál.
- **No construirlo.**

Ninguna deja elegir y cargar una cinta desde el 65XE sin PC. Por eso se construye.

## Objetivos

- Desde el menú del 65XE, elegir un `.cas` de la microSD y dejarlo montado en `C:`.
- Encender el 65XE con START y que cargue esa cinta, con la FujiNet sola, sin PC.
- Discos (D1–D8), `.XEX` y TNFS siguen funcionando igual en el mismo firmware.

## Usuarios y contexto

El dueño del 65XE con la FujiNet conectada al SIO, sin PC ni consola USB. Tiene sus cintas `.cas` en la raíz de la
microSD, junto a sus `.ATR` y `.XEX`.

## Qué podrá hacer el usuario

- Ver sus `.cas` en la lista de la fuente 1 (microSD), junto a los `.ATR` y `.XEX`.
- Elegir una cinta con su letra y, como destino, pulsar `C` en lugar de un número de unidad. La línea `C:` del menú
  muestra el nombre de la cinta.
- Sacar la cinta de `C:` desde el menú, como se desmonta una unidad.
- Apagar el 65XE, encenderlo manteniendo START (y OPTION en el XE, para quitar el BASIC), oír el pitido y pulsar
  una tecla: el 65XE carga la cinta.
- Volver al menú como hoy (botón de la FujiNet o `menu` por consola) para elegir otra cinta o un disco.

## Qué resultado obtiene

- **Referencia aprobada:** el comportamiento del menú en el 65XE ya aprobado en `menu-principal` y `menu-navegacion`
  (pantallas, teclas, línea `UNIDAD PARA:`), y el de la casetera en `cassette-cas` (AC10–AC12: International Karate,
  Alien Ambush y una cinta turbo solo `fsk `).
- En el menú: la línea `C:` dice `C: <NOMBRE>.CAS` con la cinta montada y `C: SIN CINTA` sin ella.
- Encendido con START: el 65XE pita, se pulsa una tecla y la cinta carga como en una casetera real. Una cinta estándar
  y una turbo cargan y corren.
- Durante la carga de la cinta, las unidades D1–D8 no cambian. Después, el menú vuelve a estar disponible al
  encender sin START.
- Una cinta que no se puede leer (formato no válido, sin memoria) se rechaza al elegirla con un aviso en la línea de
  mensajes; `C:` no cambia.

## Qué debe respetar

- **Puertas de hardware (`cassette-cas`, `docs/mediciones/puertas.json`):** la casetera no usa el MOTOR sin M3a
  aprobada, ni escribe en DATA IN sin H4 aprobada. Este cambio no aprueba ninguna puerta: las aprueba solo el
  responsable, en un PR aparte. Sin ellas, elegir la cinta en el menú funciona, pero no se carga.
- **El disco sigue primero:** el arranque temprano de D1 (< 1 s desde EN, `menu-sio-fisico`, PR #77), las unidades
  persistentes (`menu-unidades-persistentes`) y el Wi-Fi/TNFS (`menu-sio-wifi-tnfs`) no cambian.
- **Una sola línea DATA IN:** disco y cinta no la usan a la vez. Cuándo pasa de uno a otro lo decide el plan; el
  resultado observable es que el disco responde como hoy cuando no hay cinta en marcha.
- **Código FSK propio** (ADR-012 enmienda 1): la reproducción de la cinta es la de `cassette-cas`, sin cambios de
  comportamiento. Sin código de terceros.
- **NVS:** nunca se borra ni se escribe; lo que se recuerde de `C:` va a la microSD, como D1–D8.
- **Lo ya aprobado de `cassette-cas` sigue valiendo:** formatos aceptados, bloques `fsk `, reanudar con MOTOR.

## Cómo lo demuestra

- **Pruebas de host** (PC, sin hardware): el menú lista `.cas`, asigna `C:`, la muestra, la desmonta, rechaza una
  cinta no válida y recuerda `C:` tras un apagado. Quedan en `ctest` y en el CI.
- **CI:** el paquete `menu-sio` compila con la casetera dentro y pasa sus comprobaciones (incluidas las de puertas).
- **Banco en el 65XE, antes de las puertas:** elegir una cinta, ver `C: <NOMBRE>.CAS`, apagar y encender, verla aún
  en `C:`. Evidencia: registro en `02-DOCS/raw/` con el paquete y lo visto en pantalla.
- **Banco en el 65XE, después de M3a y H4:** cargar las tres cintas de referencia de `cassette-cas` (AC10–AC12)
  desde el menú. Evidencia: registro con el paquete, la cinta, lo visto en pantalla y la consola si estaba conectada.

## Comportamiento

1. **Listar.** La fuente 1 muestra los `.cas` de la raíz de la microSD con los `.ATR` y `.XEX`, en el mismo orden.
2. **Elegir.** Letra de una cinta → `UNIDAD PARA: <NOMBRE>` → `C` la monta en `C:`. Un número (1–8) con una cinta
   elegida no hace nada útil para un disco: se rechaza con un aviso (una cinta no va en una disquetera).
   Una `.ATR` o `.XEX` con `C` se rechaza también (un disco no va en la casetera).
3. **Mostrar.** La línea `C:` muestra el nombre de la cinta, recortado al ancho de la línea si hace falta.
4. **Desmontar.** El mismo flujo de desmontar unidades ofrece `C`: la línea vuelve a `C: SIN CINTA`.
5. **Recordar.** `C:` se recuerda tras apagar y encender, como D1–D8.
6. **Cargar.** Con la cinta en `C:`, al encender con START el Atari pide la cinta (MOTOR): la FujiNet la reproduce
   desde el principio. Cada encendido con START empieza la cinta desde el principio.
7. **Sin puertas.** Con M3a o H4 sin aprobar, los pasos 1–5 funcionan; el paso 6 no reproduce nada y la consola dice
   por qué. El menú no promete una carga que no va a ocurrir: la línea de mensajes lo dice al montar.
8. **Errores.** Cinta no válida o sin memoria → aviso en la línea de mensajes, `C:` sin cambios.

## Criterios de aceptación

- **AC1** — Dada una microSD con `international_karate.cas`, cuando se abre la fuente 1, entonces la lista muestra
  esa cinta con su letra junto a los `.ATR` y `.XEX`.
- **AC2** — Dada la cinta elegida en la lista, cuando se pulsa `C`, entonces la pantalla principal muestra
  `C: INTERNATIONAL_KARATE.CAS` (o su nombre recortado) y D1–D8 no cambian.
- **AC3** — Dada una cinta elegida, cuando se pulsa un número 1–8, entonces la línea de mensajes dice que una cinta
  va en `C:` y ninguna unidad cambia. Dada una `.ATR`, cuando se pulsa `C`, entonces se rechaza y `C:` no cambia.
- **AC4** — Dada una cinta en `C:`, cuando se desmonta `C`, entonces la línea dice `C: SIN CINTA`.
- **AC5** — Dada una cinta en `C:`, cuando se apaga y enciende el 65XE (FujiNet solo por SIO), entonces el menú
  sigue mostrando la cinta en `C:`.
- **AC6** — Dado un `.cas` no válido, cuando se elige con `C`, entonces la línea de mensajes da un aviso y `C:` no
  cambia.
- **AC7** — Dado el paquete `menu-sio` con la casetera dentro, cuando se enciende con una cinta en `C:` y sin START,
  entonces D1 sirve el menú en el mismo plazo que hoy y los discos, `.XEX` y TNFS funcionan igual.
- **AC8** — Con M3a o H4 sin aprobar, cuando se monta una cinta, entonces el menú avisa que no podrá cargarse y la
  casetera no toca MOTOR ni DATA IN.
- **AC9** *(tras M3a y H4)* — Dada `international_karate.cas` en `C:`, cuando se enciende con START+OPTION y se pulsa
  una tecla, entonces el juego carga y corre.
- **AC10** *(tras M3a y H4)* — Igual con Alien Ambush (turbo): el Atari muestra `PROGRAMA CARGADO`.
- **AC11** *(tras M3a y H4)* — Igual con una cinta turbo solo `fsk `: carga y corre.
- **AC12** *(tras M3a y H4)* — Tras cargar una cinta, al encender sin START aparece el menú y D1–D8 siguen
  asignadas.

*Nota (2026-11-24):* T9a prueba en host la lógica de AC9–AC12 (traspaso, plazos, fin de cinta) con
`kTapeOutput = false`. Esa prueba **no** es evidencia física: AC9–AC12 se cumplen solo en el banco T9b, con los
requisitos previos del plan (M3a medida, protección validada, H4-C y la aprobación de H4 y M3a).

## Fuera de alcance

- Cintas desde TNFS (fuentes 2–8). Se verá en otra spec.
- Grabar en cinta (`SAVE` a `C:`), crear `.cas` nuevos.
- Elegir posición dentro de la cinta (avance o retroceso) desde el menú. Cada carga empieza desde el principio.
- Aprobar M3a o H4. Son decisiones del responsable, en un PR aparte.
- Cambiar la reproducción FSK de `cassette-cas`.
- Cargar la cinta desde el menú con una tecla tipo `A` (sin START): el 65XE arranca cintas solo con START.

## Puntos a clarificar

- **suposición tomada** — las cintas se eligen solo de la microSD (fuente 1). *Base:* el responsable aceptó la
  recomendación el 2026-11-23; la casetera ya lee de `/sd/`. *Riesgo:* bajo; TNFS queda fuera de alcance.
- **suposición tomada** — flujo: letra de la cinta y `C` como destino; carga encendiendo con START (y OPTION en el
  XE) y una tecla. *Base:* el responsable aceptó la recomendación el 2026-11-23; es el arranque de cinta del Atari.
- **suposición tomada** — `C:` se recuerda tras apagar, como D1–D8. *Base:* el flujo exige apagar y encender con
  START entre elegir y cargar; con la FujiNet solo por SIO, la placa se apaga en medio. *Riesgo:* si no se recuerda,
  la cinta elegida se pierde justo antes de cargarla.
- **suposición tomada** — cada encendido con START empieza la cinta desde el principio. *Base:* es lo que espera
  quien carga una cinta; la posición no se recuerda.
- **suposición tomada** — el texto exacto de los avisos (cinta en disquetera, disco en casetera, sin puertas, cinta
  no válida) lo fija el plan, en mayúsculas y dentro de 40 columnas, como los demás avisos del menú.
- **suposición tomada** — con M3a/H4 sin aprobar, el menú deja montar la cinta y avisa que no podrá cargarse (AC8);
  no lo impide. *Base:* el responsable aprobó la recomendación el 2026-11-23. *Riesgo:* ninguno sobre el hardware:
  la casetera no toca MOTOR ni DATA IN sin las puertas.
- **suposición tomada** — `A` arranca D1 o `C:` según las teclas de consola, como el Atari al encenderlo: `A` con
  OPTION arranca D1 sin BASIC; `A` sola, D1 con BASIC; `A` con START+OPTION, la cinta de `C:`. *Base:* el
  responsable lo pidió tras el banco T8 (2026-11-23); `A` ya salta al arranque en frío del sistema operativo
  (`COLDSV`), que lee START y OPTION. *Riesgo:* sin comprobar en el 65XE; se prueba en T9 (requisitos 3–5 del plan).
- **decidido (2026-11-24, responsable, T9a.5)** — qué contesta D1 en el arranque de disco que el sistema operativo
  intenta después de una cinta (requisito 4 del plan). `A` con D1 vacía y una cinta lista en `C:` saca el menú de
  D1 y deja D1 vacía: D1 responde como unidad sin disco (error inmediato), así que el OS no espera ni carga el menú
  sobre el programa de la cinta. Con un disco en D1, D1 lo sirve como siempre. Un arranque en frío (65XE apagado y
  encendido) devuelve el menú a D1. Con las puertas cerradas, `A` no arranca y dice
  `CINTA EN C:, AUN NO PUEDE CARGARSE` (como AC8). Probado en host en T9a.5; falta el 65XE (T9b).
- **decisión diferida** — cintas por TNFS.
- **área no formulable** — cómo convive la línea DATA IN entre disco y cinta si el Atari usa ambos en la misma
  sesión (por ejemplo, un juego de cinta que luego lee disco). Se precisa en el plan con el diseño del traspaso.

## Objeción registrada

Planteada al responsable el 2026-11-23: el paquete `menu-sio` no arranca la casetera porque comparten UART2 y DATA
IN; el traspaso de la línea entre disco y cinta es el punto delicado. Y aunque el spec avance, cargar una cinta en el
65XE sigue necesitando M3a y H4. El responsable decidió seguir: «es importante que el firmware de retrobridge pueda
leer archivos .cas».

## Clarificaciones

- 2026-11-23 — ¿Montar cintas sin M3a/H4? → montar y avisar, no impedir → permite probar AC1–AC8 en el 65XE antes
  de las puertas, y la casetera sigue sin tocar MOTOR ni DATA IN.
- 2026-11-23 — ¿Fuente de las cintas? → solo la microSD (fuente 1) → la casetera ya lee de `/sd/`; TNFS después.
- 2026-11-23 — ¿Flujo en el Atari? → letra de la cinta, `C` como destino; cargar encendiendo con START (+OPTION en el
  XE) y una tecla → es el arranque de cinta del Atari.
- 2026-11-23 (tras el banco T8) — ¿Cómo distingue `A` entre D1 y `C:`? → por las teclas de consola: OPTION, D1 sin
  BASIC; nada, BASIC; START+OPTION, `C:` → el responsable: «son dos dispositivos distintos»; es la misma regla del
  Atari al encenderlo, sin tecla nueva. Va a T9.
