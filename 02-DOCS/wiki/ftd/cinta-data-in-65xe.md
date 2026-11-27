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
- [ ] Pendientes: D8/.cas con AC3; limpiar `TakeDiag` al cancelar una toma.

## Next

Alcance de esta PR: diagnóstico y corrección del plazo de la toma. El responsable decide la fusión. El
incidente de cesión de DATA IN se sigue aparte, con su criterio de aceptación.
