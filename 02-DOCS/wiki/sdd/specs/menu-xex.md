---
type: spec
title: Spec — Arrancar archivos XEX desde el menú
description: QUÉ y POR QUÉ de que el menú de RetroBridge muestre los archivos .xex de la carpeta local y de TNFS, y los arranque desde D1.
tags: [sdd, spec, menu, xex, 6502]
timestamp: 2026-11-20T08:00:00Z
topic: sdd
slug: menu-xex
status: approved
---

# Spec — Arrancar archivos XEX desde el menú

## Problem & why

El menú solo muestra carpetas y archivos `.ATR`, en la carpeta local (`--images`) y en TNFS. Muchos juegos y
programas del Atari se distribuyen como `.xex`: un programa en bloques, no un disco. Hoy no aparecen en la lista y
no se pueden arrancar. La spec `menu-tnfs` lo dejó como suposición, con este riesgo: «quien busque un `.xex` no lo
verá hasta otra etapa».

Disparador: el responsable lo notó el 2026-11-20 al usar el menú con el servidor `tnfs.calaluis.cl`, que mezcla
`.atr` con `.xex`.

## Cost of not building it

- Los `.xex` de los servidores TNFS quedan invisibles: una parte de la colección no se puede usar.
- Para usar un `.xex` hay que convertirlo a `.ATR` en el PC, archivo por archivo, y copiarlo a la carpeta local.
- Con el ESP32 (sin PC), no habría ninguna forma de usarlos.

## The cheapest alternative

- **Convertir en el PC.** Una herramienta externa empaqueta el `.xex` en una `.ATR` con un cargador. Funciona para la
  carpeta local, pero no para TNFS, y exige un paso manual por archivo. Además, ese cargador es código de terceros.
- **Solo mostrarlos.** Listar los `.xex` sin poder arrancarlos no resuelve nada.
- **No construirlo.** El menú sigue con solo `.ATR`.

Ninguna permite arrancar un `.xex` de TNFS desde el Atari, así que se construye.

## Objeción registrada

Un `.xex` no trae su propio cargador: RetroBridge debe escribir uno nuevo, en 6502, propio (ADR-012). Ningún
cargador funciona con el 100 % de los `.xex`: algunos programas pisan la memoria del cargador, otros esperan un DOS o
un entorno distinto. Habrá `.xex` que no arranquen. El responsable decidió seguir (2026-11-20); la spec dice qué
pasa en esos casos.

## Goals

- G1. El menú muestra los `.xex` en la carpeta local y en TNFS, junto a las `.ATR`.
- G2. Un `.xex` elegido queda en D1, con su nombre, y se arranca con `A`, igual que una `.ATR`.
- G3. Al arrancar, el Atari carga el programa y lo ejecuta, con el BASIC apagado.
- G4. Un `.xex` que no se puede cargar da un mensaje claro, no un Atari colgado sin aviso cuando se puede evitar.

## Non-goals / out of scope

- `.xex` en D2..D8: no son discos.
- Convertir `.xex` a `.ATR` o guardarlos en la carpeta.
- Otros formatos: `.com`, `.exe`, `.car`/`.rom` (cartuchos), `.cas` (casete), `.bas`.
- Un DOS para el programa: el `.xex` corre sin DOS.
- Programas que leen más archivos del disco después de cargar (necesitan un DOS).
- Firmware ESP32 (H4): el soporte vive en el host y el programa 6502; el ESP32 lo heredará después.
- Cambiar el comportamiento de las `.ATR`.

## Users & context

El usuario del Atari (800XL o Altirra) frente al menú de RetroBridge, servido por `retrobridge_host` en D1. Navega
la carpeta local o un servidor TNFS con juegos en `.atr` y `.xex`.

## What the user can do

- Ver los `.xex` en la lista de una fuente (local o TNFS), mezclados con las `.ATR` y las carpetas.
- Elegir un `.xex` y asignarlo a D1 (paso de unidad: `1` o el cursor sobre D1).
- Arrancarlo con `A` en la pantalla principal.
- Volver al menú con un reinicio en frío, como hoy.

Condición: el host corre con `--images`. No hay permisos: cualquier usuario del Atari puede hacerlo.

## What result they get

1. La lista muestra el `.xex` con su nombre. Referencia: igual que una `.ATR` hoy, con la extensión `.XEX` visible.
2. Tras elegirlo para D1, la línea de D1 en la pantalla principal muestra su nombre.
3. Tras `A`, el programa corre. Referencia aprobada: un `.xex` de prueba propio (texto en pantalla) y un juego `.xex`
   de `tnfs.calaluis.cl` que el responsable elige en la demo; su ruta queda en el registro.
4. Si se elige D2..D8 para un `.xex`: mensaje ` UN XEX SOLO VA EN D1` y nada cambia.
5. Si el archivo no es un `.xex` válido: mensaje ` XEX NO VALIDO` al elegirlo y nada cambia.
6. Si un segmento pisa la memoria del cargador: mensaje ` XEX NO COMPATIBLE` al elegirlo y nada cambia.

## What it must respect

- ADR-017 (menú de arranque) y sus enmiendas: terminal delgado; el host arma las pantallas.
- ADR-012: el cargador es código 6502 propio. Fuente pública del formato (por ejemplo, el manual del DOS 2.0 o el
  *Altirra Hardware Reference Manual*), registrada en `docs/fuentes.md`. Sin leer código de FujiNet ni de emuladores.
- ADR-018 (cliente TNFS): un `.xex` de TNFS se descarga como hoy una `.ATR`.
- Las `.ATR` siguen igual: lista, asignación a D1..D8, arranque, escritura y formateo.
- D1 vuelve al menú tras cada reinicio en frío, como hoy.
- Un solo `retrobridge_host` por hub; en Altirra, las unidades servidas en Off.

## How it proves it

- Pruebas del host (`ctest`): el catálogo local y TNFS listan `.xex`; elegir un `.xex` para D2..D8 da el mensaje;
  un archivo que no es `.xex` válido da el mensaje; el disco que se sirve en D1 lleva el cargador y el programa.
- Pruebas del cargador en el intérprete 6502 de `tools/tests`: carga los segmentos en sus direcciones, ejecuta
  `INITAD` y `RUNAD`, apaga el BASIC.
- Demo en Altirra con un solo host: un `.xex` local de prueba y un juego `.xex` de TNFS arrancan. Evidencia en
  `D:\rb-e0\m48-xex` (registro, versión, capturas).
- Registro de verificación en `02-DOCS/wiki/sdd/verifications/`.

## Behaviour

- **Lista.** Las fuentes muestran carpetas, `.ATR` y `.XEX` (sin distinguir mayúsculas), en orden alfabético como
  hoy. Un `.xex` no lleva marca propia: se reconoce por la extensión visible.
- **Elegir.** Elegir un `.xex` pide la unidad como hoy. `1` (o el cursor en D1) lo asigna a D1. `2`..`8` muestran
  ` UN XEX SOLO VA EN D1`.
- **Arrancar.** `A` con un `.xex` en D1: el host sirve en D1 un disco que lleva el cargador y el programa. El
  cargador apaga el BASIC, carga cada segmento en su dirección, ejecuta las rutinas `INITAD` cuando aparecen, y al
  final salta a `RUNAD`.
- **Fallos.**
  - Archivo sin la cabecera `$FFFF` o con un segmento roto: ` XEX NO VALIDO` al elegirlo; no se asigna.
  - Un segmento que pisa la memoria del cargador: el host lo detecta al elegirlo y muestra ` XEX NO COMPATIBLE`;
    no se asigna.
  - Un error de lectura durante la carga: el cargador muestra un aviso en pantalla y se detiene.
- **Volver.** Un reinicio en frío vuelve al menú, como hoy.

## Acceptance criteria

- **AC1** — Dada una carpeta local con `JUEGO.XEX`, cuando se abre la fuente 1, entonces `JUEGO.XEX` aparece en la
  lista.
- **AC2** — Dada una carpeta TNFS con `.xex`, cuando se abre, entonces los `.xex` aparecen en la lista.
- **AC3** — Dado un `.xex` elegido, cuando se elige `1`, entonces D1 muestra su nombre en la pantalla principal.
- **AC4** — Dado un `.xex` elegido, cuando se elige `2`..`8`, entonces se ve ` UN XEX SOLO VA EN D1` y ninguna unidad
  cambia.
- **AC5** — Dado un `.xex` válido en D1, cuando se pulsa `A`, entonces el programa se carga y corre (demo: un `.xex`
  local de prueba y un juego de TNFS).
- **AC6** — Dado un `.xex` con `INITAD`, cuando carga, entonces la rutina `INITAD` corre antes del segmento
  siguiente (prueba del intérprete).
- **AC7** — Dado un arranque de `.xex`, cuando el cargador empieza, entonces el BASIC queda apagado (prueba del
  intérprete).
- **AC8** — Dado un archivo `.xex` sin cabecera `$FFFF`, cuando se elige, entonces se ve ` XEX NO VALIDO` y D1 no
  cambia.
- **AC9** — Dado un `.xex` con un segmento sobre la memoria del cargador, cuando se elige, entonces se ve
  ` XEX NO COMPATIBLE` y D1 no cambia.
- **AC10** — Las pruebas de hoy (`ctest`, `tools/tests`) siguen en verde: las `.ATR` funcionan igual.

## Enfoques considerados

1. **Disco virtual con cargador propio** (recomendado). El host sirve en D1 un disco hecho al vuelo: sectores de
   arranque con el cargador y, detrás, el `.xex`. Usa el SIO de disco que ya existe; el ESP32 podrá hacer lo mismo.
2. **Cargador en el menú.** El programa del menú carga el `.xex` con una orden SIO nueva al dispositivo `$5A`. Evita
   un disco virtual, pero el menú ocupa memoria que el programa puede necesitar y el protocolo crece.
3. **Convertir a `.ATR` en el PC.** Ya descartado en *The cheapest alternative*.

## Points to clarify

- **suposición tomada** — los requisitos se numeran M48-n. *Base:* la última spec usó M47. *Riesgo:* ninguno.
- **suposición tomada** — el tamaño máximo de un `.xex` de TNFS es el mismo que el de una `.ATR` descargada hoy.
  *Base:* se descarga con el mismo mecanismo. *Riesgo:* un `.xex` muy grande no carga; plan fija el límite.
- **suposición tomada** — el tamaño máximo de un `.xex` local es el que cabe en un disco virtual de 65535 sectores.
  *Base:* el límite de sectores de SIO. *Riesgo:* ninguno práctico.
- **decisión diferida** — `.com`, `.exe`, cartuchos y casete: specs propias.
- **área no formulable** — compatibilidad con `.xex` que usan memoria baja o esperan un DOS: se medirá en la demo.

## Clarifications

- 2026-11-20 — ¿Dónde se ven? → carpeta local y TNFS → los juegos `.xex` del responsable están en las dos.
- 2026-11-20 — ¿Qué pasa al elegir? → va a D1 y se arranca con `A`, como una `.ATR`; D2..D8 no aceptan `.xex` → un
  solo flujo para todo, y un `.xex` no es un disco.
- 2026-11-20 — ¿BASIC? → el cargador lo apaga siempre → es lo que piden casi todos los juegos; no hay que mantener
  OPTION.
- 2026-11-20 — ¿Dónde se trabaja? → rama `docs/spec-menu-xex` desde main, PR solo con la spec.

- 2026-11-20 (clarify) — ¿Juego de referencia para AC5? → lo elige el responsable en la demo; su ruta queda en el
  registro → conoce su colección mejor que una lista fija.
- 2026-11-20 (clarify) — ¿La lista marca los `.xex`? → no: basta la extensión visible → sin cambio de pantalla y sin
  perder una columna del nombre.
- 2026-11-20 (clarify) — ¿Cuándo se avisa ` XEX NO COMPATIBLE`? → al elegirlo, como ` XEX NO VALIDO` → un `.xex` que no
  arrancará nunca llega a D1 (AC9 cambia).

## Revisions

- 2026-11-20 — borrador (specify).
- 2026-11-20 — clarify: 3 preguntas resueltas, AC9 cambia (aviso al elegir).
- 2026-11-20 — aprobada por el responsable.
