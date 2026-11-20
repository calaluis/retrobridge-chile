---
type: progress
title: Progreso — cassette-cas
description: Avance por tarea de la spec cassette-cas (cargar cintas .cas de nivel 1 desde el menú).
tags: [sdd, progress, menu, cassette, cas]
timestamp: 2026-11-20T22:00:00Z
topic: sdd
slug: cassette-cas
status: in-progress
---

# Progreso — cassette-cas

Rama `feat/cassette-cas` (worktree `.worktrees/feat-cassette-cas`, desde main `b3183dc`). Plan en `d2650e0`.

## C001 — Fuente A8CAS y CasImage (hecha, 2026-11-20)

- Fuentes en `docs/fuentes.md`:
  - formato A8CAS (a8cas.sourceforge.net, *CAS file format - description*);
  - código propio adaptado (FujiNet PR #1756, commit 6ea2070, `casFSK`), ADR-012 enmienda 1.
- `host/src/cas_image.{hpp,cpp}`: `CasImage::index` → `Ok` con bloques, `Invalid` o `Pulses`.
- Desviación 1 del plan (menor, sin cambio de alcance):
  - los bloques `pwm*` también dan `Pulses`: la fuente los describe como señales de pulsos que el nivel 1 no puede
    reproducir; el plan solo nombraba `fsk `.
  - la fuente no dice si un bloque desconocido se puede saltar; saltarlo es decisión propia (anotada en
    `fuentes.md`).
- Controles:
  - pruebas: `test_cas_image` 17/17 (RED 7 fallos contra el esqueleto, luego verde; 5 más tras la revisión); `ctest` 68/68;
  - compilación: sin avisos con `-Werror`;
  - seguridad: función pura sin E/S; límites comprobados antes de cada lectura;
  - trazabilidad: AC6 (índice de velocidad y pausa), AC8 (`fsk ` solo y `pwm*`), AC9 (sin `FUJI`, cabecera y
    bloque cortados, `fsk ` impar, sin datos).
  - `check_layering` OK; `check_notices` OK; `rsc risk`: tier 1, un revisor.
- Revisión (1 revisor, ronda 1), 4 hallazgos, todos corregidos:
  - la suma de un silencio `fsk ` largo daba la vuelta en 32 bits → `silenceUs` de 64 bits; prueba con 656 × `$FFFF`;
  - `baud` 0 se aceptaba → `Invalid` (decisión propia: una velocidad 0 no se puede reproducir);
  - un `data` vacío contaba como dato → queda como silencio de su pausa; una cinta solo con `data` vacíos es
    `Invalid`, y con `fsk ` es `Pulses`;
  - dos pruebas de fallo no comprobaban la cinta vacía → ahora sí.

## C002 — Reproductor, casetera y NetSIO (hecha, 2026-11-20)

- `host/src/cassette_player.{hpp,cpp}`: `TapeLink` (puerto) y `CassettePlayer`. Con MOTOR encendido: pausa,
  silencio, velocidad (solo si cambia) y bytes a 10 bits por byte con el reloj del host; como máximo 512 por envío.
  Con MOTOR apagado se congela. Un envío fallido no mueve la cinta. El ritmo se cuenta en µs × baud: no deriva.
- `host/src/cassette_deck.{hpp,cpp}`: la cinta de `C:` (copia de los bytes, índice, nombre); `mount` rechaza
  `Pulses` e `Invalid` sin tocar `C:`; `eject`; `rewind`.
- `host/src/netsio_tape_link.hpp`: `TapeLink` sobre NetSIO, con `sendOutsideFrame()`: nunca espera; falla si hay
  una trama, una sincronía o una escritura en curso (el transporte tomaría los bytes como el ACK del disco).
- `components/rb_netsio`: `encodeSpeedChange`; `motorOn()` desde `20`/`21` (ya no cuentan como ignorados;
  estadística `motorChanges`; apagado tras conectar, desconectar y cada reinicio); `sendSpeed()` sin crédito;
  `sendOutsideFrame()`: sin crédito pide `C6` una vez y falla hasta que llega el `C7`.
- Controles:
  - pruebas: `test_cassette_player` 15/15; `test_netsio_tape_link` 3/3; códec y transporte con casos nuevos;
    `ctest` 70/70;
  - RED: la implementación del reproductor se escribió antes de ver el rojo; en su lugar, 8 mutantes (sin MOTOR,
    sin pausa, sin límite de 512, sin guarda de sincronía, sin estado de MOTOR, sin tope tras un rechazo, contando
    tiempo parado, reinicio sin apagar MOTOR): todos fallan las pruebas;
  - compilación: sin avisos con `-Werror`; `check_layering` OK; `check_notices` OK;
  - `rsc risk`: tier 1 (el plan preveía 2), un revisor;
  - trazabilidad: AC6 (pausa y velocidad), AC7 (MOTOR congela y reanuda), AC12 a nivel de casetera (`rewind`).
- Revisión (1 revisor, ronda 1), 4 hallazgos, todos corregidos con prueba:
  - MOTOR seguía encendido tras desconectar o reiniciar → se apaga en `connect`, `disconnect`, `20`/`FE`/`FF`;
  - el envío de cinta podía esperar crédito hasta 1 s dentro del bucle y perder órdenes de disco →
    `sendOutsideFrame()` no espera y falla también dentro de una trama;
  - tras un rechazo, la cinta mandaba de golpe lo atrasado → el tiempo rechazado no se debe;
  - el primer tick tras encender MOTOR contaba tiempo parado, y un reloj que retrocede contaba dos veces → corregido.
- Pendiente para C003: que Altirra acepte `SPEED_CHANGE` desde el dispositivo y bytes fuera de una trama.

## C003 — Puerta de viabilidad (detenida, 2026-11-20)

- `host/src/cassette_run.hpp` y opción `--cassette <archivo.cas>` (con `--image` o `--images`); compila sin avisos.
- Demo 1 en Altirra (evidencia `D:\rb-e0\m49-cas\c003`: `host.jsonl`, `version.txt`), `international_karate.cas`:
  - 20:04:05 reinicio en frío con START; el host rebobina. 20:04:17 llega MOTOR ON (`21`): el menú en D1 no estorbó.
  - 600 baudios: 9 registros de 132 bytes enviados al ritmo de la cinta; 20:04:48 la cinta pasa a 750 baudios.
  - 20:05:19 el Atari pide `STATUS` a D1: el arranque de cinta falló. El responsable vio `BOOT ERROR` y `SELF TEST`.
  - Nunca llegó MOTOR OFF (`20`), ni al fallar. Créditos: 1638 rechazos momentáneos, sin mover la cinta.
- Resultado: **AC1 no se cumple todavía**. Regla de la puerta: parar y volver al responsable.
- Hipótesis (sin verificar): la parte estándar a 600 se leyó (el SO siguió 9 registros); la falla está en el tramo a
  750 del cargador propio del juego, o en los bloques `fsk ` (uno antes de cada registro) tratados como silencio.

## C100 — Quitar lo que no sigue (hecha, 2026-11-20)

- C003 sin commit descartado: `host/src/cassette_run.hpp` borrado; `host/src/main.cpp` y `host/CMakeLists.txt` vuelven
  al commit. La evidencia sigue en `D:b-e0\m49-cas\c003`.
- Retirado el envío de cinta por NetSIO: `encodeSpeedChange`, `sendSpeed`, `sendOutsideFrame`, `creditAsked_`,
  `host/src/netsio_tape_link.hpp` y `test_netsio_tape_link`, más sus pruebas de códec y transporte.
- Se conserva: `motorOn()`, `motorChanges` y sus dos pruebas (MOTOR de NetSIO).
- Controles:
  - pruebas: `ctest` 69/69 (70 menos `test_netsio_tape_link`); `retrobridge_host` compila;
  - compilación: sin avisos con `-Werror`; `check_layering` OK; `check_notices` OK;
  - seguridad: solo borrado; ninguna entrada nueva;
  - trazabilidad: AC13; `rsc risk`: tier 1, un revisor.
- Revisión (1 revisor, ronda 1): sin hallazgos; 1 detalle corregido (comentario huérfano en `test/host/CMakeLists.txt`).

## C101 — rb_cassette: TapeSource y CasIndex (hecha, 2026-11-20)

- `components/rb_cassette` (puro, capa en `check_layering`): `TapeSource` (lectura posicional; `MemoryTapeSource`) y
  `CasIndex::build` → segmentos `Data` (`baud`, pausa) y `Fsk` (tirada, pausa, bloques), en una tabla del llamador, sin
  reservas. Estados: `Ok`, `Invalid`, `Unsupported` (`pwm*`), `Full`, `ReadError`.
- Regla de unión adaptada de `casFSK` (`fsk_scan_run`, PR #1756, commit 6ea2070); fila en `docs/fuentes.md`.
- Desviaciones (menores):
  - `fsktest/fsk_mem_513.cas` y `fsk_mem_65535.cas` (pruebas de memoria de FujiNet, un `fsk ` de longitud impar) dan
    `Invalid`, igual que `fsk_scan_run`; la prueba AC7 lo espera así.
  - 1 a 7 bytes sobrantes al final hacen `Invalid` todo el archivo (AC6, bloque cortado). `fsk_scan_run` solo cierra
    la tirada; ninguna cinta real tiene ese caso. Decisión propia, señalada por el revisor.
  - `host/src/cas_image.*` y el reproductor de C002 siguen hasta C104, que los reemplaza.
- Controles:
  - pruebas: `test_cas_index` 21/21 con `RB_CAS_SAMPLES` (16 cintas reales Ok, 2 de memoria `Invalid`); sin la variable,
    19 pasan y 2 se ignoran; RED: 14 fallos contra el esqueleto; `ctest` 70/70;
  - compilación: sin avisos con `-Werror`; `check_layering` OK; `check_notices` OK;
  - seguridad: límites comprobados antes de cada lectura y suma; sin E/S propia;
  - trazabilidad: AC1 (International Karate: 106 `data` a 600/750, 107 `fsk ` en tiradas), AC2 (unión, otro bloque
    corta, 64+1), AC6 (fallos), AC7 (todas las cintas reales); `rsc risk`: tier 1, un revisor.
- Revisión (1 revisor, ronda 1): sin hallazgos que corregir; 2 preguntas anotadas arriba.

## C102 — FskWave: la señal de una tirada (hecha, 2026-11-20)

- `components/rb_cassette`: `FskWave` convierte los valores de una tirada, por piezas, en porciones {nivel, ticks}:
  1 valor = 100 ticks de 1 µs; índice par = espacio, impar = marca, contado en toda la tirada; un 0 no da porción pero
  gasta su índice; porciones de como máximo 32767 ticks; el resto de un valor partido sigue en la pieza siguiente.
- Adaptado de `casFSK.h` y `cassette_fsk_pull_portion` (PR #1756, commits 6ea2070 y 5180b1d); fila en `docs/fuentes.md`.
- Cintas reales: International Karate, 107 tiradas cortas (48,1 ms de señal, sin partir); `turbo_software_zorro.cas`,
  1 tirada de 927 s (unos 15 min) con 2281 porciones partidas. Esa tirada confirma que C103 (carga de a poco) hace falta.
- Controles:
  - pruebas: `test_fsk_wave` 10/10 con `RB_CAS_SAMPLES` (8/8 y 2 ignoradas sin ella); RED: 6 fallos contra el
    esqueleto; `ctest` 71/71;
  - mutantes: sin `remaining_ = 0` en `reset()`, y con `remaining_ = 0` en `setValues()`: los dos fallan;
  - compilación: sin avisos con `-Werror`; `check_layering` OK; `check_notices` OK;
  - seguridad: sin E/S ni reservas; `RB_ASSERT` de longitud par;
  - trazabilidad: AC3; `rsc risk`: tier 1, un revisor.
- Revisión (1 revisor, ronda 1): sin defectos; 3 huecos de prueba corregidos (resto pendiente entre piezas, `reset()`
  a mitad de una porción, una cinta real con valores largos).

## C103 — FskLoader: carga de a poco (hecha, 2026-11-20)

- `components/rb_cassette`: `FskLoader` carga una tirada `fsk ` en un banco contiguo del llamador (PSRAM en el ESP32,
  reservado una vez): una lectura = como máximo un bloque de 512 o una cabecera; cuenta de valores publicada con release
  y leída con acquire; consumidor con `next()` (`Value`/`Underrun`/`End`), `piece()` para `FskWave` y `startGate()`
  (20 s de señal y 1536 valores, o tirada final); 3 reintentos por lectura, el 4.º fallo seguido falla la tirada;
  `requestStop()`; una cabecera que ya no coincide con el índice falla la tirada.
- Adaptado de `casFSKLoader` (PR #1773, commits 8601cef y 57ca858); fila en `docs/fuentes.md`. Cambios propios: banco
  contiguo en vez de bloques reservados uno a uno; forma de la tirada desde `CasIndex`. El solape de 64 bytes de
  `cassetteFSKRead` queda para la lectura de la SD (C107).
- Cinta real: `turbo_software_zorro.cas`, 252 122 valores (927 s de señal) en 1000 lecturas; la puerta abre tras 7.
- Controles:
  - pruebas: `test_fsk_loader` 14/14 con `RB_CAS_SAMPLES`; RED: 12 de 13 fallos contra el esqueleto; `ctest` 72/72;
  - sin reservas: contador de `new` alrededor de `step()`, `next()`, `piece()` y `startGate()`, con control positivo;
  - compilación: sin avisos con `-Werror`; `check_layering` OK; `check_notices` OK;
  - seguridad: escritura en el banco acotada por `start()` (tirada ≤ banco) y por la cabecera (≤ lo que falta);
  - trazabilidad: AC4; `rsc risk`: tier 1 (el plan preveía 2), un revisor.
- Revisión (1 revisor, ronda 1): sin defectos. Corregido: el presupuesto de fallos fallaba en el 3.º; el código propio
  (`read_fault`) tolera 3 reintentos y falla en el 4.º; ahora igual, con prueba. Añadida prueba de `requestStop()`
  tras `Final`. Anotado: `start()` borra un `requestStop()` anterior; quien llama los ordena (C104).

## Decision queue

- **resuelta (2026-11-20)** — C003 falló con `international_karate.cas`. La propuesta de una cinta inventada se
  rechazó: «No hay que inventar cintas». Replanteo: spec y plan reescritos para el ESP32 con el código FSK propio
  (tareas C100–C108). C003 queda como evidencia de por qué NetSIO no sirve.
