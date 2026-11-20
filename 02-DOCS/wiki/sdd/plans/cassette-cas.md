---
type: plan
title: Plan — Cargar cintas .cas reales en el ESP32
description: CÓMO se construye la spec cassette-cas: un componente portable rb_cassette (lectura A8CAS, señal FSK, carga progresiva, reproductor con MOTOR) adaptado del código FSK propio del responsable, y adaptadores ESP32 (MOTOR, UART, RMT) tras M3a y H4.
tags: [sdd, plan, cassette, cas, fsk, esp32, motor]
timestamp: 2026-11-20T23:50:00Z
topic: sdd
slug: cassette-cas
spec: 02-DOCS/wiki/sdd/specs/cassette-cas.md
branch: feat/cassette-cas
status: approved
---

# Plan — Cargar cintas .cas reales en el ESP32

## 0. Global constraints (literales, para cada tarea)

- Código propio adaptable: solo la cuarentena `D:\rb-e0\fsk-propio` (ADR-012 enmienda 1). Se adapta, no se pega.
  Cada archivo nuevo que nace de ella registra en `docs/fuentes.md` el PR y el commit de origen:
  - #1756 → `6ea2070`, `5180b1d`, `947a72f` (FSK A8CAS básico: `casFSK`, `cassetteFSK`);
  - #1773 → `8601cef`, `57ca858` (carga progresiva: `casFSKLoader`, `cassetteFSKLoader`, `cassetteFSKRead`);
  - #1804 → `7d84b91` (parar el RMT al caer MOTOR).
- Ningún otro código de FujiNet ni de emuladores. `bus.h`, `fnio.h`, `pinmap.h`, `debug.h`, `global_types.h` y las
  clases `cassette`/`disk` de FujiNet se reescriben, no se traen.
- Constantes probadas (del código propio, se conservan): 100 ticks por unidad de 1/10 ms; porción máxima 32767
  ticks; tirada de como máximo 64 bloques `fsk `; bloques de carga de 512 bytes; margen de 20 s antes de empezar.
- Solo `.cas` reales en pruebas de cinta completa y demos, leídos desde `D:\GitHub\Respaldos\MicroSD` (variable
  `RB_CAS_SAMPLES`), nunca copiados al repo. Fragmentos de bytes mínimos solo para casos de formato.
- ADR-011: sin reservas de memoria durante la reproducción. La memoria de carga se reserva una vez, antes.
- Ningún pin SIO como salida sin H4 aprobado. MOTOR (GPIO36) no se usa sin M3a aprobado. Flash y salida GPIO:
  aprobación del responsable antes.
- C++17, `-fno-exceptions -fno-rtti -Werror`. `check_layering` y `check_notices` en verde.
- Cada commit, push y PR: aprobación del responsable.

## 1. Context & constraints

- Spec §«Por qué no Altirra»: NetSIO no lleva la señal `fsk ` (C003). El destino es el ESP32.
- Spec §«What it must respect»: el comportamiento probado del código propio (unión, carga progresiva, parada segura).
- ADR-002 (capas): lo puro no incluye ESP-IDF. `tools/check_layering.py` lo vigila por componente.
- El firmware hoy: `main/app_main.cpp` y componentes `rb_*`. La consola (`rb_console`) depende de ESP-IDF: no se
  prueba en el host. Ya tiene `mount` y `ls` para discos.
- Ya hecho en la rama: C001 (`host/src/cas_image.*`, 17 pruebas) y C002 (`host/src/cassette_player.*`,
  `cassette_deck.*`, enlace NetSIO). Sin commit: C003 (`cassette_run.hpp`, `--cassette`).

## 2. Architecture

```text
            ┌───────────────────────── rb_cassette (puro, rb/cassette/, solo rb/core) ─────────────────────────┐
 SD ──read──▶ TapeSource ─▶ CasIndex ─▶ segmentos {Data | FskRun} ─▶ CassettePlayer ─▶ ByteOut  (setBaud, send)│
            │                                  │                         ▲   │      ─▶ WaveOut  (niveles, ticks)│
            │                        FskLoader (bloques 512, margen 20 s)│   └──────▶ MotorIn   (encendido?)    │
            │                                               CassetteCommands (mount/status/eject/rewind)      │
            └──────────────────────────────────────────────────────────────────────────────────────────────────┘
                     ▲ host: fakes en test/host/cassette                ▲ ESP32: rb_esp_cassette
                                                                          (GPIO36, UART DATA IN, RMT DATA IN)
            rb_console ──"cas ..."──▶ CassetteCommands
```

- **rb_cassette** (nuevo, puro): todo lo que decide. Se prueba entero en el host.
- **rb_esp_cassette** (nuevo, plataforma): tres adaptadores finos (MOTOR, bytes por UART, señal por RMT) y la
  lectura de la SD. Sin lógica de cinta.
- **rb_console**: solo traduce `cas ...` a `CassetteCommands`.

**Decisión principal: la lógica del código propio pasa a un componente puro, y el RMT queda detrás de un puerto.**
En FujiNet la lógica y el RMT estaban juntos; solo se podían probar en el aparato. Separarlos permite probar en el
host todo lo que la spec pide antes de M3a y H4 (fase 1), que es lo único que hoy se puede avanzar. Alternativa
descartada: portar los archivos tal cual al ESP32 y probar solo en hardware: bloquea todo hasta H4 y no cumple
«adaptar, no pegar».

## 3. Interfaces & contracts

```text
TapeSource.read(offset, dst) -> bytes leídos | Error      // archivo de la SD; el host usa un archivo o memoria
CasIndex::build(source, out) -> Ok | Invalid(razón) | Unsupported(pwm)
  - segmentos en orden: Data{offset, length, gapMs, baud} | FskRun{offset, chunks<=64, gapMs, totalTicks}
  - baud 0, sin FUJI, bloque cortado -> Invalid; pwm* -> Unsupported
FskWave::next(run, cursor) -> {level, ticks<=32767} | End // niveles alternos desde el valor 0
FskLoader.step() -> progress | done | failed | stopped    // llena bloques fijos de 512; sin reservas
FskLoader.startGate() -> ready | wait                      // ready con 20 s de señal cargada o la tirada entera
CassettePlayer.tick(nowUs, MotorIn, ByteOut, WaveOut)
  - MOTOR apagado: congela; una tirada FSK se corta al final del nivel en curso (punto seguro)
  - nunca pierde ni repite: un envío rechazado no avanza la cinta
CassetteCommands.run(args, out) -> texto de respuesta     // mount <ruta> | status | eject | rewind
ByteOut.setBaud(b) / send(bytes) -> Ok | Busy;  WaveOut.push(level, ticks) -> Ok | Busy;  MotorIn.on() -> bool
```

## 4. Data model & flow

- Montar: `CassetteCommands` abre la ruta, `CasIndex` recorre solo cabeceras (no copia la cinta) y guarda los
  segmentos en una tabla fija. Un fallo deja la cinta anterior como estaba.
- Reproducir `Data`: el player lee el registro de la SD por partes y lo pasa a `ByteOut` a su `baud`.
- Reproducir `FskRun`: `FskLoader` lee bloques de 512 a un banco fijo (PSRAM en el ESP32); `FskWave` convierte
  valores en porciones; `WaveOut` las encola. Empieza cuando `startGate` da `ready`.
- Estado: segmento actual, posición dentro, MOTOR anterior. Reinicio en frío o `rewind` → posición 0.

## 5. Testing strategy

| AC | Nivel | Qué afirma | Qué se finge |
| --- | --- | --- | --- |
| AC1, AC7 | integración host | `international_karate.cas` y todas las cintas de `RB_CAS_SAMPLES` dan sus segmentos; se salta sin la carpeta | nada (archivo real) |
| AC2 | unidad | unión: sin pausa se une; pausa o bloque 65 abre otra tirada | fragmento de bytes |
| AC3 | unidad + real | suma y alternancia de niveles igual al archivo; porciones ≤ 32767 | `WaveOut` que graba |
| AC4 | unidad | lectura por bloques de 512; `startGate` no da `ready` antes de 20 s | `TapeSource` lento |
| AC5 | unidad | MOTOR cae a mitad de `Data` y de `FskRun`: sigue sin perder ni repetir | `MotorIn`, reloj |
| AC6 | unidad | mensajes de fallo; la cinta montada no cambia | fragmentos |
| AC8 | unidad | `mount/status/eject/rewind` y sus respuestas | `TapeSource`, salida de texto |
| AC9 | hardware | `cas status` sigue al MOTOR medido en M3a | nada |
| AC10–AC12 | demo | las cintas de referencia cargan en el Atari | nada |
| AC13 | suite | `ctest`, `tools/tests`, capas, avisos | — |

Las pruebas de C001/C002 que valen se mueven con el código; las de «`fsk ` = silencio» se reescriben con AC1–AC3.

## 6. Sequencing & dependencies

1. Limpiar lo que no sigue (C003 sin commit, enlace NetSIO de cinta).
2. `rb_cassette`: índice → señal FSK → carga progresiva → reproductor → órdenes. Cada paso con sus pruebas.
3. **Puerta M3a**: adaptador MOTOR y `cas status` en el ESP32.
4. **Puerta H4**: adaptadores UART y RMT, lectura SD, consola; demo.
5. Cierre.

## 7. Risks & open decisions

1. **DATA IN compartido por UART y RMT.** Bytes y señal salen por el mismo pin. *Disparador:* cambio de `data` a
   `fsk ` en la cinta. *Mitigación:* el código propio ya lo hacía en FujiNet; el adaptador conmuta el pin entre
   segmentos, nunca dentro. Se prueba primero con Alien Ambush (alterna los dos).
2. **MOTOR en GPIO36 sin medir (R1).** *Impacto:* lecturas falsas, carga que no arranca. *Mitigación:* M3a antes de
   C106; si la mitigación C cambia el cableado, solo cambia el adaptador.
3. **Memoria de carga.** La carga progresiva necesita un banco grande (PSRAM). *Mitigación:* reservarlo una vez al
   montar o al arrancar (ADR-011); C107 confirma la PSRAM de la placa antes de fijar el tamaño.
4. **Adaptar sin pegar.** *Mitigación:* reescritura con otros nombres y puertos; revisión de procedencia en cada
   tarea contra `D:\rb-e0\fsk-propio\LEEME.md`.
5. **Lectura de la SD lenta durante la señal.** *Mitigación:* el margen de 20 s y el reintento de lectura del código
   propio (3 intentos, 64 bytes de solape).

Decisión abierta: ninguna que bloquee la fase 1.

## Tasks

| Id | Tarea | AC | Riesgo | Done-check | Depende | Paralelo |
| --- | --- | --- | --- | --- | --- | --- |
| C100 | Quitar lo que no sigue: descartar C003 sin commit; retirar `netsio_tape_link.hpp`, `sendOutsideFrame`, `sendSpeed` y `encodeSpeedChange` (se conserva `motorOn` de NetSIO) | AC13 | 1 | `ctest` verde; `git status` sin `cassette_run.hpp` | — | no |
| C101 | `rb_cassette`: `TapeSource`, `CasIndex` con `Data`/`FskRun` y regla de unión; mover y adaptar pruebas de C001; prueba real con `RB_CAS_SAMPLES`; capa en `check_layering`; fila en `fuentes.md` (#1756) | AC1, AC2, AC6, AC7 | 1 | `test_cas_index` verde; AC7 pasa con la carpeta; capas OK | C100 | no |
| C102 | `FskWave`: valores → porciones de nivel | AC3 | 1 | `test_fsk_wave` verde con International Karate | C101 | sí (con C103) |
| C103 | `FskLoader`: bloques de 512 en banco fijo, `startGate` 20 s, reintento de lectura; fila #1773 | AC4 | 2 | `test_fsk_loader` verde; sin reservas en `step()` | C101 | sí (con C102) |
| C104 | `CassettePlayer` con `Data` (lógica de C002) y `FskRun`, parada segura (#1804) | AC5 | 2 | `test_cassette_player` verde | C102, C103 | no |
| C105 | `CassetteCommands`: mount/status/eject/rewind | AC6, AC8 | 1 | `test_cassette_commands` verde | C104 | no |
| C106 | **[puerta M3a]** `rb_esp_cassette`: MOTOR en GPIO36 (solo entrada); `cas status` en la consola; flash con aprobación | AC9 | 2 | evidencia M3a en `D:\rb-e0\m49-cas` | C105 + M3a | no |
| C107 | **[puerta H4]** UART y RMT en DATA IN, lectura SD, consola `cas`; demo con las cintas de referencia | AC10–AC12 | 2 | demo con evidencia | C106 + H4 | no |
| C108 | Cierre: `verify` y `review` de la rama | AC13 | 1 | registro en `verifications/` | C107 (o C105 si las puertas siguen cerradas) | no |

Si M3a o H4 siguen cerrados al terminar C105, la fase 1 se cierra sola (C108 sobre C100–C105) y C106–C107 esperan.

## Revisions

- 2026-11-20 — plan nivel 1 en Altirra (C001–C007), aprobado. C001 y C002 hechas; C003 falló.
- 2026-11-20 — reescrito tras el replanteo de la spec: ESP32, `rb_cassette` puro, código FSK propio, tareas
  C100–C108 por fases. Las tareas C004–C007 anteriores se retiran.
- 2026-11-20 — aprobado por el responsable.
