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
| `send_FUJI_tape_block` (un registro por paso) | Adaptador | `src/fuji_tape_loop.cpp` | `test_fuji_tape_loop` (12) con Karate y Alien Ambush **[V]** |
| Cuándo se activa el casete (bus SIO) | Adaptador con reglas propias | `src/fuji_tape_runner.cpp` | `test_fuji_tape_runner` (20); reglas en §3 **[I]** |
| Traspaso de DATA IN con el disco (menu-sio) | Adaptador: el mismo protocolo que `CassetteTick` (plan menu-cinta §2.4) | `src/fuji_tape_runner.cpp`, `cassette_service.cpp` | `test_fuji_tape_runner`, `test_fuji_shared` (2) con la cadena real **[V]** en el host |
| Llamada a `cassette_fsk_play_run` | Adaptador | `src/esp_fsk_engine.cpp` | `test_esp_fsk_engine` (7) **[V]** |
| `cas status` y fin de cinta | Adaptador | `TapePosition` (`cassette_commands.hpp`), `FujiTapeRunner` | `test_cassette_commands`, `test_fuji_tape_runner`, `test_fuji_shared` **[V]** |
| Disco, ATR, xex, menú, TNFS | Sin cambios | — | `ctest` completo **[V]** |

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

### 2.1 Integración con menu-sio (revisión de la PR #122, H1-H3)

En menu-sio, DATA IN es la TX de la UART2 del disco. La casetera la usa en modo compartido
(`EspDataIn::attachShared`) y la tarea SIO se la presta (`DataInHandover`). Con la variante, `CassetteService`
elige el motor de la beta siempre que DATA IN se conduce, compartido o exclusivo (`fujiEngineChosen`). Hoy no se
conduce: `kTapeOutput = false`.

1. **Traspaso.** `FujiTapeRunner` sigue el mismo protocolo que `CassetteTick`. La cinta solo avanza mientras la
   casetera tiene la línea. *Take*: confirma `Tape` cuando la línea está libre. *Release*: se responde entre dos
   pasos; antes se detiene el paso en curso. Luego la línea vuelve a la UART2: `Disk`, o `Released` si no pudo.
2. **Detener un paso.** El registro o la tirada en curso se detiene si la tarea SIO pide la línea o si espera un
   comando que no es `cas status`. El hueco y cada espera de la línea lo comprueban cada 1 ms. El motor del
   responsable lo comprueba con su propia llamada `motor_dropped`, cada 100 ms. El registro vuelve a sonar entero
   desde su principio cuando vuelve la línea, a la velocidad de la cinta.
3. **Fin de cinta.** `FujiTapeRunner` publica el fin en el mismo `TapeProgress`. Nunca lo publica mientras la
   línea pueda llevar aún bytes de la cinta tras una devolución.
4. **`cas status`.** Lee la posición de `FujiTapeRunner` (`TapePosition`), no la de `CassettePlayer`. Durante un
   paso se responde al momento, sin detenerlo.
5. **Esperas con límite.** Si la línea no acepta bytes ni velocidad, o no se vacía, en `kStallMs` (2 s) más el
   tiempo que piden sus bytes, el runner se detiene (falla cerrado). No suena nada más hasta una cinta nueva o un
   nuevo *Take*. `cas status` dice `line stalled`.

## 3. Supuestos y diferencias

| Id | Tema | Beta | RetroBridge | Estado |
|---|---|---|---|---|
| D1 | Cuándo se activa el casete | Lo decide el bus SIO de FujiNet (`sio.cpp`), **no leído** | MOTOR encendido activa a 600 baudios; MOTOR apagado entre registros desactiva y la cinta guarda su sitio; tras el final espera MOTOR apagado y encendido | **[I]** supuesto |
| D2 | Nuevo montaje, expulsión, rebobinado | Del bus y de la interfaz web de FujiNet | Cada uno sube la generación de `CassetteCommands`; el motor vuelve al inicio | **[I]** |
| D3 | Pausa de 1 ms | `delay_microseconds(999)` en espera activa | `vTaskDelay(pdMS_TO_TICKS(1))` con `CONFIG_FREERTOS_HZ=1000`: la primera espera puede durar menos de 1 ms y otra tarea puede alargar alguna | **[I]** la pausa total difiere en menos de 1 ms más la latencia del planificador |
| D4 | Registro cortado (cabecera dice más bytes que quedan) | Envía lo que lee, a veces con el búfer anterior | Termina la cinta sin enviar nada del registro | **[V]** decisión propia |
| D5 | Cabecera incompleta al final | Lee lo que hay | Exige 8 bytes; si no, fin de cinta | **[V]** decisión propia |
| D6 | `_record_ms` de un `fsk ` | `gap + waveform_ms`, con `gap` ya en 0 tras la espera | `waveform_ms`. Solo informativo: no cambia la señal | **[V]** |
| D7 | Modo compartido (la UART2 es también del disco) | La UART del SIO es del bus; el bus decide | El protocolo de traspaso de RetroBridge (§2.1). Corregido tras la revisión de la PR #122: antes el motor no se usaba en compartido y era inalcanzable en menu-sio | **[V]** en el host; **[P]** en placa |
| D8 | Rebobinar por segundos (PR #1722) | Existe | Diferido, como en la spec `cassette-cas` | **[P]** |
| D9 | ESP-IDF | La versión de la beta | v6.1: sin `io_loop_back`/`io_od_mode`; `hal/uart_periph.h`; `SOC_RMT_SUPPORT_TX_ASYNC_STOP` = 0; `SOC_RMT_CHANNELS_PER_GROUP` = `RMT_LL_CHANS_PER_INST`; `rmt_ll_tx_get_mem_blocks` leído de `conf0.mem_size` | **[P]** que el canal 0 reciba 8 bloques en v6.1: sin verificar en placa |
| D10 | Pruebas originales del responsable (`CasFSKTests` y otras) | Existen en FujiNet | No se ejecutaron. Se escribieron pruebas propias que comparan toda la salida con `CasIndex` y `FskWave` | **[P]** |
| D11 | Puerta del empaquetador | — | `check_sio_calls` no cuenta `components/rb_fujinet_fsk/`. A cambio, todo paquete exige el motor apagado en `sdkconfig` y ausente de `.symtab` (`FUJINET_FSK_CHECK`); un ELF ilegible se rechaza | **[V]** `test_firmware_package` y 4 mutaciones |
| D12 | Trozos de un registro `data` | 256 bytes por escritura | Hasta 256 bytes por `flushOutput`, entregados en porciones de 16: en compartido `EspDataIn::send` solo acepta menos de 32 bytes en vuelo. En la línea los bytes salen seguidos | **[V]** `test_fuji_tape_runner` |
| D13 | Pausa interrumpida por la línea | Solo MOTOR, con 1000 ms de gracia | La petición de la línea o un comando detienen la pausa al siguiente ms, sin gracia. La gracia de 1000 ms sigue igual para MOTOR | **[V]** `test_fuji_tape_loop`, `test_fuji_tape_runner` |
| D14 | Tiempo hasta devolver la línea | — | Un registro: lo que tardan en salir los bytes en vuelo (≤ 32 a 600 baudios, 0,53 s). Una tirada FSK: ≤ 100 ms, la consulta del motor del responsable. El árbitro espera 600 ms. **Excepción:** al precargar una tirada corta en PSRAM, el código del responsable no consulta MOTOR. Si la tarea SIO pide la línea entonces, puede vencer el plazo; el árbitro pasa a `Fault` y el disco queda aparcado (falla segura) | **[V]** FSK en `test_fuji_shared`; **[P]** precarga en placa |
| D15 | Comandos durante un paso | El bus no los recibe mientras suena | `cas status` se responde al momento. Cualquier otro comando detiene el paso y se ejecuta entre pasos; el registro vuelve a sonar entero | **[V]** `test_fuji_tape_runner` (reglas); **[P]** colas de FreeRTOS sin prueba en el host |
| D16 | El primer registro detenido por RetroBridge | La regla del offset 0 desactiva el casete | No se aplica a una parada de RetroBridge (línea o comando): el registro vuelve a sonar sin esperar MOTOR apagado y encendido | **[V]** `test_fuji_tape_runner` |
| D17 | Velocidad que restaura un *Take* | No existe (no hay traspaso) | Activar el casete pone la UART a 600 y la velocidad de la cinta pasa a 600, solo si la UART lo aceptó. Así un *Take* tras una cinta nueva, o tras MOTOR apagado y encendido, restaura 600 y no la velocidad anterior. Corregido tras la revisión de la PR #122, ronda 2 (R2-H4) | **[V]** `test_fuji_tape_runner` (4: cinta nueva, rebobinado, MOTOR apagado y encendido, activación rechazada), 2 mutaciones caen |

## 4. Puertas que no cambian

- H4 sigue sin aprobar. `kTapeOutput = false` en `main/menu_sio.cpp`: DATA IN no se conduce y el motor no suena.
- El motor solo arranca si DATA IN se conduce (exclusivo, o compartido y prestado) y su memoria está lista.
  Con `kTapeOutput = false` no se conduce: en el firmware actual sigue sin sonar.
- La variante no se empaqueta ni se flashea. La CI comprueba que `cassette_fsk_play_run` solo está en el ELF de
  `build-cas-fujinet` y no en `build`, `build-fujinet` ni `build-menu-sio`.
- La placa FN32ROV-1.3-Q24 sigue original. No se usó COM3, el 65XE ni GPIO.

## 5. Pruebas

| Prueba | Qué demuestra |
|---|---|
| `test_fuji_tape_loop` (12) | Karate y Alien Ambush registro a registro contra `CasIndex`: tipo, baudios, pausas, bytes; una petición de parada en el último segundo de la pausa. 5 mutaciones caen, más 2 de la revisión |
| `test_fuji_tape_runner` (20) | Reglas de D1 y D2; UART ocupada; traspaso (Take, Release a mitad de registro, fin solo con la línea devuelta); porciones de 16 bytes; línea que no acepta nada y velocidad rechazada (falla cerrado); comando en espera; `stopNow`; velocidad que restaura un Take tras cinta nueva, rebobinado o MOTOR (D17). 3 mutaciones caen, más 11 de la ronda 1 y 2 de la ronda 2 |
| `test_fuji_shared` (2) | H1 y H2 con la cadena real: `EspDataIn` compartido, `DataInHandover`, `TapeProgress` y el código del responsable. Toda la cinta y el fin publicado; un Release durante una tirada FSK se confirma `Disk` en menos de 600 ms y la tirada vuelve a sonar entera |
| `test_cassette_commands` | `cas status` sigue al motor que suena (`TapePosition`). 1 mutación cae |
| `test_esp_fsk_engine` (7) | Código del responsable sobre fakes de ESP-IDF: cada byte de UART y cada porción RMT de Karate, Alien Ambush y Missile Command (progresiva); MOTOR cae (pin a la UART con el canal activo, `RMTMEM[0] = 0`, un reinicio de puntero); sin memoria; tirada corta. 5 mutaciones caen |
| `check_fujinet_fsk` y sus 9 autopruebas | Los archivos del responsable siguen idénticos al manifiesto, salvo el cambio declarado. **Límite:** el manifiesto está en esta misma PR; la puerta no descarga el repositorio del responsable. Que el manifiesto refleje `cf3382f` se comprobó a mano al copiar |
| `test_firmware_package` | D11 |
