# Carga `.cas` con el motor FSK de la beta del responsable

- Fecha: 2026-11-26. Rama `feat/cas-motor-fujinet` desde `main` `8bb25d0`.
- Referencia fijada: `calaluis/fujinet-firmware`, etiqueta `v1.6.2-atari-fsk-beta.1`, commit
  `cf3382fafeed9b57e307b8618d7ba7173e7e6ec9`. No se usó `master`.
- Base legal: ADR-012, enmiendas 1 y 2. Entran tal cual solo los archivos del responsable. `cassette.cpp` es
  código ajeno (GPL-3.0): se leyó en el commit fijado y no se copió nada.
- Variante: `CONFIG_RB_CAS_ENGINE_FUJINET` (`sdkconfig.cas-fujinet`, sobre menu-sio). Por defecto `n`.
- Alcance: software, compilación y pruebas del host. **Nada de esto se probó en el Atari.** No se afirma que
  `international_karate.cas` cargue en el 65XE.

Etiquetas: **[V]** verificado (prueba del host o lectura citada), **[I]** inferido, **[P]** pendiente.

## 1. Qué es idéntico, qué es adaptador y qué se comporta distinto

| Pieza | Clase | Dónde | Evidencia |
|---|---|---|---|
| Formato A8CAS, `cas_data_record_ms` | Idéntico | `lib/media/atari/casTape.h` | `fuente.sha256` **[V]** |
| Tiradas `fsk `, niveles, porciones, símbolos (`fsk_scan_run`, `fsk_pull_portion`, `fsk_fill_symbols`) | Idéntico | `lib/media/atari/casFSK.*` | `fuente.sha256`; `test_esp_fsk_engine` compara cada porción RMT con `FskWave` **[V]** |
| Carga progresiva: puerta, umbral de 16 384 bytes, reintentos | Idéntico | `casFSKLoader.*`, `cassetteFSKLoader.*`, `cassetteFSKRead.*` | `fuente.sha256`; Missile Command progresiva en el host **[V]** |
| Canal RMT, codificador, tarea `fskload`, parada por MOTOR, aborto por `RMTMEM` | Idéntico salvo dos líneas | `lib/device/sio/cassetteFSK.cpp` | Cambio `idf61-rmt-flags` (`FUENTE.md`); compila en la CI con ESP-IDF v6.1 **[P]** hasta ver la CI |
| Compatibilidad con ESP-IDF v6.1 | Adaptador | `adapt/rb_fujinet_idf6.h`, `adapt/soc/uart_periph.h` | Ver §3, D9 |
| `global_types.h`, `fnio.h`, `bus.h`, `debug.h`, `pinmap.h` | Adaptador | `adapt/`, `lib/include/`, `src/fujinet_adapt.cpp` | Escritos para RetroBridge |
| `send_FUJI_tape_block` (un registro por paso) | Adaptador | `src/fuji_tape_loop.cpp` | `test_fuji_tape_loop` (10) con Karate y Alien Ambush **[V]** |
| Cuándo se activa el casete (bus SIO) | Adaptador con reglas propias | `src/fuji_tape_runner.cpp` | `test_fuji_tape_runner` (5); reglas en §3 **[I]** |
| Llamada a `cassette_fsk_play_run` | Adaptador | `src/esp_fsk_engine.cpp` | `test_esp_fsk_engine` (7) **[V]** |
| Disco, ATR, xex, menú, TNFS | Sin cambios | — | `ctest` 106/106 **[V]** |

## 2. El camino de la beta, del montaje a DATA IN

Leído en `cassette.cpp` y en los archivos del responsable, en el commit fijado.

1. Al activar el casete, la UART pasa a 600 baudios.
2. Cada vuelta del bus llama una vez a `send_FUJI_tape_block`.
3. Busca el siguiente bloque `data` o `fsk `. Un bloque `baud` cambia la velocidad.
4. Espera la pausa del bloque, 1 ms cada vez. Si MOTOR cae y quedan más de 1000 ms de pausa, vuelve al inicio del
   bloque.
5. Un bloque `fsk ` llama a `cassette_fsk_play_run`. Si el resultado no es `ok`, vuelve al inicio del bloque.
6. Un bloque `data` sale por la UART en trozos de 256 bytes como máximo, cada uno con `flushOutput`.
7. Al final de la cinta, el desplazamiento vuelve a 0 y el casete se desactiva (19 200 baudios).

`cassette_fsk_play_run` (del responsable): con un primer bloque de 16 384 bytes o más, carga progresiva con la
tarea `fskload`; si no, precarga la tirada en PSRAM. Una sola llamada bloqueante a `rmt_transmit`. Mientras
transmite, comprueba MOTOR cada 100 ms. Si MOTOR cae: `stop_req`, el pin vuelve a la UART y, con el canal de 8
bloques, `RMTMEM[0] = 0` y `rmt_ll_tx_reset_pointer`.

En RetroBridge: `FujiTapeRunner` hace los pasos 1, 2 y 7; `FujiTapeLoop` los pasos 3 a 6; `EspFskEngine` llama a
`cassette_fsk_play_run` sin cambios.

## 3. Supuestos y diferencias

| Id | Tema | Beta | RetroBridge | Estado |
|---|---|---|---|---|
| D1 | Cuándo se activa el casete | Lo decide el bus SIO de FujiNet (`sio.cpp`), **no leído** | MOTOR encendido activa a 600 baudios; MOTOR apagado entre registros desactiva y la cinta guarda su sitio; tras el final espera MOTOR apagado y encendido | **[I]** supuesto |
| D2 | Nuevo montaje, expulsión, rebobinado | Del bus y de la interfaz web de FujiNet | Cada uno sube la generación de `CassetteCommands`; el motor vuelve al inicio | **[I]** |
| D3 | Pausa de 1 ms | `delay_microseconds(999)` en espera activa | `vTaskDelay(pdMS_TO_TICKS(1))` con `CONFIG_FREERTOS_HZ=1000`: la primera espera puede durar menos de 1 ms y otra tarea puede alargar alguna | **[I]** la pausa total difiere en menos de 1 ms más la latencia del planificador |
| D4 | Registro cortado (cabecera dice más bytes que quedan) | Envía lo que lee, a veces con el búfer anterior | Termina la cinta sin enviar nada del registro | **[V]** decisión propia |
| D5 | Cabecera incompleta al final | Lee lo que hay | Exige 8 bytes; si no, fin de cinta | **[V]** decisión propia |
| D6 | `_record_ms` de un `fsk ` | `gap + waveform_ms`, con `gap` ya en 0 tras la espera | `waveform_ms`. Solo informativo: no cambia la señal | **[V]** |
| D7 | Modo compartido (la UART2 es también del disco) | No existe | No se usa este motor; sigue `CassettePlayer` | **[V]** `start()` |
| D8 | Rebobinar por segundos (PR #1722) | Existe | Diferido, como en la spec `cassette-cas` | **[P]** |
| D9 | ESP-IDF | La versión de la beta | v6.1: sin `io_loop_back`/`io_od_mode`; `hal/uart_periph.h`; `SOC_RMT_SUPPORT_TX_ASYNC_STOP` = 0; `SOC_RMT_CHANNELS_PER_GROUP` = `RMT_LL_CHANS_PER_INST`; `rmt_ll_tx_get_mem_blocks` leído de `conf0.mem_size` | **[P]** que el canal 0 reciba 8 bloques en v6.1: sin verificar en placa |
| D10 | Pruebas originales del responsable (`CasFSKTests` y otras) | Existen en FujiNet | No se ejecutaron. Se escribieron pruebas propias que comparan toda la salida con `CasIndex` y `FskWave` | **[P]** |
| D11 | Puerta del empaquetador | — | `check_sio_calls` no cuenta `components/rb_fujinet_fsk/`. A cambio, todo paquete exige el motor apagado en `sdkconfig` y ausente de `.symtab` (`FUJINET_FSK_CHECK`); un ELF ilegible se rechaza | **[V]** `test_firmware_package` y 4 mutaciones |

## 4. Puertas que no cambian

- H4 sigue sin aprobar. `kTapeOutput = false` en `main/menu_sio.cpp`: DATA IN no se conduce y el motor no suena.
- El motor solo arranca si DATA IN es exclusivo del casete y su memoria está lista.
- La variante no se empaqueta ni se flashea. La CI comprueba que `cassette_fsk_play_run` solo está en el ELF de
  `build-cas-fujinet` y no en `build`, `build-fujinet` ni `build-menu-sio`.
- La placa FN32ROV-1.3-Q24 sigue original. No se usó COM3, el 65XE ni GPIO.

## 5. Pruebas

| Prueba | Qué demuestra |
|---|---|
| `test_fuji_tape_loop` (10) | Karate y Alien Ambush registro a registro contra `CasIndex`: tipo, baudios, pausas, bytes. 5 mutaciones caen |
| `test_fuji_tape_runner` (5) | Reglas de D1 y D2; UART ocupada. 3 mutaciones caen |
| `test_esp_fsk_engine` (7) | Código del responsable sobre fakes de ESP-IDF: cada byte de UART y cada porción RMT de Karate, Alien Ambush y Missile Command (progresiva); MOTOR cae (pin a la UART con el canal activo, `RMTMEM[0] = 0`, un reinicio de puntero); sin memoria; tirada corta. 5 mutaciones caen |
| `check_fujinet_fsk` y sus 9 autopruebas | Los archivos del responsable siguen idénticos al commit fijado, salvo el cambio declarado |
| `test_firmware_package` | D11 |
