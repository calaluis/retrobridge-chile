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
  al commit. La evidencia sigue en `D:
b-e0\m49-cas\c003`.
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

## C104 — CassettePlayer: la cinta como una casetera (hecha, 2026-11-20)

- `components/rb_cassette`: `CassettePlayer` recorre los segmentos de `CasIndex` por tres puertos (`MotorIn`, `ByteOut`,
  `WaveOut`). Cada segmento espera su pausa. Registros: velocidad cuando cambia, bytes al ritmo del reloj (10 bits por
  byte, como máximo 512 por envío); MOTOR apagado congela. Tiradas `fsk `: el cargador arranca con la pausa, la señal
  espera la puerta de arranque y luego empuja porciones a `WaveOut`.
- MOTOR cae con la señal ya en marcha: la señal para, DATA IN vuelve a marca, el cargador para, y al volver MOTOR la
  misma tirada suena otra vez desde su pausa. Adaptado del código propio (PR #1804, commit 7d84b91; PR #1756); fila en
  `docs/fuentes.md`. Si la tirada ya había sonado entera, no se repite: pasa al segmento siguiente.
- Retirados `host/src/cas_image.*`, `cassette_player.*`, `cassette_deck.*` y sus pruebas del menú (eran de C001/C002).
- Cinta real: `Alien Ambush` (PAL), 100 segmentos, 49 tiradas, 8190 bytes de registros, recorrida entera.
- Controles:
  - pruebas: `test_rb_cassette_player` 25/25 con `RB_CAS_SAMPLES`; RED antes de GREEN; `ctest` 71/71 (se quitan 2
    pruebas viejas, entra 1);
  - compilación: sin avisos con `-Werror`; `check_layering` OK; `check_notices` OK;
  - seguridad: `tick()` sin reservas (ADR-011); lecturas acotadas por el segmento y el búfer de 512;
  - trazabilidad: AC5; `rsc risk`: tier 1, un revisor.
- Revisión (1 revisor, ronda 1). Corregido, con prueba cada uno:
  - el cargador fallaba a media tirada y la señal quedaba esperando: ahora la tirada se reinicia desde su pausa;
  - tras un corte, `start()` podía llamarse con el productor aún dentro de `step()`: ahora espera a que salga de
    `Loading`, y la pausa sigue contando mientras;
  - una caída de MOTOR con la tirada ya sonada la repetía: ahora avanza.

## C105 — CassetteCommands: `cas mount|status|eject|rewind` (hecha, 2026-11-20)

- `components/rb_cassette`: `CassetteCommands::run(args, motor, wave, out)` responde una línea. `mount` da el nombre y
  los bloques `data` y `fsk ` (la ruta puede tener espacios). Un archivo que no abre o no se indexa da su mensaje y
  deja la cinta montada y su posición como estaban (AC6). Uno bueno la reemplaza y queda al principio. `status`:
  segmento, fase, MOTOR y «run too big». `eject` y `rewind`. Archivos por el puerto `TapeFiles` (la SD en el ESP32).
- Dos tablas de segmentos del llamador: el índice nuevo se arma en la de repuesto; solo si sale bien se cambian.
- Una cinta que sale (mount, eject) se cierra solo cuando el productor ya no la carga; mientras, `mount` y `eject`
  responden «busy». Nuevo accesor `FskLoader::source()`.
- Código propio de RetroBridge: no adapta líneas de FujiNet; sin fila nueva en `docs/fuentes.md`.
- Cinta real: `PAL_ALIEN_AMBUSH_TURBO_SOFTWARE.cas` → «51 data, 49 fsk», 100 segmentos (cuenta independiente).
- Controles:
  - pruebas: `test_cassette_commands` 27/27 con `RB_CAS_SAMPLES`; RED: 26 de 26 fallos contra el esqueleto; `ctest`
    72/72;
  - sin reservas: contador de `new` alrededor de los comandos, con control positivo;
  - compilación: sin avisos con `-Werror`; `check_layering` OK; `check_notices` OK;
  - seguridad: ruta acotada a 256, nombre a 64, respuesta con `vsnprintf` acotado; ningún cierre con el productor
    leyendo;
  - trazabilidad: AC6, AC8; `rsc risk`: **no ejecutado** (permiso denegado en esta sesión); tier del plan: 1, un revisor.
- Revisión (1 revisor, ronda 1): sin defectos. Dos huecos de prueba cerrados: la cinta vieja se cierra cuando el
  cargador ya carga la nueva (el mutante que quitaba esa condición sobrevivía; ahora falla), y tras un montaje fallido
  la cinta vieja suena entera con sus segmentos. Anotado para C106: la cinta retirada se cierra en el siguiente `cas`;
  el adaptador del ESP32 puede llamar a `run` sin texto o cerrar en su bucle.

## C108 — Cierre de la fase 1 (hecha, 2026-11-20)

- Registro: `02-DOCS/wiki/sdd/verifications/cassette-cas-2026-11-20.md`. Veredicto: PASS para la fase 1, AC8 parcial
  (enganche a la consola en C106). AC9–AC12 esperan M3a y H4.
- Gate nuevo (`build/host-c108`, `build/exe-c108`): `ctest` 72/72, ejecutable 1/1, `tools/tests` 167/167, capas y
  avisos OK. `idf.py build` y sanitizadores no corren aquí: los corre el CI.
- Revisión de cierre (1 revisor, pruebas como evidencia): sin defectos; 6 huecos de prueba cerrados con pruebas, cada
  mutante comprobado.

## C106 — MOTOR en GPIO36 y `cas` en la consola del ESP32 (código hecho, M3a y flash pendientes, 2026-11-20)

- Cambio de estrategia del responsable: probar en el Atari real con la FujiNet, no en Altirra (ninguna de las 36
  cintas reales de MicroSD es solo `data`; NetSIO no lleva `fsk `).
- `rb_board`: `motor` y `motorGateApproved` (M3a) en el perfil; FujiNet: GPIO36, M3a **sin firmar**.
  `validate_for_motor()` da un `MotorAssignment` solo con la placa confirmada y M3a firmada; el pin debe ser de solo
  entrada (34–39) y no chocar con la microSD ni el SIO. `check_sio_pins()` también ve MOTOR.
- `rb_cassette`: `StdioTapeFiles` (cintas por stdio, 3 ranuras, lectura con candado: la leen el reproductor y el
  productor); `DiscardByteOut`/`DiscardWaveOut` (antes de H4 nada sale por DATA IN); `cas status` sin cinta muestra
  MOTOR.
- `rb_esp_cassette` (nuevo): `EspMotorIn` (configura GPIO36 como entrada solo si `validate_for_motor` lo da) y
  `CassetteService` (banco de 1 MB y tablas en PSRAM, una vez; tarea del bucle y tarea del productor; pedidos y
  respuestas numerados por colas). `rb_console`: comando `cas`. `app_main`: arranca la casetera si hay microSD.
- `sdkconfig.board.fujinet-atari-v1x`: PSRAM encendida (`CONFIG_SPIRAM`, con `IGNORE_NOTFOUND`). microSD: 7 archivos
  abiertos (antes 4).
- Controles:
  - pruebas: `test_motor_assignment` 7/7, `test_stdio_tape_files` 9/9 (Alien Ambush real hasta el final en las salidas
    que descartan); `ctest` 74/74; `check_layering` y `check_notices` OK;
  - RED: `StdioTapeFiles` falló contra el esqueleto. `validate_for_motor` se escribió antes de su prueba: compensado con
    5 mutantes, todos atrapados; el candado, con otro mutante atrapado;
  - **no ejecutado**: `idf.py build` (sin ESP-IDF en este equipo; lo compila el CI) y `rsc risk` (permiso denegado);
    tier del plan: 2;
  - seguridad: ningún GPIO del SIO ni GPIO36 se configura con M3a y H4 sin firmar (revisor de seguridad);
  - trazabilidad: AC8 (consola), AC9 (espera M3a).
- Revisión (2 revisores: corrección y seguridad; la lente de pruebas, cubierta con mutantes propios). Seguridad: sin
  defectos. Corregido: presupuesto de archivos de la microSD; una misma cinta leída por dos tareas (candado); respuesta
  tardía tomada por la siguiente (pedidos numerados); pila de las tareas; arranque sin PSRAM; limpieza si falla el
  arranque; MOTOR visible desde los chequeos del SIO; comentario de las salidas. Anotado: `cmd_cas` une los argumentos
  con un espacio (un nombre con dos espacios seguidos cambia).
- Falta para cerrar C106: compilar en el CI, M3a (medición del responsable), firmar M3a en el perfil, flashear con
  aprobación y ver `cas status` seguir a `CLOAD` (AC9).

## C107 — salida a DATA IN (UART y RMT), en curso (2026-11-21)

Rama `feat/cassette-output` (desde main 786ee39). Alcance limitado a C106/C107 por el responsable.

- `rb_board`: `validate_for_cassette_output()` y `CassetteOutputAssignment`: exige `validate_for_sio(ReceiveTransmit)`
  (H4) y `validate_for_motor()` (M3a). El perfil real no tiene ninguna de las dos firmada: no se maneja nada.
- `rb_cassette`: `FskSymbolFeed`, cola sin bloqueo (un productor, un consumidor) entre el bucle de la casetera y la
  interrupción de recarga del RMT; dos porciones por símbolo, cola impar con `duration1 = 0` (código propio, PR #1756).
- `rb_esp_cassette`: `EspDataIn` (ByteOut y WaveOut en el mismo pin): registros por UART2 8N1 a 600 baudios (escrito de
  cero), FSK por RMT a 1 MHz con un canal por tirada, el pin vuelve a la UART al terminar o en `stop()`; fin de
  transmisión en marca. `CassetteService` usa `EspDataIn` solo si se configuró; si no, las salidas que descartan.
  `app_main`: `EspDataIn::configure()` solo con `validate_for_cassette_output()` aceptado.
- Controles:
  - pruebas: `test_cassette_output_assignment` 6/6, `test_fsk_symbol_feed` 9/9 (incluye productor y consumidor en dos
    hilos); `ctest` 76/76;
  - RED: las dos pruebas fallaron al compilar antes del código; una regla de `fill` cambió tras un fallo (no cortar la
    transmisión si se escribieron símbolos enteros);
  - **no ejecutado**: `idf.py build` (lo compila el CI), `rsc risk` (permiso denegado; tier del plan: 2) y la demo;
  - seguridad: sin H4 y M3a firmadas ningún pin del SIO se configura (`app_main` y `EspDataIn`).
- Revisión (1 revisor de corrección, por economía de tokens; tier 2 pedía 3). Sin bloqueos. Corregido: el RMT arrancaba en espacio antes del primer símbolo (`init_level = 1`); un fallo del RMT reintentaba cada tick y colgaba al reproductor (ahora la tirada se descarta, el pin vuelve a la UART y se cuenta); el margen al productor se perdía entre bloques de 32 símbolos. Anotado: canal RMT creado por tirada (asigna memoria, como la beta) y la recarga fuera de IRAM (una escritura en flash durante una tirada podría cortarla).
- Hallazgo: `tools/ci/firmware_package.py` prohíbe `uart_driver_install`, `uart_param_config`, `uart_set_pin` y
  `gpio_config` en `main/` y `components/` (puerta H4). Ya fallaba con `gpio_config` de C106; ahora también con la UART
  de C107. No lo debilité: ver la cola de decisiones.
- Falta para cerrar C107: compilar en el CI, M3a, H4 (parte de DATA IN), firmar ambas en el perfil, flashear con
  aprobación y la demo desde la microSD: `international_karate.cas` (AC10), Alien Ambush o Bruce Lee (AC11) y una cinta
  turbo solo `fsk ` elegida por el responsable (AC12).

## C107 — empaquetado de la casetera (2026-11-21)

Rama `feat/cassette-package` (desde main 7621040). Atiende la decisión bloqueante del empaquetado, con el alcance que
fijó el responsable: la excepción solo para las llamadas de C107, con H4 y M3a comprobadas al generar y al verificar.

- `tools/ci/firmware_package.py`: variante `cassette` (`retrobridge-fw-cassette-fujinet-atari-v1x`).
  - Código: vigila todo identificador `gpio_*`, `uart_*`, `rmt_*`, `esp_rom_gpio_*`, `ledc_*`, `sigmadelta_*` y
    `dedic_gpio_*` en `main/` y `components/` (sin comentarios ni cadenas; cuenta también llamadas partidas en dos
    líneas y punteros). Solo admite los 26 usos de C107, archivo por archivo y en su número exacto (15 funciones en
    `esp_data_in.cpp`; `gpio_config` y `gpio_get_level` de MOTOR en `cassette_service.cpp`). Además, cada
    `uart_set_pin` debe ir a `pins.dataIn()`/`pin_` sin RX/RTS/CTS, y `kUart` debe ser `UART_NUM_2`.
  - Puertas al generar: H4 y M3a en `true` en el perfil compilado (`fujinet_atari_v1x.hpp`, leído campo por campo; si
    no se puede leer, rechaza; también rechaza `#if`/`#define` en esa cabecera, una segunda definición y un orden
    de campos de `BoardProfile` distinto del esperado) y en `docs/mediciones/puertas.json` (JSON `true`, fecha y registro existente en
    `docs/mediciones/` que nombra su puerta; sin claves repetidas en el JSON, con fecha real).
  - Puertas al verificar: el paquete lleva `config/puertas.json`; `verify` lo vuelve a comprobar, lo compara con
    `PROVENANCE.json` y exige registradas las comprobaciones de perfil, registro y código.
  - Marcadores: `EspDataIn` y `CassetteService` en la aplicación y en el ELF; versión = commit fijado.
  - `h3c` y `diag` sin cambios: siguen rechazando cualquier llamada del SIO y no leen las puertas.
- `docs/mediciones/puertas.json`: H4 y M3a **no aprobadas**. El perfil real queda igual (`false`, `false`).
- Controles:
  - pruebas: 43 nuevas en `test_firmware_package.py`; `tools/tests` 210/210;
  - RED: 26 de las 30 fallaron antes del código (las 4 que pasaban son las de `h3c`/`diag`, que ya rechazaban);
  - mutantes: 6, todos atrapados (perfil solo H4, aprobación no estricta, `verify` sin comparar procedencia, llamadas
    fuera de lista, `verify` sin exigir lo registrado, registro inexistente); tras la revisión, 10 más, todos
    atrapados (la prueba de `#define` se reforzó porque al principio no lo atrapaba);
  - contra el código real de main: 8 llamadas exactas; perfil leído `(false, false)`; registro rechazado;
  - **no ejecutado**: empaquetar un build real (sin ESP-IDF aquí) y `rsc risk` (permiso denegado; tier 2).
- Revisión (1 revisor de corrección, por economía de tokens). Corregido: un perfil aprobado escondido tras `#if 0` o
  una segunda definición engañaba al lector del perfil; el escáner no veía `rmt_*`, `uart_write_bytes`, punteros ni
  llamadas partidas; los comentarios contaban; `uart_set_pin` no fijaba el pin; JSON con clave repetida, fecha
  imposible o registro que no nombra su puerta. Anotado: `h3c`/`diag` conservan su escáner por línea (límite previo,
  sin cambios a pedido del responsable); `verify` de un paquete sin firma criptográfica solo comprueba coherencia
  interna (quien edite el paquete y el manifiesto a la vez no se detecta).
- Flasheo por COM3: espera la aprobación del responsable.

### Empaquetado en solo lectura (`cassette-lectura`, rama `feat/cassette-lectura-pkg`, 2026-11-21)

- Motivo: con H4 y M3a sin firmar, ningún paquete aceptaba `main` (`h3c`/`diag` rechazan las llamadas de C107 y
  `cassette` exige las puertas). Se quería validar `cas mount` y `cas status` por USB antes de tocar el hardware.
- Variante nueva `cassette-lectura`: las mismas 26 llamadas exactas, y **exige** H4 y M3a en falso en el perfil y
  en `puertas.json`, al generar y al verificar (`closed_gates`). En ejecución, el perfil sin firmar hace fallar
  `validate_for_cassette_output` y `validate_for_motor`: DATA IN y MOTOR no se configuran.
- CI: job `firmware-package-cassette-lectura` (PR y ejecución manual), firmware fijado en `f405106`, con `verify`
  del paquete. Los jobs de paquetes se habían quitado en `c2698d9`; este sigue su mismo patrón.
- Evidencia local: `python -m unittest discover -s tools/tests` → 230 OK (20 nuevas). Mutantes: aceptar cualquier
  registro, aceptar cualquier perfil y no exigir las comprobaciones en `verify` → todos fallan (3, 4 y 2 pruebas).
  **No ejecutado aquí:** el build real (sin ESP-IDF local); lo cubre el job de CI.
- Procedimiento: `docs/hardware/procedimiento-instalacion-recuperacion.md` §7 (respaldo D7, instalación y R0/R1).
  Flasheo por COM3: espera la aprobación del responsable.

## Decision queue

- **resuelta en `feat/cassette-package` (2026-11-21)** — `tools/ci/firmware_package.py` rechaza las llamadas a UART y GPIO de los pines del SIO
  (`FORBIDDEN_CALLS`), y con eso el empaquetado del firmware para flashear. C106 (`gpio_config` de MOTOR) y C107 (UART2
  en DATA IN) las necesitan. Propuesta: permitirlas solo en `components/rb_esp_cassette/` cuando H4 y M3a estén
  firmadas, en un PR aparte. Decide el responsable.

- **review-after (2026-11-20)** — AC5 dice «sin perder ni repetir». Para tiradas FSK, si MOTOR cae con la señal en
  marcha, la tirada se repite desde su pausa. Así lo hace el código propio (PR #1804/#1756), que funciona con cintas
  reales. Seguí el código propio. Para registros sí vale «sin perder ni repetir». Decide: ¿ajusto el texto de AC5?

- **resuelta (2026-11-20)** — C003 falló con `international_karate.cas`. La propuesta de una cinta inventada se
  rechazó: «No hay que inventar cintas». Replanteo: spec y plan reescritos para el ESP32 con el código FSK propio
  (tareas C100–C108). C003 queda como evidencia de por qué NetSIO no sirve.
