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

**Revisión adicional de `832b222` (M1–M3):**
- M1: `check_atomic_calls.py` cubre ahora `esp_sio_uart.cpp.obj` (perfil `uart`) y
  `menu_sio_transport.cpp.obj` (perfil `menu`) en la CI. Los almacenamientos de los contadores son
  `release` (`memw` + `s32i`). Pruebas negativas: sin `memw`, otro objeto, y una llamada a libatomic.
- M2: una respuesta cortada por un bloque fallido ya no fija `respuesta ultima/maxima`; su tiempo va a
  `cortada ultima` (`lastInterruptedUs`). Prueba RED→GREEN en `test_host_menu`.
- M3 (limitación conocida): cada contador se lee por separado; una lectura de `sio status` no es una
  instantánea de todos a la vez.

**Medición física con `3863461` (2026-11-27, autorizada por el responsable):**
- Instalación: la placa tenía `60ac6e4` (verify 3/3). Paquete `menu-sio-cinta` de la CI 46081235582, zip
  `0b0c43b7…` igual al digest del artefacto; `write_flash` y `verify_flash` 3/3. Flasheo solo con USB,
  65XE apagado y SIO desconectado; NVS sin tocar.
- Prueba (USB y SIO a la vez): el menú aparece y funciona; al montar un `.cas` con C sale «EL HOST NO
  RESPONDE» (lo vio el responsable).
- `sio status` después: `ordenes 1` (la primera pantalla, completa en 504 ms); `tramas 22 cortas 16
  command_pegado 4803`; `err_rx 0 err_tx 0`; `checksum_malo 0 ack_fallido 0 envio_fallido 0`, `cortada
  ultima 0 ms`; `uart tx: no_lista 0 escritura_corta 0 sin_terminar 0`, `escritura_maxima 133 ms`. No hubo
  línea «SIO diag» de fallo.
- Evidencia: `D:\GitHub\Respaldos\FujiNet-RB-menu-sio-cinta-3863461` (consola y `sio status`, con
  `MANIFEST.sha256`).

**Qué muestra (acotado por la revisión de `7adabf8`):** en esta captura no consta la recepción ni el
proceso de un segundo comando de menú completo, y las transmisiones que sí se intentaron no registraron
fallos. Abundan las tramas cortas y las ventanas COMMAND sin liberar. **Falta correlación temporal con la
orden C:** los contadores son acumulados desde el arranque; `cortas 16` y `command_pegado 4803` no se pueden
atribuir a esa orden ni entre sí (`SioLineTransport::waitCommandFrame` cuenta uno u otro, y vacía la entrada
antes de cada ventana).
**H1:** sin apoyo en esta ocurrencia para un `write()` fallido, porque no consta un intento de respuesta a
la orden C. No se descarta una línea aparcada (`read()` devuelve 0 con `parked_`) ni una indisponibilidad
previa al envío. La incidencia de `60ac6e4` (`err_tx 3`) es otra observación y sigue abierta.
**Préstamo de DATA IN:** el árbitro solo cede DATA IN a la cinta con MOTOR encendido y la cinta preparada;
montar con C no la presta por sí solo, y el menú se atiende antes que el árbitro en el bucle. Que esa misma
pulsación cause la falla sería circular. Queda como hipótesis un préstamo **anterior** o un estado heredado
(UART aparcada).
**SIO +5V 4780 mV (aclarado con el responsable, 2026-11-28):** no hubo contradicción. El 65XE estuvo
encendido desde que el responsable lo conectó hasta la lectura de `sio status`. El botón «Listo, apagado»
lo redactó el asistente con el sentido de «probé y apagué», y el responsable lo usó para avisar que iba a
probar. Error de redacción del asistente. Con eso cuadra todo: 4780 mV es el valor con el Atari encendido;
no salió el resumen porque no hubo apagado (`SioPowerWatch` lo exige con menos de 1000 mV durante 100 ms);
`ordenes 1` corresponde a un solo arranque; y `command_pegado` no incluye tiempo con el Atari apagado.
**El interruptor del 65XE es físico, no lógico:** corta la alimentación del equipo. No hay apagado por
software ni modo de espera; ni el firmware ni el asistente pueden encenderlo, apagarlo ni consultar su
posición. La placa solo infiere el estado por el pin +5V del SIO. Cada encendido o apagado es una acción
manual del responsable, y una instrucción debe decir de forma explícita cuándo hacerla y cuándo avisarla.
**Siguiente medición propuesta (sin aplicar):** registrar con marca de tiempo, antes y después de pulsar C,
el dueño del árbitro, `parked()`, MOTOR, COMMAND, el estado de apagado y los deltas de `frames`,
`shortFrames`, `stuckLows`, `menuCommands`, `menuRetries` y `err_rx/tx`, con los eventos Take/Release/ToDisk.
Si la consola no los expone, requiere un commit de diagnóstico acotado con RED→GREEN y revisión aparte.
**Diseño de esa medición (sin implementar):** [registro de eventos SIO](cinta-data-in-65xe-eventos-sio.md),
publicado para revisión independiente.

**Medición mínima en lugar de la cola (`aa8ee49`, decisión del responsable):** el diseño de la cola queda en
pausa. `sio status` muestra `data in: duenio …, aparcada …, lecturas_aparcada N`. `lecturas_aparcada` cuenta
los `read()` hechos con la UART aparcada, que devuelven 0 bytes.

**Cómo leer los deltas entre dos lecturas** (revisión de `aa8ee49`, M1). Una lectura aparcada termina como
trama corta solo si COMMAND se libera después; si COMMAND sigue bajo, cuenta como `command_pegado`. Lo
prueban `test_a_parked_read_with_command_released_is_a_short_frame` y
`test_a_parked_read_with_command_stuck_is_not_a_short_frame`.

| Δ`cortas` | Δ`lecturas_aparcada` | Qué se puede decir |
|---|---|---|
| > 0 | 0 | esas tramas cortas no vienen de lecturas aparcadas |
| > 0 | > 0 | hubo de las dos clases; **no** prueba que sean los mismos intentos |
| 0 | > 0 | hubo lecturas aparcadas; pudieron terminar en COMMAND pegado |
| 0 | 0 | no constan esos sucesos en el intervalo |

**Límites:** `duenio` y `aparcada` son el estado en el momento de la lectura, no un historial; no muestran
una cesión breve que ya se revirtió. Los contadores son acumulados y no llevan hora. El perfil `uart` de
`check_atomic_calls` confirma un almacenamiento con barrera en el objeto, no la secuencia de cada contador
nuevo por separado.

**Prueba física con `f93ad2e` (2026-11-27, solo USB y SIO, 65XE operado por el responsable):** el error
**no se reprodujo** en ninguna variante.

| Variante | Qué se hizo | Resultado |
|---|---|---|
| A–C | menú, montar con C, ciclos repetidos | 14 órdenes, deltas 0 tras C |
| D (variante 1) | apagar y encender el 65XE, luego U y C | 30 órdenes completas; Δcortas +2, Δpegado +1814, lecturas_aparcada 0 |
| E (variante 2) | montar `.cas`, cargar el juego entero (~4 min), apagar, encender, montar con C | 38 órdenes completas; Δcortas +2, Δpegado +3849, Δlecturas_aparcada +2 |

En todas: `envio_fallido`, `ack_fallido`, `checksum_malo`, `err_rx`, `err_tx` y fallos de UART en 0.

**Observado en E:** DATA IN pasó a la cinta en 22 ms con MOTOR encendido. Tras la carga, MOTOR se apagó y
la cinta **siguió siendo dueña de DATA IN unos 63 s**; volvió al disco solo al apagar el 65XE (cambios 5 y
6). Cada apagado registra pasos de transporte fallidos (13 en D, 10 en E) justo antes de «65XE off».

**Regla del equipo (responsable):** tras cargar un juego en RAM, la única forma de volver al menú es
apagar y encender el 65XE con su interruptor físico. Por eso el menú no se usa mientras la cinta tiene
DATA IN: el apagado se la devuelve al disco.

**Acotado:** las tramas cortas y los COMMAND pegados coinciden con los encendidos y apagados del 65XE. Que
expliquen el error visto con `3863461` es hipótesis, no está demostrado. Que el error no se reproduzca no
prueba que esté resuelto; el issue #125 sigue abierto. La retención de DATA IN tras la carga queda medida,
sin efecto visible en el menú en estas pruebas.

**Criterio de aceptación de #125, revisado (2026-11-27, decisión del responsable).**

*Antecedente:* el criterio anterior decía: «tras una carga desde cinta, con MOTOR apagado, la primera orden
del 65XE a una unidad (COMMAND bajo) devuelve DATA IN al disco (`DATA IN: cambio 6`) y esa unidad
responde». *Por qué cambia:* en el 65XE real, después de cargar un juego en RAM, la única forma de volver
al menú es apagar y encender el equipo con su interruptor físico. El criterio anterior describía una orden
con la cinta todavía dueña de DATA IN, sin apagado entre medio; ese flujo no ocurre en el uso real.

*Flujo que se acepta:* carga completa del juego → apagado físico → encendido físico → regreso al menú.

*Se cumple cuando, en una misma prueba, la consola y la pantalla del 65XE muestran todo esto:*

1. DATA IN vuelve al disco: `DATA IN: cambio 6, ahora disco` y `sio status` con `data in: duenio disco`.
2. La UART deja de estar aparcada: `sio status` con `aparcada no`.
3. La primera orden SIO después del encendido recibe respuesta. *Evidencia exigida (revisión de
   `1a2c1da`, M1):* el registro del primer intento después de `+5V alto` (primer COMMAND, trama y resultado)
   y los deltas de fallos y reintentos en esa ventana. Que el menú aparezca y `ordenes` suba no basta: es
   compatible con un primer intento perdido seguido de otro correcto. La primera trama SIO observada no es
   lo mismo que la primera orden dirigida a RetroBridge/D1: el punto 3 trata de la segunda.
4. Después, desmontar `C:` (U) y montar otra cinta en `C:` (C) funciona sin «EL HOST NO RESPONDE».
   *Evidencia exigida (M2):* dos archivos `.cas` distintos, identificados por nombre (sin datos privados).
   Secuencia completa en una misma sesión: cargar el primero, apagar físicamente, encender físicamente,
   U y C con el segundo, con la respuesta del menú y sin el error. Montar con C la misma cinta no cumple
   este punto.

*Estado:* **pendiente.** La variante E de `f93ad2e`, reportada por Claude y el responsable, da evidencia
de 1 y 2. Punto 3: «respuesta del menú después del encendido demostrada; primera orden relevante no
verificable». La primera trama SIO tras el encendido ocurrió 1571 ms después de `+5V` y el paso la
clasificó `ignored`; el registro no dice a qué dispositivo iba y no se le atribuye ninguno. El punto 3 no
se marca aprobado ni fallido. Punto 4: evidencia favorable de la variante F (abajo), revisada independientemente (revisión de
`1373aba`); una sola prueba, sin cierre. H2 sigue como riesgo independiente. #125
sigue abierta hasta tener evidencia suficiente de los cuatro puntos en una misma prueba.

*Prueba física del punto 4, variante F (2026-11-28, `f93ad2e`, solo USB y SIO, autorizada por el
responsable).* A = `international_karate.cas`, B = `Night Knight.cas`; los dos en la microSD (`sd ls`),
con nombre y tamaño distintos.

1. Primer intento de A: **BOOT ERROR** a unos 22 s de carga; el 65XE pasó a SELF TEST. En ese momento
   el registro muestra una ráfaga de unas 40 tramas en medio segundo (23 pasos `transport`, 19
   `ignored`) con la cinta todavía dueña de DATA IN y MOTOR encendido. No se sabe la causa del BOOT ERROR.
2. Apagado físico: DATA IN vuelve al disco (`cambio 6`), D1 al menú.
3. Segundo intento de A: toma en 22 ms; carga completa sin pasos `transport` fallidos; el juego corre.
4. Apagado físico: `cambio 6`, D1 al menú. Encendido físico: «65XE on»; primera trama 1570 ms después,
   `ignored`.
5. U y C con B: dos guardados de la memoria de unidades; **sin «EL HOST NO RESPONDE»** (pantalla,
   reportado por el responsable).

| Contador (`sio status`) | F0 (antes) | F1 (después) | Δ |
|---|---|---|---|
| `ordenes` | 46 | 77 | +31 |
| `envio_fallido` / `ack_fallido` | 2 / 0 | 2 / 0 | 0 / 0 |
| `cortas` | 13 | 35 | +22 |
| `command_pegado` | 6237 | 13197 | +6960 |
| `lecturas_aparcada` | 2 | 47 | +45 |
| `duenio` / `aparcada` | disco / no | disco / no | — |

*Acotado:* las +45 lecturas aparcadas no tienen hora; no se afirma que vengan de la ráfaga del primer
intento. El punto 3 sigue no verificable: la primera trama tras el encendido es `ignored`, sin dispositivo
registrado.

*Hallazgo aparte, sin registro de consola:* F0 ya traía `envio_fallido 2`, `sin_terminar 2` y una
respuesta cortada de 514 ms (máxima 707 ms), que no estaban en E1 (`envio_fallido 0`). Ocurrieron entre E1
y F0, cuando no corría ningún registro. Una respuesta del menú se cortó y no se sabe cuándo ni si se vio
el error en pantalla. Seguimiento: issue #127.

*BOOT ERROR del primer intento:* hallazgo nuevo. El plazo de 60 ms (`d1eb418`) lo había corregido en las
pruebas anteriores con la misma cinta. Queda abierto, sin causa atribuida. Seguimiento: issue #126.

*Observado en el registro de E, sin concluir causa:* al apagar, fallan dos pasos de transporte, uno con
la cinta aún dueña de DATA IN (1109698 ms) y otro durante la devolución (1109737 ms). La devolución la
dispara COMMAND bajo, antes de que se detecte «65XE off» (1109842 ms). `lecturas_aparcada` subió en 2,
pero el contador no lleva hora: **no** se afirma que esos dos fallos sean las dos lecturas aparcadas.

*Riesgo que sigue abierto (H2 de la revisión de `afa7c03`):* en `sioTask()`, `g_station->step()` corre
antes de leer COMMAND para `g_arbiter.step()`; un pulso de COMMAND durante ese paso podría no verlo el
árbitro. Es hipótesis de lectura estática. La prueba de host de ese pulso queda pendiente, en evaluación
aparte. Cambiar el criterio **no** descarta ese riesgo.

## Riesgos pendientes antes de fusionar la PR #124

Registro expreso (2026-11-28, pedido del responsable). La fusión la decide el responsable; nada de esta
lista está cerrado.

Tipos (revisión de `f17d774`, M3): **impide aceptar #125**; **incidente externo** (seguimiento en otra
issue, con riesgo residual: abrir la issue es disposición trazable, no prueba de corrección ni permiso de
fusión); **no aprobado** (fuera de esta PR).

| Riesgo | Dónde se sigue | Tipo para la decisión de fusión |
|---|---|---|
| Punto 3 de #125: primera orden relevante tras el encendido, no verificable con los registros actuales | #125 | Impide aceptar #125 |
| H2: un pulso de COMMAND durante `g_station->step()` que el árbitro no ve | #125, prueba de host aparte | Impide aceptar #125 |
| Punto 4 de #125: evidencia favorable de la variante F, revisada independientemente (una prueba) | #125 | Favorable; no basta para aceptar #125 sola |
| BOOT ERROR en el primer intento de `international_karate.cas` | #126, con su propio criterio | Incidente externo, riesgo residual |
| `envio_fallido 2` y respuesta cortada entre E1 y F0, sin registro | #127, con su propio criterio | Incidente externo, riesgo residual |
| H3 (caché de `baud_`), H4 y GPIO36 | fuera de esta PR | No aprobado; ninguna prueba de aquí es aprobación eléctrica |

Lo que esta PR sí aporta, con evidencia: el plazo de toma de 60 ms (`d1eb418`) y la instrumentación de
#125. Ninguna de las dos cierra #125, #126 ni #127.

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
  verificado (sha256 `48077aaa…`). Los commits posteriores cambian firmware (texto de la tecla C,
  diagnóstico de la Issue #125); cada uno tiene su CI y su paquete.
- [x] Prueba en el 65XE: `international_karate.cas` carga (con aprobación) — prueba: «PROGRAMA CARGADO»
  en pantalla y el juego corre (lo vio el responsable), consola de `d1eb418` (sección «Prueba física»).
  Evidencia del responsable, no verificación independiente.
- [x] Revisión independiente, ronda 5 (`7863d59`): sin bloqueante de firmware; pide coherencia documental,
  CI del HEAD y seguimiento explícito del incidente.
- [x] Revisión independiente, ronda 4 (`d1eb418`): sin bloqueante en el delta; no es prueba de carga.
- [ ] Seguimiento aparte, fuera de esta PR — incidente «EL HOST NO RESPONDE» (prueba de `47e5f56`, tras el
  segmento 36): contrastar con la especificación las transiciones reales (COMMAND, `tapeEnded`, dueño del
  árbitro, Release) con los registros `DATA IN diag:`. Criterio de aceptación revisado (2026-11-27): ver
  «Criterio de aceptación de #125, revisado» arriba; el criterio anterior queda ahí como antecedente.
  Estado: punto 4, evidencia favorable de la variante F, revisada independientemente; punto 3 no
  verificable; H2 pendiente;
  #125 sin cierre. El plazo de 60 ms no lo corrige por sí solo.
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
