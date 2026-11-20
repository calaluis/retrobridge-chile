---
type: spec
title: Spec — Cargar cintas .cas reales en el ESP32
description: QUÉ y POR QUÉ de que RetroBridge cargue en un Atari real los .cas reales (estándar y turbo software, bloques data y fsk), en el ESP32, con el código FSK propio del responsable como base.
tags: [sdd, spec, cassette, cas, fsk, esp32, motor]
timestamp: 2026-11-20T23:30:00Z
topic: sdd
slug: cassette-cas
status: approved
---

# Spec — Cargar cintas .cas reales en el ESP32

## Problem & why

Una parte de la colección del responsable existe solo en cinta: `.cas` estándar (International Karate, Night
Knight) y `.cas` del sistema de carga turbo software (Alien Ambush, Bruce Lee, `turbo_software_*`, `tt_*`).
RetroBridge hoy solo sirve discos (`.ATR`) y programas (`.xex`). No carga ninguna cinta.

Disparador: el responsable lo pidió el 2026-11-20: «que el firmware lea archivos .cas legacy y .cas del sistema de
carga turbo software».

**Todas** las cintas reales de `D:\GitHub\Respaldos\MicroSD` traen bloques `fsk ` (señal de pulsos), también las
estándar: International Karate tiene 107 bloques `fsk ` y 106 `data`. Una cinta real solo carga si esa señal se
reproduce tal cual.

El responsable ya resolvió eso en FujiNet: PRs #1722, #1756, #1773 y #1804 (release v1.6.2-atari-fsk-beta.1). Le
costó un mes que funcionara. Ese código reproduce los bloques `fsk ` con el RMT del ESP32, los carga de a poco desde
la tarjeta y se detiene con el MOTOR. ADR-012 enmienda 1 permite adaptarlo. **Ese trabajo no se pierde: es la base
de esta spec.**

## Por qué no Altirra (evidencia C003)

La versión anterior de esta spec cargaba las cintas en Altirra por NetSIO y trataba `fsk ` como silencio. La prueba
de viabilidad C003 (2026-11-20, evidencia `D:\rb-e0\m49-cas\c003`) falló con `international_karate.cas`:

- MOTOR ON llegó y los registros a 600 baudios salieron;
- en el tramo a 750 baudios el arranque falló: `BOOT ERROR` y `SELF TEST`;
- MOTOR OFF nunca llegó.

NetSIO lleva bytes a una velocidad, no una señal. Por NetSIO no se puede reproducir un bloque `fsk `. Por eso esta
spec va al ESP32.

## Cost of not building it

- Los juegos que solo existen en cinta quedan fuera de RetroBridge.
- El trabajo FSK del responsable queda solo en FujiNet.
- Para usar esas cintas hay que volver a FujiNet.

## The cheapest alternative

- **Seguir usando FujiNet** para las cintas. Funciona hoy, pero fuera de RetroBridge.
- **Convertir la cinta a `.xex` o `.ATR`.** A mano, archivo por archivo. No sirve con el turbo: el cargador turbo es
  parte del programa.
- **No construirlo.**

Ninguna carga un `.cas` real con RetroBridge, así que se construye.

## Objeción registrada

La parte que de verdad carga una cinta (señal en SIO DATA IN, MOTOR por GPIO36) **no se puede probar** hasta que el
responsable apruebe M3a (medición del MOTOR) y H4 (pines SIO como salida). Hasta entonces solo avanza lo portable,
con pruebas del host. El responsable lo aceptó (2026-11-20): «Medir M3a primero», «Cadena completa por fases».

## Goals

- G1. Un `.cas` real de la tarjeta SD se monta en la casetera del ESP32.
- G2. Con la cinta montada, el Atari la carga como con una casetera real: arranque con START, o `CLOAD` / `RUN"C:"`.
- G3. Los bloques `fsk ` salen como la señal que describe el archivo. Los bloques `data` salen como bytes a su
  `baud`, tras su pausa.
- G4. La cinta avanza solo con el MOTOR encendido y se detiene sin cortar la señal a medias.
- G5. Una cinta que no se puede reproducir da un mensaje claro al montarla.

## Non-goals / out of scope

- Grabar en cinta.
- Rebobinar por segundos (PR #1722, `cassetteTrail`). Spec propia.
- Elegir la cinta desde el menú del Atari. El menú vive hoy en `retrobridge_host` (PC). Spec propia cuando el menú
  llegue al ESP32.
- Cargar cintas en Altirra o por NetSIO (ver C003).
- Bloques `pwm*`: ninguna cinta real de la colección los trae.
- Inventar cintas de prueba. Las demos usan solo `.cas` reales.
- Cambiar el comportamiento de `.ATR` y `.xex`.

## Users & context

El responsable, con un Atari real conectado al ESP32 (FN32ROV-1.3) por SIO, la tarjeta SD con su colección y una
consola serie al ESP32. Hardware: MOTOR (pin 8) llega directo a GPIO36 (R1); DATA IN es un pin SIO de salida.

## What the user can do

- Montar un `.cas` de la SD desde la consola del ESP32: `cas mount <ruta>`.
- Ver la cinta montada y dónde va: `cas status`.
- Expulsarla: `cas eject`. Montar otra la reemplaza y queda al principio.
- Cargarla en el Atari: reinicio en frío con START (y una tecla tras el pitido), o `CLOAD` / `RUN"C:"`.
- Reintentar una carga fallida con otro reinicio en frío o `cas rewind`: la cinta vuelve al principio.

Condición: M3a y H4 aprobados para cargar en el Atari. Antes, montar y `cas status` funcionan sin tocar los pines
SIO.

## What result they get

1. `cas mount` responde con el nombre y el número de bloques `data` y `fsk `.
2. Tras cargar, el programa corre. Referencias aprobadas (cintas reales de `D:\GitHub\Respaldos\MicroSD`):
   - estándar: `international_karate.cas`, Night Knight;
   - turbo software con `data`: Alien Ambush (el Atari muestra `PROGRAMA CARGADO`), Bruce Lee;
   - turbo software solo `fsk `: un `turbo_software_*` o `tt_*`, elegido por el responsable.
3. Un archivo sin cabecera `FUJI`, con un bloque cortado o con `pwm*`: mensaje claro; la cinta montada no cambia.

## What it must respect

- **ADR-012 enmienda 1**: se adapta solo el código propio del responsable de la cuarentena `D:\rb-e0\fsk-propio`
  (PRs #1722, #1756, #1773, #1804). Se adapta, no se pega. Cada pieza registra su commit de origen en
  `docs/fuentes.md`. Ningún otro código de FujiNet ni de emuladores. Las dependencias de FujiNet (`bus.h`,
  `fnio.h`, `pinmap.h`, clases `cassette`/`disk`) se reescriben.
- **El comportamiento ya probado** del código propio: la regla de unión (bloques `fsk ` sin pausa forman una sola
  tirada, como máximo 64), la carga progresiva (bloques de 512 bytes, 20 s de margen antes de empezar) y la parada
  segura al caer el MOTOR.
- **ADR-011**: sin reservas de memoria en el camino de reproducción.
- **Puertas de hardware**: ningún pin SIO es salida sin H4 aprobado; el MOTOR no se usa sin M3a aprobado
  (`docs/hardware/mitigacion-c-motor-pin8.md` sigue siendo propuesta).
- Capas: `check_layering` y `check_notices` en verde. C++17, `-fno-exceptions -fno-rtti -Werror`.
- `.ATR`, `.xex` y el menú del host siguen igual.

## How it proves it

- **Fase 1, portable (sin hardware)**: pruebas del host (`ctest`) para la lectura del `.cas`, la regla de unión, la
  carga progresiva y el reproductor con MOTOR. Además, una prueba que lee los `.cas` reales desde
  `D:\GitHub\Respaldos\MicroSD` (ruta fuera del repo) y se salta si no están.
- **Fase 2, ESP32 tras M3a**: medición del MOTOR en GPIO36 con el Atari, con evidencia.
- **Fase 3, ESP32 tras H4**: demo en el Atari real con las cintas de referencia. Evidencia en `D:\rb-e0\m49-cas`
  (registro de la consola, versión, fotos o capturas).
- Registro de verificación en `02-DOCS/wiki/sdd/verifications/`.

## Behaviour

- **Leer.** Un `.cas` se recorre en orden: `baud` fija la velocidad (600 si no hay); `data` es un registro de bytes
  tras su pausa; `fsk ` es una tira de duraciones de nivel (1/10 ms, alternando 0 y 1) tras su pausa.
- **Unir.** Bloques `fsk ` seguidos, el segundo y siguientes sin pausa, forman una sola tirada (como máximo 64).
- **Reproducir.** Con MOTOR encendido: cada pausa es silencio; cada `data` sale como bytes a su `baud`; cada tirada
  `fsk ` sale como señal, con sus duraciones exactas.
- **Cargar de a poco.** Una tirada larga no se carga entera en memoria. Se lee de la SD en bloques; la señal empieza
  cuando hay 20 s de margen leídos.
- **MOTOR.** Al apagarse, la cinta se detiene. Una tirada `fsk ` se corta en un punto seguro, no a mitad de un nivel.
  Al volver a encenderse, sigue desde ahí.
- **Fin de cinta.** Tras el último bloque la cinta queda al final; un nuevo MOTOR no envía nada.
- **Reinicio en frío o `cas rewind`.** La cinta sigue montada y vuelve al principio.
- **Fallos.** Sin `FUJI`, bloque cortado, `baud` 0 o `pwm*` → mensaje en la consola; la cinta montada no cambia.

## Acceptance criteria

Fase 1 — portable, prueba del host:

- **AC1** — Dado `international_karate.cas` real, cuando se lee, entonces salen sus bloques en orden: los `data` con
  su `baud` y su pausa, y los `fsk ` con su pausa y sus duraciones (no silencio).
- **AC2** — Dados bloques `fsk ` seguidos sin pausa, cuando se leen, entonces forman una sola tirada; un bloque con
  pausa, o el número 65, empieza otra.
- **AC3** — Dada una tirada `fsk `, cuando se reproduce, entonces la señal tiene las mismas duraciones y niveles
  alternos que el archivo.
- **AC4** — Dada una tirada más larga que la memoria de carga, cuando se reproduce, entonces se lee en bloques y la
  señal no empieza antes de tener 20 s de margen.
- **AC5** — Dada una cinta montada, cuando el MOTOR se apaga a mitad de un `data` o de una tirada `fsk ` y vuelve,
  entonces la cinta sigue desde un punto seguro, sin perder ni repetir datos.
- **AC6** — Dado un archivo sin `FUJI`, con un bloque cortado, con `baud` 0 o con `pwm*`, cuando se monta, entonces
  da su mensaje y la cinta montada no cambia.
- **AC7** — Todos los `.cas` reales de `D:\GitHub\Respaldos\MicroSD` se leen sin error (prueba que se salta sin la
  carpeta).
- **AC8** — La consola del ESP32 acepta `cas mount`, `cas status`, `cas eject` y `cas rewind` (prueba del host sobre
  el componente de consola).

Fase 2 — ESP32, tras M3a:

- **AC9** — Con el Atari encendido, `cas status` muestra el MOTOR tal como lo mide el responsable en M3a (encendido
  con `CLOAD`, apagado al terminar).

Fase 3 — ESP32, tras H4 (demo en el Atari real):

- **AC10** — `international_karate.cas` carga con START y corre.
- **AC11** — Alien Ambush carga y el Atari muestra `PROGRAMA CARGADO`.
- **AC12** — Una cinta turbo software solo `fsk ` carga y corre.

Siempre:

- **AC13** — `ctest`, `tools/tests`, `check_layering` y `check_notices` siguen en verde; `.ATR` y `.xex` funcionan
  igual.

## Enfoques considerados

1. **Casetera en el ESP32 con el código FSK propio** (recomendado y elegido). Es lo único probado con cintas reales.
2. **Casetera en el host por NetSIO.** Descartado: C003 mostró que NetSIO no lleva la señal `fsk `.
3. **Convertir la cinta a disco.** Descartado: no sirve para el turbo.

## Points to clarify

- **suposición tomada** — la cinta se elige desde la consola del ESP32 (`cas mount <ruta>`), no desde el menú.
  *Base:* el menú vive hoy solo en el host del PC; la consola (`rb_console`) ya tiene `mount` y `ls` para discos.
  *Riesgo:* si el responsable quiere el menú, esa parte espera a una spec de menú en el ESP32.
- **suposición tomada** — las pruebas de formato usan fragmentos mínimos de bytes (cabecera, bloque cortado); las
  cintas completas son siempre las reales, leídas desde su carpeta y nunca copiadas al repo. *Base:* «no hay que
  inventar cintas». *Riesgo:* ninguno.
- **suposición tomada** — de C001 se conserva la lectura A8CAS, sin la regla «`fsk ` = silencio»; de C002 se conserva
  la lógica de MOTOR, pausa y `baud` para `data`, con otra salida (UART del ESP32 en lugar de NetSIO). *Base:* el
  código y sus pruebas ya pasan revisión. *Riesgo:* el plan decide qué queda del enlace NetSIO.
- **suposición tomada** — los cambios de C003 sin commit (`cassette_run.hpp`, opción `--cassette`) no se suben; la
  evidencia queda en `D:\rb-e0\m49-cas\c003` y en el progreso. *Base:* el camino Altirra se descartó.
- **suposición tomada** — los requisitos siguen numerados M49-n.
- **decisión diferida** — rebobinar por segundos (PR #1722): spec propia.
- **decisión diferida** — menú del Atari para cintas en el ESP32: spec propia.
- **área no formulable** — si la mitigación C (quitar el pin 8) cambia cómo se lee el MOTOR: depende de M3a.

## Clarifications

- 2026-11-20 — ¿Plataforma? → Altirra primero (luego revertido, ver abajo).
- 2026-11-20 — ¿Alcance? → solo cargar → grabar no es necesario ahora.
- 2026-11-20 — ¿Pausas? → tal cual → algunos juegos las necesitan.
- 2026-11-20 — ¿Tras un reinicio en frío? → la cinta sigue montada, al principio.
- 2026-11-20 (replanteo) — C003 falló en Altirra. «No hay que inventar cintas. Se tienen que ocupar los .cas reales…
  el código fuente que aporté a fujinet nos demoramos 1 mes a que funcione. Ese trabajo no debe perderse.» → se
  replantea hacia el ESP32 con el código FSK propio.
- 2026-11-20 (replanteo) — ¿MOTOR? → «Medir M3a primero» → las tareas de hardware esperan M3a.
- 2026-11-20 (replanteo) — ¿Alcance? → «Cadena completa por fases» → portable primero, ESP32 tras M3a y H4.
- 2026-11-20 (replanteo) — ¿Dónde? → «Esta rama» → `feat/cassette-cas`.

## Revisions

- 2026-11-20 — borrador (specify), nivel 1 en Altirra.
- 2026-11-20 — clarify: 3 preguntas resueltas. Aprobada.
- 2026-11-20 — C003 falló. Replanteo: ESP32, código FSK propio, cintas reales, fases tras M3a y H4. Vuelve a
  borrador.
- 2026-11-20 — aprobada por el responsable («Aprobada, a plan»).
