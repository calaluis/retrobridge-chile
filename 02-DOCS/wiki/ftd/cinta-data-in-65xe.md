---
author: Calaluis
branch: fix/cinta-data-in-65xe
status: in-progress
---
# La cinta no carga en el 65XE: la toma de DATA IN vence (T9b, depuración)

## Intent

Que `international_karate.cas` cargue en el 65XE con `menu-sio-cinta`. Primero, medir por qué no carga.

## Evidencia de la prueba física (2026-11-26, `menu-sio-cinta` `47e5f56`)

- Menú en el 65XE: sí. `C: INTERNATIONAL_KARATE.CAS` montada. Arranque con START+OPTION: BOOT ERROR.
- Consola: `cambio 1` (TakeRequested) → `cambio 4` (TakeTimedOut) → `cambio 6` (ToDisk), 369 veces en 47 s.
  La toma vence a los 20 ms (`kTakeTimeoutUs`). Después, una toma se confirmó y la cinta llegó al
  segmento 36 de 213; con MOTOR apagado la línea siguió en la cinta, y el menú dijo «EL HOST NO
  RESPONDE». MOTOR apagado solo pausa y conserva la cinta (`givesBack()`: hace falta además COMMAND bajo
  o fin de cinta); no está demostrado que sea un defecto.
- `rmt: GPIO 21 is not usable, maybe conflict with others`: aviso de ESP-IDF por el pin ya reservado por la
  UART; `rmt_new_tx_channel` no falló. No se trata como causa.
- Logs fuera del repo: `D:\GitHub\Respaldos\FujiNet-RB-menu-sio-cinta-47e5f56\consola-*.log`.

## Medición con el diagnóstico (2026-11-26, `e286c35`)

- 282 tomas, todas vencidas a los 21-22 ms. En cada una: 1 consulta a `hardwareFree()`, 0 negativas,
  bits `0x00`: la casetera vio la línea libre. Según el código (`CassetteTick::answer()`), con la línea
  libre confirma en ese mismo tick; el instante del ACK no se registró. MOTOR on, COMMAND alto.
- Causa: con MOTOR encendido el 65XE solo escucha, no manda COMMAND. La tarea SIO espera entonces su
  poll completo (`SioSession::kPollTimeoutUs` = 20 ms) y vuelve a mirar a ~21,5 ms. El árbitro mira el
  plazo (`kTakeTimeoutUs` = 20 ms) antes que la confirmación: la descarta y la toma vence.
- Logs fuera del repo, sin datos de red.

## Prueba física de la corrección (2026-11-26, `d1eb418`)

- Pantalla del 65XE: «PROGRAMA CARGADO» con `INTERNATIONAL_KARATE.CAS` en `C:`, START+OPTION y RETURN.
- El juego corre (confirmado por el responsable en el 65XE).
- Consola: `cambio 1` → `cambio 2` (ToTape) a los 22 ms, vencidas 0. La cinta tuvo la línea de 22:43:52 a
  22:50:44 (~7 min, 106 arranques del canal RMT, uno cada ~3,7 s), MOTOR on, COMMAND alto.
- Después: MOTOR off, COMMAND alto, dueño cinta. Según `givesBack()`, es una pausa: la línea vuelve al disco
  con COMMAND bajo o al fin de la cinta. Falta ver qué pasa cuando el Atari vuelve a usar el disco.
- Log fuera del repo, sin datos de red.

## Ampliación pedida por el responsable (2026-11-26)

La PR #124 entrega la cinta completa: carga, mensaje de carga del menú, y montar y desmontar `.cas`
en `C:` también después de una carga.

- Mensaje «Cargando RetroBridge...»: D1 empieza con un cargador en la página 6 (`LOADER_ORG` $0600, 2
  sectores). El sistema del Atari carga solo esos sectores; el cargador escribe el texto en la pantalla
  del sistema (`SAVMSC`) y lee el bloque del menú a $0700, igual que antes, sector por sector. Un sector
  que no se lee vuelve al arranque en frío. El bloque del menú no cambia (D1: 20 sectores, antes 18).
- Montar y desmontar en `C:`: ya existe (elegir el `.cas` y C; U y luego C). Un `.cas` en D1-D8 ya se
  rechaza con «UNA CINTA VA EN C:» (AC3). El «EL HOST NO RESPONDE» de D8 no era ese rechazo: la placa no
  contestó, con la línea todavía en la cinta tras la carga cortada (issue #125). Se mide en el 65XE.

## Issue #125: «EL HOST NO RESPONDE» — medición (2026-11-27)

Prueba física con `60ac6e4` (responsable, 65XE, USB y SIO conectados): «Cargando RetroBridge...» se ve;
montar y cargar `.cas` desde la microSD funciona, también con carga Turbo; A + OPTION + START funciona;
un `.cas` en D8 se rechaza. **Falla:** apagar y encender el 65XE, luego U y C: «EL HOST NO RESPONDE»;
tras Esc, C: ya está desmontado (la placa ejecutó la orden). También pasó, intermitente, al arrancar.

**Procedimiento reproducible:** USB y SIO conectados; C: con un `.cas`; apagar el 65XE 3 s y encender;
en el menú, U y C. Al ver el error, dejar el 65XE encendido y leer `sio status` por la consola (sin
reiniciar la placa); después apagar el 65XE para el resumen.

**Demostrado:**
- Bisección: con `d1eb418` la misma secuencia falla igual. El cargador (`10a1a92`) y `70abb98` no lo causan.
- `sio status` de `60ac6e4` tras usar el menú: `err_tx 3`, `reintentos 3`, `ordenes 11`, `err_rx 0`,
  `cortas 11`. Un envío de la respuesta del menú falló tres veces; tras cada fallo el Atari repitió la orden.
- `SioSession` no ve esos fallos: las órdenes del menú se sirven en `MenuSioTransport::serveMenu`, que
  corta la respuesta si un bloque no sale (`menu_sio_transport.cpp`) y descarta sin contar una trama con
  checksum malo. Por eso el diagnóstico de `f6426ec` no mostró pasos fallidos.
- Una respuesta del menú son 964 bytes (COMPLETE, 962 de pantalla, checksum): a 19200 baudios, ~502 ms
  de línea. Los pasos de 337-553 ms de la consola eran esas respuestas, no esperas.

**Hipótesis (sin demostrar):**
- H1: la UART está aparcada o el TX no termina a tiempo (`EspSioUart::write`) cuando la cinta restaurada
  de C: actúa tras el encendido. `33e72ee` cuenta cada causa (`uart tx: no_lista / escritura_corta /
  sin_terminar`).
- H2: el Atari falla tras agotar sus reintentos porque la placa está ocupada (respuesta de ~0,5 s más el
  resto del bucle) cuando llega la orden repetida. Se ve como `reintentos` sin `envio_fallido` final.
- H3 (lectura de código, aparte): `EspDataIn::baud_` no vuelve a 0 cuando el disco recupera la línea a
  19200 (`EspSioUart::resume`); una segunda cinta en la misma sesión podría salir a 19200. Sin prueba.
- La transferencia de 964 bytes sola no supera el plazo del menú (`DTIMLO` 7 s, ~0,5 s de línea).

**Propuesta (para revisión, sin aplicar):** según el contador que suba con `33e72ee`:
- `no_lista`: el menú no responde con la línea prestada; devolver DATA IN al disco antes de servir la
  orden, o no aceptarla (sin ACK) para que el Atari la repita.
- `sin_terminar`: medir la cola real del TX (bytes pendientes del anillo compartido) en vez de solo el
  bloque, y revisar la tasa de la UART tras la cinta (H3).
- solo `reintentos`: reducir el trabajo del bucle entre respuestas; el protocolo de pantalla (enviar solo
  las filas cambiadas) queda para su propia spec.

## Scope

- Paso 1 (este commit): diagnóstico que solo registra. `EspDataIn::hardwareFree()` guarda qué condición
  estaba ocupada (bits) y cuántas veces dijo que no; `CassetteService` guarda el último tick; `menu_sio`
  registra cada toma (confirmada o vencida) con los deltas de esa toma, y cada 2 s mientras la línea
  no es del disco.
- Paso 2: con la medición, prueba RED en el host y corrección: `kTakeTimeoutUs` = 60 ms, y
  `main/menu_sio.cpp` exige en compilación que cubra dos polls SIO completos (más 5 ms de trabajo cada uno).
  La regla de §2.4 no cambia: una confirmación en el plazo o después se descarta.

Fuera: H4, las puertas y los paquetes. Flashear y probar en el 65XE necesitan aprobación aparte.

## Checklist

- [x] Rama `fix/cinta-data-in-65xe` desde `main` `47e5f56` — prueba: `git log`.
- [x] Diagnóstico sin cambios de decisión — prueba: mismo orden de condiciones en `hardwareFree()`;
  atómicos de 32 bits (sin libatomic).
- [x] Revisión independiente, ronda 1 — NO FUSIONAR: H1 (el estado ocupado no era de la toma que venció).
- [x] H1 — prueba: `test_data_in_diag` (7): sin consulta nueva, con bit ocupado, libre sin confirmar, toma
  confirmada sin pedido (N/D), contadores que dan la vuelta. 3 de 3 mutaciones caen. Contadores de 32 bits
  con un solo escritor; `check_atomic_calls` revisa `esp_data_in.cpp` y `cassette_service.cpp` en la CI.
- [x] Ronda 2 — H2: la referencia se tomaba después de `g_arbiter.step()`, que ya publicó la toma; una
  consulta en ese intervalo se perdía. Prueba: `stepWithTakeBaseline` (2 pruebas nuevas, 9 en total);
  la mutación «referencia después del paso» cae. `lastBusy` queda como orientativo (tres atómicos sueltos).
- [x] CI del diagnóstico y paquete `menu-sio-cinta` — prueba: run 45918286814, 6/6; paquete `e286c35`
  verificado (sha256 `ac8ea9b3…`).
- [x] Medición en el 65XE (con aprobación) — prueba: consola de `e286c35`, sección «Medición».
- [x] RED — prueba: `test_data_in_handover`, 2 pruebas nuevas fallan con 20 ms (toma confirmada a los
  2 ms y paso SIO de 21,5 ms → `TakeTimedOut`; el plazo no cubre dos pasos SIO).
- [x] GREEN — prueba: con 60 ms, `ctest` 109/109; `tools/tests` 341 OK; layering, notices, fsk OK.
- [x] CI de la corrección y paquete `menu-sio-cinta` — prueba: run 45921940608 en `d1eb418`, 6/6; paquete
  verificado (sha256 `48077aaa…`). Los commits posteriores solo tocan este documento.
- [x] Prueba en el 65XE: `international_karate.cas` carga (con aprobación) — prueba: «PROGRAMA CARGADO»
  en pantalla y el juego corre (lo vio el responsable), consola de `d1eb418` (sección «Prueba física»).
  Evidencia del responsable, no verificación independiente.
- [x] Revisión independiente, ronda 5 (`7863d59`): sin bloqueante de firmware; pide coherencia documental,
  CI del HEAD y seguimiento explícito del incidente.
- [x] Revisión independiente, ronda 4 (`d1eb418`): sin bloqueante en el delta; no es prueba de carga.
- [ ] Seguimiento aparte, fuera de esta PR — incidente «EL HOST NO RESPONDE» (prueba de `47e5f56`, tras el
  segmento 36): contrastar con la especificación las transiciones reales (COMMAND, `tapeEnded`, dueño del
  árbitro, Release) con los registros `DATA IN diag:`. Criterio de aceptación: tras una carga, con MOTOR
  apagado, la primera orden del 65XE a una unidad (COMMAND bajo) devuelve la línea al disco (`cambio 6`)
  y esa unidad responde. El plazo de 60 ms no lo corrige por sí solo.
- Fuera de alcance: H4 y la medición de GPIO36 siguen pendientes; ni la CI ni esta carga son aprobación
  eléctrica.
- [x] Mensaje de carga — prueba: `test_menu_program` `MenuLoaderTest` (4): el texto está en pantalla antes
  de la primera lectura; el cargador lee los sectores 3-20 a $0700 en orden y arranca el menú; un sector
  que falla vuelve al arranque en frío; D1 es el cargador y luego el bloque del menú sin cambios. RED
  (`disk_block` no existía) → GREEN; `tools/tests` 345 OK; `ctest` 109/109.
- [ ] CI y paquete con el cargador; flasheo (aprobado por el responsable) y prueba en el 65XE: el mensaje
  se ve; cargar el juego, apagar y encender, desmontar `C:` (U, C), montar otro `.cas` en `C:`; `.cas` en
  D8 dice «UNA CINTA VA EN C:».
- [x] Limpiar `TakeDiag` al cancelar una toma (`TakeFailed` o `ReleaseRequested`) — prueba:
  `test_a_cancelled_take_leaves_no_baseline`; con `cancelled()` vacío falla (mutante muerto); 10/10.

## Next

Alcance de esta PR: diagnóstico y corrección del plazo de la toma. El responsable decide la fusión. El
incidente de cesión de DATA IN se sigue aparte, con su criterio de aceptación.
