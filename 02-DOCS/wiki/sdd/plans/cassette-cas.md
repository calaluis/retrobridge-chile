---
type: plan
title: Plan — Cargar cintas .cas (nivel 1) desde el menú
description: CÓMO se construye la spec cassette-cas: leer .cas A8CAS, una casetera virtual en el host que responde a MOTOR por NetSIO, y su integración en el menú (lista, C:, U, reinicio).
tags: [sdd, plan, menu, cassette, cas, netsio, host]
timestamp: 2026-11-20T21:00:00Z
topic: sdd
slug: cassette-cas
spec: 02-DOCS/wiki/sdd/specs/cassette-cas.md
branch: feat/cassette-cas
status: approved
---

# Plan — Cargar cintas .cas (nivel 1) desde el menú

## 0. Global constraints (literales, para cada tarea)

- C++17 con `-fno-exceptions -fno-rtti -Werror` (ADR-004). Sin reservas en la ruta SIO (ADR-011): la cinta se lee y
  se indexa al elegirla, fuera de la ruta SIO, como hoy una `.ATR` o un `.xex` de TNFS.
- ADR-012 y su **enmienda 1**: se puede adaptar el lector A8CAS propio del responsable (cuarentena
  `D:\rb-e0\fsk-propio`, `nuevos/pr1756/lib/media/atari/casFSK.*` y `nuevos/pr1773/lib/media/atari/casFSKLoader.*`).
  Se adapta, no se pega: sin `global_types.h`, `fnio.h`, `debug.h`, `bus.h`, `pinmap.h` ni clases de FujiNet. Cada
  tarea que use ese código registra en `docs/fuentes.md` el PR y commit de origen. Ningún otro código de FujiNet ni
  de emuladores.
- El formato A8CAS se describe con una fuente pública (documentación del proyecto A8CAS, formato `.cas`), registrada
  en `docs/fuentes.md` en C001 antes de escribir el lector.
- `check_layering` en verde (ADR-002): el formato `.cas` y la casetera viven en el host (`host/src`), como `.xex`. El
  transporte NetSIO (`components/rb_netsio`) solo gana lo mínimo: avisar MOTOR y enviar velocidad y datos fuera de
  una trama.
- `.ATR` y `.xex` no cambian (AC10). Un solo `retrobridge_host` por hub; en Altirra, D1..D8 en Off y la casetera
  propia de Altirra sin cinta.
- Los requisitos se numeran M49-n. Evidencia de demos en `D:\rb-e0\m49-cas` (no versionada).

## 1. Context & constraints

- Spec: 12 criterios de aceptación; 8 puntos tipados. AC1 es una **puerta**: si falla, se detiene el resto.
- Hoy (main `b3183dc`):
  - `NetSioSioTransport::onMessage` (`components/rb_netsio/src/netsio_sio_transport.cpp`) guarda `kSpeedChange` en
    `stats_.lastBaud` y cuenta `kMotorOn/Off` como `ignoredMessages`. `send()` sin sync pendiente ya emite un
    `DATA BLOCK` (`02`) de 1..512 bytes.
  - El bucle `serve()` de `host/src/main.cpp` llama a `afterStep()` tras cada `session.step()`, también en Idle.
  - El menú: `MenuController` (`host/src/menu_controller.cpp`) tiene `chooseXex`, `startJob(Job::Xex, …)`,
    `View::Unmount` (`onUnmount`, 1..8) y dibuja `C: SIN CINTA` en `kMainTape` (línea 839).
  - El catálogo local filtra por extensión (`menu_catalog.cpp`, `file_identity.cpp`); TNFS en `tnfs_folder.cpp`.
- Formato A8CAS (fuente pública, C001): bloques de cabecera de 8 bytes (nombre de 4, longitud de 2 LE, aux de 2 LE).
  `FUJI` abre el archivo; `baud` fija la velocidad (aux); `data` lleva bytes y su aux es la pausa previa en ms;
  `fsk ` lleva duraciones de pulsos. Muestras analizadas en `D:\GitHub\Respaldos\MicroSD` (ver spec).

## 2. Architecture

```text
 .cas (local o TNFS) ──► CasImage::index()  ──►  CasTape (bloques: velocidad, pausa, bytes | silencio)
                              │ errores: NoValido / Pulsos
                              ▼
 MenuController ── elegir .cas ──► CassetteDeck (C:) ◄── reinicio en frío: rebobinar
        │  U → C:: expulsar               │
        ▼                                 ▼ tick(now) en afterStep()
   pantalla C: <nombre>            CassettePlayer ──► TapeLink (puerto) ──► NetSioSioTransport
                                     MOTOR on/off ◄──────────── kMotorOn/Off
                                     velocidad ──────────────► kSpeedChange (saliente)
                                     bytes, a ritmo de baud ──► DATA BLOCK fuera de trama
```

- **CasImage** (host, puro): valida e indexa un `.cas` en una lista de bloques reproducibles. No toca E/S.
- **CassetteDeck** (host): la cinta montada en `C:` (nombre, `CasTape`, posición). Montar, expulsar, rebobinar.
- **CassettePlayer** (host, puro con reloj inyectado): máquina de estados que, con MOTOR encendido, avanza la
  posición según el tiempo: espera la pausa, envía la velocidad si cambia, y entrega bytes a ritmo de baud
  (10 bits por byte). Con MOTOR apagado, se congela.
- **TapeLink** (puerto en el host, implementado sobre `NetSioSioTransport`): `motorOn()`, `setBaud(b)`,
  `sendTapeBytes(span)`. Las pruebas usan un falso.
- **Decisión principal: el host marca el ritmo**, no Altirra. El host envía los bytes en trozos pequeños cuando su
  reloj dice que ya pasaron (como una cinta real que gira), en vez de volcar el bloque entero. *Por qué:* MOTOR
  puede apagarse a mitad de un bloque (AC7) y la posición debe quedar donde iba; y el crédito NetSIO (`C6/C7`)
  limita lo que se puede enviar por adelantado. *Alternativa:* volcar cada bloque y dejar que Altirra lo ritme —
  más simple, pero pierde AC7 y puede chocar con el crédito. Se valida en C003.

## 3. Interfaces & contracts

```text
CasImage::index(bytes) -> CasTape | CasError::NoValido | CasError::Pulsos
  - NoValido: sin "FUJI" al inicio, o un bloque cuya longitud pasa el final del archivo
  - Pulsos: ningún bloque "data" y al menos un "fsk " con datos (tipo B)
  - "baud" sin datos fija la velocidad de los bloques siguientes; 600 si no hay ninguno
  - "fsk " en una cinta de nivel 1 -> bloque de silencio con la suma de sus duraciones
  - bloques desconocidos se saltan (A8CAS permite extensiones)

CassettePlayer::tick(nowUs, link)          -- llamado en cada vuelta del bucle
  - MOTOR apagado: no envía nada; la posición no se mueve
  - MOTOR encendido: pausa -> (setBaud si cambia) -> bytes según el tiempo transcurrido
  - fin de cinta: no envía nada más
CassettePlayer::onMotor(on, nowUs)        -- desde el transporte
CassetteDeck::mount(name, tape) / eject() / rewind() / mounted() -> nombre | vacío

TapeLink (puerto): motorOn() -> bool ; setBaud(uint32) ; sendTapeBytes(span) -> Result
```

El transporte gana: un aviso de MOTOR (estado consultable), `kSpeedChange` saliente y el envío de datos fuera de una
trama cuando no hay sync pendiente (ya existe; se documenta como uso de cinta).

## 4. Data model & flow

- La cinta se lee entera al elegirla (local: archivo; TNFS: descarga con el mecanismo y el límite de hoy) y se indexa
  en `CasTape` (bloques con desplazamiento al búfer, longitud, pausa, velocidad). El búfer vive mientras esté
  montada; se libera al expulsar o reemplazar, fuera de la ruta SIO.
- Estado de `C:`: vacía | montada(nombre, posición). Reinicio en frío (`kColdReset`, ya contado en el transporte):
  posición = 0, sigue montada (AC12).
- Registro JSONL: `cas_mount`, `cas_eject`, `cas_motor` (on/off, posición), `cas_end`, y el rechazo con su motivo.

## 5. Testing strategy

| AC | Nivel | Qué afirma | Qué se finge |
| --- | --- | --- | --- |
| AC1 | demo (puerta) | un `.cas` estándar pequeño carga en Altirra por NetSIO | nada |
| AC2 | unidad (`ctest`) | el catálogo local y TNFS listan `.CAS` | carpeta y TNFS falsos, como `.xex` |
| AC3 | unidad | elegir un `.cas` válido pone su nombre en la línea `C:` | cargador falso |
| AC4, AC5 | demo | estándar y turbo tipo A cargan y corren | nada |
| AC6 | unidad | bloques a 600 y 800 salen a su velocidad y tras su pausa | reloj y `TapeLink` falsos |
| AC7 | unidad | MOTOR off a mitad de bloque congela; on sigue desde ahí | reloj y `TapeLink` falsos |
| AC8, AC9 | unidad | tipo B y archivo roto dan su mensaje; `C:` no cambia | bytes en memoria |
| AC10 | suite | `ctest` y `tools/tests` siguen en verde | — |
| AC11 | unidad | `U` llega a `C:` y la deja en `C: SIN CINTA` | — |
| AC12 | unidad | reinicio en frío: `C:` conserva el nombre y la posición vuelve a 0 | transporte falso |

`CasImage` además se prueba con las muestras reales copiadas a fixtures pequeños propios (cabeceras generadas, no
archivos de terceros).

## 6. Sequencing & dependencies

1. **C001** — fuente A8CAS en `fuentes.md`; `CasImage` + pruebas (AC8, AC9, partes de AC6).
2. **C002** — `CassettePlayer`, `CassetteDeck`, `TapeLink` y los cambios mínimos del transporte; pruebas con reloj
   falso (AC6, AC7, AC12 a nivel de deck).
3. **C003** — **puerta de viabilidad (AC1)**: opción de host `--cassette <archivo.cas>` que monta la cinta sin menú;
   demo en Altirra con un `.cas` estándar pequeño. Observar el orden de arranque cinta/disco con D1 sirviendo el
   menú. **Si no carga: parar, registrar y volver al responsable.**
4. **C004** — menú: `.CAS` en la lista (local y TNFS), elegir monta en `C:` sin pedir unidad, mensajes, línea `C:`
   (AC2, AC3, AC8, AC9).
5. **C005** — `U` llega a `C:` (AC11); reinicio en frío rebobina (AC12); si C003 lo pidió, D1 calla durante el
   arranque con cinta.
6. **C006** — demo final (AC4, AC5) y documentación: ADR-017 enmienda 12, `host/README.md`, `fuentes.md`.
7. Cierre: `verify` y `review` de la rama.

C001 y C002 son independientes entre sí salvo el tipo `CasTape` (C001 lo define primero).

## 7. Risks & open decisions

1. **Altirra no entrega bytes de cinta por NetSIO, o no con el tiempo que el SO mide** (alto). *Disparador:* C003
   no carga. *Mitigación:* la puerta C003 antes de tocar el menú; se prueban los dos ritmos (trozos con reloj, y
   bloque entero) antes de rendirse.
2. **Crédito NetSIO (`C6/C7`) frena el envío** (medio). *Mitigación:* trozos pequeños al ritmo de baud; C003 mira
   los créditos en el registro.
3. **El menú en D1 gana el arranque a la cinta** (medio). *Disparador:* con START, el Atari arranca el menú. *Mitigación:*
   C003 lo observa; C005 hace callar D1 durante ese arranque solo si hace falta.
4. **Cargadores turbo tipo A sensibles al ritmo** (medio). *Mitigación:* demo con dos cintas turbo; si una falla, se
   registra como límite conocido, no bloquea AC4.
5. **Reutilización del código propio contaminada por dependencias de FujiNet** (bajo). *Mitigación:* reescribir la
   E/S; `check_notices` y `check_layering`; fila en `fuentes.md` por tarea.

Decisiones abiertas: ninguna que bloquee el inicio; el ritmo final (trozos o bloque) lo fija C003.

## Tasks

Rama `feat/cassette-cas` (desde main `b3183dc`). Riesgo previsto por tarea (0/1/2); `rsc risk` decide el real.

| # | [P] | Tarea | Done-check | Depende de | Cubre | Riesgo |
|---|---|---|---|---|---|---|
| C001 | [P] | Fuente pública A8CAS y fila de origen (enmienda 1) en `docs/fuentes.md`; `cas_image.{hpp,cpp}` (`CasImage::index` → `CasTape` o error) | `ctest test_cas_image`: `FUJI` ausente, bloque cortado, tipo B, `baud` 600/800, pausa de `data`, `fsk ` como silencio, bloque desconocido saltado | — | AC6 (índice), AC8, AC9 | 1 |
| C002 |  | `cassette_player`, `cassette_deck`, puerto `TapeLink`; transporte: aviso de MOTOR, `kSpeedChange` saliente, datos fuera de trama | `ctest test_cassette_player`: pausa, velocidad, ritmo de 10 bits por byte, MOTOR off congela y on sigue, fin de cinta, rebobinar; `ctest` de NetSIO en verde | C001 (`CasTape`) | AC6, AC7, AC12 (deck) | 2 |
| C003 |  | **Puerta de viabilidad**: opción `--cassette <archivo.cas>`; demo en Altirra con un `.cas` estándar pequeño; observar el orden de arranque cinta/disco y los créditos | carpeta `D:\rb-e0\m49-cas\c003`: `host.jsonl` con `cas_motor` y bytes enviados, `version.txt`, captura del programa corriendo. **Si no corre: parar y volver al responsable** | C002 | AC1 | 1 |
| C004 |  | Menú: `.CAS` en catálogo local, escaneo de carpeta y TNFS; elegir monta en `C:` sin pedir unidad; mensajes `CAS DE PULSOS: AUN NO` y `CAS NO VALIDO`; línea `C:` con el nombre | `ctest` de catálogo, TNFS y menú: AC2, AC3, AC8, AC9; pruebas de hoy en verde | C003 | AC2, AC3, AC8–AC10 | 2 |
| C005 |  | `U` recorre D1..D8 y `C:`; reinicio en frío rebobina y conserva `C:`; D1 calla en el arranque con cinta solo si C003 lo pidió | `ctest` del menú: AC11, AC12; pruebas de hoy en verde | C004 | AC11, AC12, AC10 | 1 |
| C006 |  | Demo final en Altirra: un `.cas` estándar y uno turbo tipo A (elegidos por el responsable), tipo B y roto | carpeta `D:\rb-e0\m49-cas`: `host.jsonl`, `version.txt`, capturas; un solo host | C005 | AC4, AC5, AC8, AC9 | 0 |
| C007 | [P] | Docs: ADR-017 enmienda 12 (casetera `C:`), `host/README.md` (`.cas`, `--cassette`), `docs/fuentes.md` completo | archivos existen y enlazan; `check_notices` OK | C005 | R: ADR-017, ADR-012 | 0 |

- C003 es la puerta del plan: C004..C007 no empiezan sin su evidencia.

## Revisions

- 2026-11-20 — borrador (plan).
- 2026-11-20 — aprobado por el responsable («Aprobado, a tasks»); tareas C001–C007.
