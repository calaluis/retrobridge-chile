---
type: spec
title: Spec — Cargar cintas .cas (nivel 1) desde el menú
description: QUÉ y POR QUÉ de que RetroBridge cargue en el Atari cintas .cas estándar y turbo tipo A (por bytes), en Altirra por NetSIO, elegidas desde el menú.
tags: [sdd, spec, menu, cassette, cas, netsio]
timestamp: 2026-11-20T20:00:00Z
topic: sdd
slug: cassette-cas
status: approved
---

# Spec — Cargar cintas .cas (nivel 1) desde el menú

## Problem & why

Una parte de la colección del responsable existe solo en cinta: `.cas` estándar (International Karate, Night
Knight) y `.cas` del sistema de carga turbo software. RetroBridge hoy solo sirve discos (`.ATR`) y programas
(`.xex`). Un `.cas` no aparece en el menú y no se puede cargar.

Disparador: el responsable lo pidió el 2026-11-20: «que el firmware lea archivos .cas legacy y .cas del sistema de
carga turbo software». Se decidió hacerlo por niveles. Esta spec es el **nivel 1**: cintas cuyos datos son bytes
(bloques `baud` + `data` del formato A8CAS). Eso cubre los `.cas` estándar y el turbo **tipo A** (por ejemplo,
`PAL_ALIEN_AMBUSH_TURBO_SOFTWARE`, `NTSC_BRUCE_LEE_TURBO_SOFTWARE_FUJINET`, QROS EMOSOFT).

## Cost of not building it

- Los juegos que solo existen en cinta quedan fuera de RetroBridge.
- Para usarlos hay que volver a FujiNet o a la casetera del emulador, fuera del menú y fuera del flujo de TNFS.
- Con el ESP32 (sin PC) no habría ninguna forma de cargarlos.

## The cheapest alternative

- **La casetera de Altirra.** Altirra carga `.cas` por sí sola. Sirve en el PC, pero no en el Atari real ni desde
  TNFS, y no pasa por RetroBridge.
- **Convertir la cinta a `.xex` o `.ATR`.** Funciona con algunos `.cas` estándar, a mano y archivo por archivo. No
  funciona con el turbo: el cargador turbo es parte del programa.
- **No construirlo.**

Ninguna carga un `.cas` desde el menú, local o TNFS, así que se construye.

## Objeción registrada

El SO del Atari mide la velocidad de la cinta con los bytes de sincronía (`$55 $55`) y los temporizadores de POKEY.
No está comprobado que Altirra entregue por NetSIO los bytes del cassette con el tiempo que esa medición necesita.
Si no lo hace, este diseño no sirve. El responsable decidió (2026-11-20) que la **primera tarea sea una prueba de
viabilidad**: si un `.cas` pequeño no carga en Altirra por NetSIO, la spec se detiene y se replantea.

## Goals

- G1. El menú muestra los `.cas` de la carpeta local y de TNFS, junto a `.ATR` y `.xex`.
- G2. Un `.cas` elegido queda montado en la casetera (`C:`) y la pantalla principal lo muestra.
- G3. Con la cinta montada, el Atari la carga como con una casetera real: arranque con START, o `CLOAD` / `RUN"C:"`
  desde BASIC.
- G4. La cinta avanza solo con el MOTOR encendido, respeta las velocidades (`baud`) y las pausas de cada bloque.
- G5. Un `.cas` que no es de nivel 1 o que está roto da un mensaje claro al elegirlo.

## Non-goals / out of scope

- Turbo **tipo B**: `.cas` de pulsos (bloques `fsk ` con datos), por ejemplo `turbo_software_*`, `tt_international`,
  `tt_river_raid`. Spec propia (nivel 2).
- Grabar en cinta, rebobinar, adelantar o pausar a mano.
- Firmware ESP32 (H4) y la línea MOTOR física (pin 8). Ver *Points to clarify*.
- Convertir `.cas` a otros formatos.
- Cambiar el comportamiento de `.ATR` y `.xex`.

## Users & context

El usuario del Atari frente al menú de RetroBridge, servido por `retrobridge_host` en D1, en **Altirra** con el hub
NetSIO (un solo host por hub; unidades servidas en Off; la casetera propia de Altirra sin cinta). Su colección mezcla
`.atr`, `.xex` y `.cas`, en la carpeta local y en TNFS.

## What the user can do

- Ver los `.cas` en la lista de una fuente, mezclados con carpetas, `.ATR` y `.xex`.
- Elegir un `.cas`: queda montado en `C:`, sin pedir unidad.
- Cargarlo: reinicio en frío con START pulsado (y una tecla tras el pitido), o `CLOAD` / `RUN"C:"` desde BASIC.
- Expulsarlo con `U DESMONTAR UNIDAD`: el cursor recorre D1..D8 y después `C:`. O elegir otro `.cas`, que reemplaza
  al anterior y queda al principio.
- Reintentar una carga fallida con otro reinicio en frío: la cinta sigue montada y vuelve al principio.

Condición: el host corre con `--images`. No hay permisos.

## What result they get

1. La lista muestra el `.cas` con su nombre y la extensión `.CAS` visible.
2. La línea `C:` de la pantalla principal, que hoy dice `C: SIN CINTA` (spec `menu-principal`, Q4), muestra el
   nombre de la cinta montada. Sin cinta, sigue diciendo `C: SIN CINTA`.
3. Tras cargar, el programa corre. Referencias aprobadas para la demo: un `.cas` estándar (International Karate o
   Night Knight) y un turbo tipo A (Alien Ambush o Bruce Lee turbo software), de `D:\GitHub\Respaldos\MicroSD` o de
   TNFS, elegidos por el responsable; sus rutas quedan en el registro.
4. Un `.cas` de tipo B: mensaje ` CAS DE PULSOS: AUN NO` al elegirlo; `C:` no cambia.
5. Un archivo que no es un `.cas` válido: mensaje ` CAS NO VALIDO` al elegirlo; `C:` no cambia.

## What it must respect

- ADR-017 (menú de arranque) y sus enmiendas: terminal delgado; el host arma las pantallas.
- ADR-012 y su **enmienda 1**: se puede reutilizar, adaptado, el código propio del responsable de la cuarentena
  `D:\rb-e0\fsk-propio` (PRs de FujiNet #1756 y #1773, lectura A8CAS), registrando el commit de origen. Ningún otro
  código de FujiNet ni de emuladores. El formato A8CAS se describe con una fuente pública, registrada en
  `docs/fuentes.md`.
- ADR-018 (cliente TNFS): un `.cas` de TNFS se descarga como hoy una `.ATR` o un `.xex`.
- D1 sigue sirviendo el menú; `.ATR` y `.xex` funcionan igual.
- Un solo `retrobridge_host` por hub.

## How it proves it

- **Prueba de viabilidad primero** (tarea 1): un `.cas` estándar pequeño carga en Altirra por NetSIO, con evidencia
  (registro del host y captura). Si falla, se detiene la spec.
- Pruebas del host (`ctest`): el catálogo lista `.cas`; la lectura de un `.cas` produce los bloques, velocidades y
  pausas esperados; un tipo B y un archivo roto dan su mensaje; la cinta avanza solo con MOTOR encendido.
- Demo en Altirra con un solo host: un `.cas` estándar y uno turbo tipo A cargan y corren. Evidencia en
  `D:\rb-e0\m49-cas` (registro, versión, capturas).
- Registro de verificación en `02-DOCS/wiki/sdd/verifications/`.

## Behaviour

- **Lista.** Las fuentes muestran también `.CAS` (sin distinguir mayúsculas), en orden alfabético como hoy.
- **Elegir.** Elegir un `.cas` lo revisa y lo monta en `C:` al principio de la cinta. No pide unidad.
- **Clasificar.** Un `.cas` es de nivel 1 si sus datos van en bloques `data`. Un bloque `fsk ` en una cinta de nivel 1
  se trata como silencio de la misma duración. Un `.cas` cuyos datos van solo en bloques `fsk ` es tipo B.
- **Reproducir.** Con la cinta montada, cuando el Atari enciende el MOTOR, la cinta avanza: cada bloque `data` se
  envía a la velocidad del último `baud` (600 si no hay), tras su pausa. Cuando el Atari apaga el MOTOR, la cinta se
  detiene donde está y sigue desde ahí al volver a encenderse.
- **Fin de cinta.** Tras el último bloque la cinta queda al final; un nuevo MOTOR no envía nada.
- **Reinicio en frío.** La cinta sigue montada y vuelve al principio.
- **Expulsar.** `U` lleva el cursor por D1..D8 y `C:`; elegir `C:` la deja en `C: SIN CINTA`.
- **D1 durante un arranque con cinta.** Lo fija la prueba de viabilidad (AC1): si el menú en D1 estorba la carga
  desde cinta, el host no responde en D1 solo durante ese arranque. Fuera de ese caso, D1 sirve el menú como hoy.
- **Fallos.** Tipo B → ` CAS DE PULSOS: AUN NO`. Cabecera `FUJI` ausente o bloque cortado → ` CAS NO VALIDO`. En los
  dos casos `C:` no cambia.

## Acceptance criteria

- **AC1** — Dada una prueba de viabilidad, cuando se carga un `.cas` estándar pequeño en Altirra por NetSIO, entonces
  el programa corre; si no corre, la spec se detiene (registro de la prueba).
- **AC2** — Dada una carpeta local o TNFS con `JUEGO.CAS`, cuando se abre, entonces `JUEGO.CAS` aparece en la lista.
- **AC3** — Dado un `.cas` de nivel 1 elegido, entonces la pantalla principal muestra `C:` con su nombre.
- **AC4** — Dado un `.cas` estándar montado, cuando se reinicia en frío con START y se pulsa una tecla, entonces el
  programa carga y corre (demo).
- **AC5** — Dado un `.cas` turbo tipo A montado, cuando se carga, entonces el programa corre (demo).
- **AC6** — Dado un `.cas` con bloques `baud` 600 y 800, cuando se reproduce, entonces cada bloque `data` sale a su
  velocidad y tras su pausa (prueba del host).
- **AC7** — Dada una cinta montada, cuando el MOTOR se apaga a mitad de un bloque y vuelve a encenderse, entonces la
  cinta sigue desde donde quedó (prueba del host).
- **AC8** — Dado un `.cas` de solo bloques `fsk `, cuando se elige, entonces se ve ` CAS DE PULSOS: AUN NO` y `C:` no
  cambia.
- **AC9** — Dado un archivo sin cabecera `FUJI` o con un bloque cortado, cuando se elige, entonces se ve
  ` CAS NO VALIDO` y `C:` no cambia.
- **AC11** — Dada una cinta montada, cuando se pulsa `U` y se elige `C:`, entonces la línea dice `C: SIN CINTA`
  (prueba del host).
- **AC12** — Dada una cinta montada y leída en parte, cuando hay un reinicio en frío, entonces `C:` conserva su
  nombre y la cinta vuelve al principio (prueba del host).
- **AC10** — Las pruebas de hoy (`ctest`, `tools/tests`) siguen en verde: `.ATR` y `.xex` funcionan igual.

## Enfoques considerados

1. **Casetera virtual en el host, por NetSIO** (recomendado). El host reacciona a MOTOR y envía los bytes con cambio
   de velocidad. Usa mensajes que NetSIO ya tiene (`MOTOR_ON/OFF`, `SPEED_CHANGE`, datos). El ESP32 heredará la lógica.
2. **Convertir la cinta a disco.** Descartado: no sirve para el turbo.
3. **Dejarlo a la casetera de Altirra.** Descartado: no pasa por RetroBridge ni por TNFS.

## Points to clarify

- **suposición tomada** — los requisitos se numeran M49-n. *Base:* la última spec usó M48. *Riesgo:* ninguno.
- **suposición tomada** (validada en clarify) — D1 no debe estorbar el arranque desde cinta; la prueba de
  viabilidad decide si el host calla en D1 durante ese arranque.
- **suposición tomada** (validada en clarify) — la cinta sigue montada tras un reinicio en frío, al principio.
- **suposición tomada** — las pausas del `.cas` se respetan tal cual (responsable, 2026-11-20).
- **suposición tomada** — los mensajes ` CAS DE PULSOS: AUN NO` y ` CAS NO VALIDO` van en la fila de mensajes de la
  lista, como los de `.xex`. *Base:* spec `menu-presentacion`, AC9. *Riesgo:* ninguno.
- **decisión diferida** — turbo tipo B (pulsos): nivel 2, spec propia; antes hay que saber si NetSIO puede llevar
  pulsos.
- **decisión diferida** — ESP32 y la línea MOTOR física: se decide entre MOTOR y la mitigación C
  (`docs/hardware/mitigacion-c-motor-pin8.md`) **antes** de cualquier trabajo de cassette en el ESP32
  (responsable, 2026-11-20).
- **área no formulable** — compatibilidad de cada cargador turbo tipo A con el tiempo de NetSIO: se medirá en la demo.

## Clarifications

- 2026-11-20 — ¿Plataforma? → Altirra primero → probar sin hardware y sin abrir el riesgo de MOTOR.
- 2026-11-20 — ¿Alcance? → solo cargar → grabar y rebobinar no son necesarios ahora.
- 2026-11-20 — ¿Nivel? → nivel 1 (estándar y turbo tipo A) primero → por bytes, lo más directo.
- 2026-11-20 — ¿Cómo se elige y arranca? → desde el menú, START o `CLOAD` → como una casetera real.
- 2026-11-20 — ¿Pausas? → tal cual → algunos juegos las necesitan.
- 2026-11-20 — ¿Viabilidad? → prueba primero; si falla, se detiene.
- 2026-11-20 — ¿Dónde se trabaja? → rama `spec/cassette-cas` desde main, PR solo con la spec.

- 2026-11-20 (clarify) — ¿Cómo se expulsa? → `U` recorre D1..D8 y después `C:` → sin tecla nueva y sin cambiar el
  pie (AC11).
- 2026-11-20 (clarify) — ¿Tras un reinicio en frío? → la cinta sigue montada, al principio → se reintenta una carga
  sin volver al menú (AC12).
- 2026-11-20 (clarify) — ¿D1 con cinta y START? → lo decide la prueba de viabilidad → depende del orden de arranque
  del SO, que se observa, no se supone.

## Revisions

- 2026-11-20 — borrador (specify).
- 2026-11-20 — clarify: 3 preguntas resueltas, AC11 y AC12 nuevos.
- 2026-11-20 — aprobada por el responsable.
