# rb_fujinet_fsk — procedencia

Este componente contiene el motor FSK de la beta del responsable, tal como está en FujiNet, y los adaptadores
que RetroBridge escribió para que funcione aquí (ADR-012, enmiendas 1 y 2).

## Origen fijado

- Repositorio: `calaluis/fujinet-firmware`.
- Etiqueta: `v1.6.2-atari-fsk-beta.1`.
- Commit: `cf3382fafeed9b57e307b8618d7ba7173e7e6ec9` (fusión del PR #1773). No se usó `master`.
- El commit contiene en su historia `fbc304b` (PR #1804). El PR #1773 ya integra la parada del #1804
  (`CassetteFSKEncode::stop_req` y el aborto por `RMTMEM`).

## Archivos del responsable (código propio, ADR-012 enmienda 2)

Todos los archivos de `lib/` que no son cabeceras de adaptación son del responsable. Ninguno tiene otro autor
humano. `fuente.sha256` guarda, por archivo, el SHA-256 en el commit y el SHA-256 aquí (finales de línea LF).
`tools/check_fujinet_fsk.py` lo comprueba en la CI.

| Archivo | PR de origen | Cambio aquí |
|---|---|---|
| `lib/media/atari/casTape.h` | #1722 | Ninguno |
| `lib/media/atari/casFSK.h`, `casFSK.cpp` | #1756; cambiados en #1773 y #1804 | Ninguno |
| `lib/media/atari/casFSKLoader.h`, `casFSKLoader.cpp` | #1773 | Ninguno |
| `lib/device/sio/cassetteFSK.h` | #1756; cambiado en #1773 y #1804 | Ninguno |
| `lib/device/sio/cassetteFSK.cpp` | #1756; cambiado en #1773 y #1804 | `idf61-rmt-flags` (abajo) |
| `lib/device/sio/cassetteFSKLoader.h`, `cassetteFSKLoader.cpp` | #1773 | Ninguno |
| `lib/device/sio/cassetteFSKRead.h`, `cassetteFSKRead.cpp` | #1773 | Ninguno |

Los archivos de `#1773` son idénticos byte a byte a la cuarentena `D:\rb-e0\fsk-propio\nuevos\pr1773`.

### Cambio `idf61-rmt-flags`

ESP-IDF v6.1 quitó `rmt_tx_channel_config_t::flags.io_loop_back` y `flags.io_od_mode`. Las dos líneas que los
ponían a `false` se quitaron. Las dos opciones ya valen `false` por defecto, así que la configuración del canal
no cambia. SHA-256 en el commit: `6a9f3b94…`; aquí: `19f51985…` (valores completos en `fuente.sha256`).

## Adaptadores (escritos para RetroBridge)

Sustituyen a cabeceras y clases de FujiNet que no se copian. Cada uno lleva el mismo nombre que la cabecera que
el código del responsable incluye.

| Archivo | Sustituye a | Qué hace |
|---|---|---|
| `adapt/global_types.h` | `global_types.h` de FujiNet | `success_is_true` y las dos macros de retorno |
| `adapt/fnio.h`, `src/fujinet_adapt.cpp` | `fnio.h` y `fnFile` | Lee la cinta montada (`TapeSource`) con `fseek`, `fread` y `ftell` |
| `adapt/bus.h` | `bus.h` (`SYSTEM_BUS`) | Solo `flushOutput()`, que espera a que la UART2 se vacíe |
| `lib/include/debug.h` | `debug.h` | `Debug_printf` a `ESP_LOGI`; nada en el host |
| `lib/include/pinmap.h` | `pinmap.h` | `PIN_UART2_TX`: el pin de DATA IN del perfil de placa |
| `adapt/soc/uart_periph.h` | `soc/uart_periph.h` | Redirige a `hal/uart_periph.h` (v6.1) |
| `adapt/rb_fujinet_idf6.h` | — | Define lo que v6.1 ya no da: `SOC_RMT_SUPPORT_TX_ASYNC_STOP` (0), `SOC_RMT_CHANNELS_PER_GROUP` y `rmt_ll_tx_get_mem_blocks` |
| `src/fuji_tape_loop.cpp` | `sioCassette::send_FUJI_tape_block` | Un registro por paso (ver el documento de diseño) |
| `src/fuji_tape_runner.cpp` | El bus SIO que activa el casete | Reglas propias de RetroBridge; traspaso de DATA IN con el disco y esperas con límite |
| `include/rb/fujinet/fuji_service_rules.hpp` | — | Cuándo `CassetteService` elige este motor y qué comando se responde durante un paso |
| `src/esp_fsk_engine.cpp` | La llamada de `cassette.cpp` a `cassette_fsk_play_run` | Prepara el cargador y el archivo, y llama al motor |

## Código de terceros

No entra código de terceros. `cassette.cpp` de FujiNet (GPL-3.0, no es del responsable) se leyó en el commit
fijado, solo para describir su comportamiento (ADR-012 enmienda 2). No se copió nada. Su papel lo cumplen
`fuji_tape_loop.cpp` y `fuji_tape_runner.cpp`, escritos para RetroBridge.

Diseño y diferencias: `docs/diseno/cassette-motor-fujinet.md`.
