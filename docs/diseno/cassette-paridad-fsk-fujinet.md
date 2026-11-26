# Paridad de la carga `.cas` con la beta FSK del responsable

- Fecha: 2026-11-26. Rama `feat/cas-paridad-fsk` desde `main` `7673980`.
- Referencia: solo el código propio del responsable (ADR-012, enmienda 1). FujiNetWIFI/fujinet-firmware PR #1722,
  #1756, #1773 y #1804, release `calaluis/fujinet-firmware` `v1.6.2-atari-fsk-beta.1`. Copia en cuarentena:
  `D:\rb-e0\fsk-propio` (`nuevos/` y líneas `+` de `modificados/`). No se consultó el contexto ajeno de los diffs.
- Fuente oficial adicional: ESP-IDF v6.1, `components/esp_driver_rmt/src/rmt_tx.c` (la versión fijada en la CI, D-12).
- Alcance: software y pruebas del host. **Nada de esto se probó en el Atari.** H4 sigue sin aprobar,
  `kTapeOutput = false`, T9b bloqueado.

Etiquetas: **[V]** verificado (prueba del host o lectura de código citada), **[I]** inferido, **[P]** pendiente.

## 1. Matriz de paridad

| Área | Referencia (beta) | RetroBridge antes | Diferencia | Pruebas que existían | Pruebas nuevas | Estado |
|---|---|---|---|---|---|---|
| Formato A8CAS: cabeceras, `baud`, `data` | Cabeceras de 8 bytes; `baud` fija la velocidad; 600 por defecto (`casTape.h`, PR #1722) | `CasIndex` (C101) | Ninguna | `test_cas_index` (23), `international_karate.cas` y todas las cintas de `RB_CAS_SAMPLES` | — | **[V]** paridad |
| Tirada `fsk `: unión de bloques | Bloque `fsk ` más los siguientes con pausa 0, como máximo 64; longitud impar inválida (`fsk_scan_run`, PR #1756) | Igual (C101) | Un bloque de continuación mal formado: la beta termina la tirada ahí (cargador) o la rechaza (precarga); RetroBridge rechaza la cinta al montarla. Ninguna cinta real lo tiene | `test_cas_index` | — | **[V]** paridad en cintas reales; diferencia D6 registrada |
| Niveles y duraciones FSK | Índice par = espacio, impar = marca, sobre toda la tirada; 1 valor = 100 µs; porciones ≤ 32 767 µs; un 0 gasta su índice (`fsk_pull_portion`) | `FskWave` (C102) | Ninguna | `test_fsk_wave` (10) | Karate completa por salida en tiempo real: cada porción (nivel y duración) y cada byte, en orden | **[V]** paridad |
| Símbolos RMT | 1 MHz, 512 símbolos, cola 1, dos porciones por símbolo, cola impar con `duration1 = 0` (PR #1756) | `FskSymbolFeed` + `esp_data_in` (C107) | Fin de transmisión en marca (`eot_level = 1`; la beta usa 0). Decisión propia ya registrada en C107 | `test_fsk_symbol_feed`, `test_esp_data_in` | — | **[V]** paridad salvo D4 |
| Pausas y cambios de baudios | Pausa en ms antes de cada registro; 10 bits por byte (`cas_data_record_ms`) | `CassettePlayer` (C104) | Ninguna en la cuenta. Dónde espera la beta la pausa de un `fsk ` depende de `cassette.cpp`, que es código ajeno: no se consultó | `test_cassette_player` (registros, Alien Ambush) | Karate: 600 y 750 baudios, bytes idénticos | **[V]** registros; **[P]** pausa `fsk ` de la beta (D3) |
| Carga progresiva: puerta | 20 s de señal y 1536 valores, o tirada final (`fsk_run_start_gate`) | Igual | — | `test_fsk_loader` | — | **[V]** |
| Carga progresiva: cuándo | Solo si el primer bloque tiene ≥ 16 384 bytes; si no, precarga entera (`fsk_progressive_wanted`, PR #1773) | Progresiva para cualquier tirada | **D2, corregida** | — | `test_small_first_chunk_waits_for_the_whole_run`, `test_first_chunk_of_16384_bytes_starts_before_final` | **[V]** corregida |
| Subdesbordamiento (el cargador o la cola se quedan atrás) | La transmisión termina (`FSKNext::underrun`), la tirada se repite entera desde el principio (`cassette.cpp` propio: «replay the same run») | La señal se reanudaba a mitad de tirada (`underruns_++`) | **D1, corregida** | `test_a_restart_on_the_same_channel_waits_for_its_own_end` (comprobaba la reanudación) | Tres pruebas del reproductor, una de `esp_data_in` y Missile Command real con un corte | **[V]** corregida |
| Reintentos de lectura | 3 reintentos; el 4.º fallo seguido falla la tirada | Igual | — | `test_fsk_loader` | — | **[V]** |
| Fallo del cargador antes de la puerta | Reanuda desde su cursor; si no, precarga | Vuelve a cargar la tirada desde el principio | Más lecturas; la señal es la misma | `test_cassette_player` | — | **[I]** equivalente en la señal (D5) |
| Fallo del cargador con la señal en marcha | Subdesbordamiento: la tirada se repite | `restartRun` | Ninguna | `test_loader_failure_mid_run_replays_the_run` | — | **[V]** |
| MOTOR cae con la señal en marcha | Sin más símbolos, DATA IN a marca, se aborta el RMT; la tirada se repite (PR #1804) | `WaveOut::stop()` y `restartRun` | Orden: la beta devuelve el pin a la UART y luego aborta; RetroBridge aborta y luego devuelve el pin (como mucho, el símbolo en curso, ≤ 65 ms) | `test_motor_drop_in_a_run_stops_and_replays_it`, `test_esp_data_in` | — | **[V]** lógica; **[P]** efecto físico (D8) |
| Aborto del RMT en el ESP32 clásico | Escribe EOF en `RMTMEM` a mano (IDF de la beta) | `rmt_disable()` | En ESP-IDF v6.1, `rmt_tx_disable()` llena de ceros la memoria del canal y espera TX_DONE (`rmt_tx.c:835-840`) | `test_esp_data_in` | — | **[V]** equivalente por la fuente de IDF (D7) |
| MOTOR cae mientras espera la puerta | La espera se corta y la tirada se repite desde su inicio | Se congela; el cargador sigue | Nada sale por DATA IN en ninguno de los dos | `test_motor_off_while_waiting_keeps_loading` | — | **[I]** sin efecto en la señal (D3) |
| Cancelación (expulsar, rebobinar) | Parar el cargador antes de cerrar el archivo (`stop_fsk_loader`, PR #1773) | `cutFsk`: `requestStop` y `WaveOut::stop` | Ninguna | `test_rewind_during_a_run_stops_the_signal`, `test_restart_waits_for_the_producer_to_stop` | — | **[V]** |
| Fin de cinta | Código ajeno (`cassette.cpp`) | `ended()`: último tramo hecho, bytes fuera y señal en reposo | No comparable sin código ajeno | `test_ended_*` | — | **[P]** |
| Rebobinar por segundos | `cassetteTrail` (PR #1722) | No existe | Decisión diferida en la spec `cassette-cas` («spec propia») | — | — | Fuera de alcance |

## 2. Diferencias corregidas

### D1. Subdesbordamiento: la tirada se repite, no se reanuda

- Referencia: cuando falta el siguiente valor, `FSKPublishedValues::next` devuelve `underrun`, el codificador termina
  la transmisión y `cassette_fsk_stream_run` devuelve `CassetteFSKStatus::underrun`. El `cassette.cpp` propio
  (PR #1756) repite entonces la misma tirada desde su inicio.
- Antes: `EspDataIn::pump()` volvía a transmitir lo que llegaba tarde, a mitad de la tirada. El Atari recibía una
  señal con un hueco en marca en medio.
- Ahora:
  - `EspDataIn::pump()` corta la tirada: descarta lo que llegó tarde, devuelve el pin a la UART (marca) y cuenta
    `underruns()`.
  - `CassettePlayer`: si una salida en tiempo real (`WaveOut::realTime()`) queda en reposo antes de recibir toda la
    tirada, la tirada se repite desde su pausa (`fskUnderruns()`).
  - Una tirada cuyos valores ya se enviaron todos está completa aunque el cargador aún no diga `Final`. Así un fin
    normal no se confunde con un corte.
  - La salida de descarte (`kTapeOutput = false`) no es en tiempo real: allí no hay cortes.
- Diferencia de arquitectura que queda: RetroBridge tiene una cola entre el bucle y la interrupción (16 KB, ≥ 0,4 s).
  Un bucle de la casetera que se atrase también puede vaciarla. Ahora ese caso se trata igual que en la beta.

### D2. Solo un primer bloque de 16 384 bytes o más empieza antes de cargarse entero

- Referencia: `fsk_progressive_wanted(first_chunk_bytes)`; si no, `cassette_fsk_play_preloaded` carga la tirada
  entera antes de transmitir.
- Antes: la puerta de 20 s y 1536 valores valía para cualquier tirada.
- Ahora: `FskLoader::startGate()` exige la tirada final si el primer bloque tiene menos de
  `kProgressiveFirstChunkBytes = 16384`.
- Efecto en cintas reales: ninguno. Todas las tiradas de varios bloques de `RB_CAS_SAMPLES` empiezan con un bloque de
  65 532 bytes, y las de un bloque pequeño ya esperaban a ser finales (véase §4).

## 3. Pruebas

RED → GREEN, compilación del host con MSYS2 UCRT64 (GCC), `RB_CAS_SAMPLES=D:\GitHub\Respaldos\MicroSD`:

| Prueba | Antes (rojo) | Después |
|---|---|---|
| `test_live_signal_running_dry_mid_run_replays_the_run` | `Expected 1 Was 0` (sin repetición) | PASS |
| `test_live_run_pushed_whole_before_final_is_not_an_underrun` | `Expected 4 Was 3` (no llegaba a `FskDrain`) | PASS |
| `test_real_missile_command_underrun_replays_the_whole_run` (cinta real) | `Expected 1 Was 0` | PASS |
| `test_small_first_chunk_waits_for_the_whole_run` | `Expected 0 Was 1` (la puerta se abría) | PASS |
| `test_portions_after_the_rmt_ran_dry_cut_the_run` | `Expected 1 Was 2` (segunda transmisión) | PASS |
| `test_first_chunk_of_16384_bytes_starts_before_final` (borde) | — | PASS |
| `test_idle_output_that_is_not_live_is_never_an_underrun` (control) | PASS | PASS |
| `test_real_international_karate_live_plays_every_portion` (cinta real) | PASS | PASS |

Regresión: 102 de 102 binarios de `ctest` en verde, con las cintas reales presentes (0 pruebas ignoradas en
`test_rb_cassette_player`, `test_fsk_loader`, `test_cas_index`, `test_fsk_wave` y `test_esp_data_in`).

Pruebas antiguas ajustadas por D2 (el primer bloque pasa a 18 000 bytes para seguir probando la puerta progresiva; lo
que comprueban no cambia): `test_gate_waits_for_runway_and_prefill`, `test_gate_needs_the_runway_too`,
`test_fsk_waits_for_the_start_gate`, `test_fsk_follows_the_loader`, `test_loader_failure_mid_run_replays_the_run`,
`test_restart_waits_for_the_producer_to_stop` y `test_switch_while_waiting_for_the_gate_cuts_nothing`.
`test_a_restart_on_the_same_channel_waits_for_its_own_end` se sustituye por
`test_portions_after_the_rmt_ran_dry_cut_the_run`: comprobaba justo el comportamiento que D1 elimina.

## 4. Cintas reales (forma de las tiradas)

| Cinta | Bloques | Tiradas `fsk ` | Tirada mayor (primer bloque, bloques, bytes) |
|---|---|---|---|
| `international_karate.cas` | 107 `fsk `, 106 `data`, 2 `baud` | 107 | 4, 1, 4 |
| `PAL_ALIEN_AMBUSH_TURBO_SOFTWARE.cas` | 49 `fsk `, 51 `data` | 49 | 4, 1, 4 |
| `NTSC_BRUCE_LEE_TURBO_SOFTWARE_FUJINET.cas` | 331 `fsk `, 335 `data` | 331 | 4, 1, 4 |
| `turbo_software_missile_command.cas` | 2 `fsk ` | 1 | 65 532, 2, 111 452 |
| `turbo_software_zorro.cas` | 8 `fsk ` | 1 | 65 532, 8, 504 244 |
| `tt_international.cas` | 7 `fsk ` | 1 | 65 532, 7, 446 264 |

Ninguna tiene bloques `pwm*` ni una tirada de varios bloques con un primer bloque menor de 16 384 bytes.

## 5. Diferencias que siguen abiertas

- **D3 [P].** Dónde espera la beta la pausa de un `fsk `, y si una caída de MOTOR antes de la señal la repite. Vive en
  `cassette.cpp`, código ajeno: no se consultó (ADR-012).
- **D4 [V].** Fin de transmisión en marca (`eot_level = 1`), decisión propia ya registrada (C107).
- **D5 [I].** Tras un fallo antes de la puerta, RetroBridge recarga desde el principio; la beta reanuda desde su
  cursor. Misma señal, más lecturas.
- **D6 [V].** Un bloque de continuación mal formado: RetroBridge rechaza la cinta al montarla.
- **D8 [P].** Al caer MOTOR, el pin vuelve a la UART después del aborto del RMT, no antes. Como mucho, el símbolo en
  curso. Solo se puede medir con la señal en el Atari.
- Rebobinar por segundos (PR #1722): decisión diferida en la spec.

## 6. Lo que no está verificado físicamente

- Que RetroBridge cargue un `.cas` en el Atari 65XE. **No se afirma.** Necesita H4, `kTapeOutput = true` y una
  prueba física autorizada (T9b).
- El comportamiento del RMT real ante un corte (D1) y ante la caída de MOTOR (D8). Las pruebas usan los dobles de
  ESP-IDF del host.
- Tiempos del bucle de la casetera con tarjeta SD real.
