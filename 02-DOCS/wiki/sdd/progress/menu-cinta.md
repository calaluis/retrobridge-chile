---
type: progress
title: Progreso — menu-cinta
description: Registro por tarea de menu-cinta desde T9a: qué se decidió, con qué pruebas y qué queda.
tags: [sdd, progress, menu, cassette]
topic: sdd
slug: menu-cinta
---

# Progreso — menu-cinta

Plan: [`plans/menu-cinta.md`](../plans/menu-cinta.md). T1–T7 se hicieron en el PR #85. El banco T8 está en
`02-DOCS/raw/2026-11-23-banco-menu-cinta-t8.md`. Este registro empieza en T9a.

## T9 dividida en T9a y T9b (2026-11-24, solo documentos)

- **Origen.** El responsable revisó el firmware FujiNet original y la FN32ROV-1.3-Q24. Las conclusiones:
  - lo funcional de MOTOR y DATA IN tiene evidencia;
  - los niveles eléctricos de GPIO36 y del pin 3 no la tienen.

  La revisión independiente aprobó desarrollar T9 sin salida física (T9a) y dejar el banco (T9b) con sus
  requisitos eléctricos.
- **Plan, §2.4.** El traspaso entre la tarea SIO y la de la casetera es asíncrono:
  - estados `TakePending`, `ReleasePending` (esperando) y `Fault`;
  - confirmaciones con secuencia, plazos de 20 ms para tomar y según la cola de TX para soltar, y una recuperación
    que nunca deja dos transmisores en GPIO21;
  - la salida cambia solo con el hardware libre: TX de UART2 vacío, sin pasada RMT y sin fallo;
  - UART2 se instala una sola vez, así que la tarea SIO no reserva memoria. Las reservas del RMT siguen en la
    tarea de la casetera, como ya está documentado.
- **Plan, §6.** T9a.1–T9a.7 cubren los cinco requisitos de T9 (salida en marcha, `tapeEnded` real, `A` sin disco en
  D1, D1 tras el arranque de cinta y la línea de ayuda). Requisitos previos de T9b:
  - M3a medida;
  - protección de GPIO36 validada;
  - H4-C con C5, C8a, C8b y C9;
  - la aprobación de H4 y M3a que exige el empaquetador;
  - un PR aparte para `kTapeOutput = true`.

  Ninguna aceptación de riesgo los reemplaza.
- **`h4-c-casetera.md`.**
  - C8 se separa en C8a (matriz GPIO y control de UART2/RMT, por USB y sin Atari) y C8b (nivel del pin 3 con el
    Atari, después de C5).
  - C9 no queda cubierto por `brownout=0` ni por el pin 10.
  - H4-C no reemplaza la aprobación de H4 del empaquetador.
- **Spec.** Una nota dice que T9a no es evidencia física de AC9–AC12. Se añadió una pregunta abierta sobre qué
  contesta D1 después de un arranque de cinta, con su recomendación.
- **Sin cambios:** `puertas.json`, ADR-012, `firmware_package.py` y `kTapeOutput = false`. No hubo flasheo ni
  conexiones.

## Revisión 2 del PR #99 (2026-11-24)

- **Política de búfer (decisión 3 del plan).**
  - Problema: el disco instala UART2 sin anillo de TX, así que 512 bytes a 600 baudios bloquearían a la casetera
    unos 6,4 s.
  - Decisión: un anillo de 2048 bytes al arrancar. El disco conserva su `write` + `uart_wait_tx_done`. La cinta
    escribe sin bloquear y con 32 bytes en vuelo como mucho, así que `release` queda en ≤ 0,6 s.
  - Alternativa, si el banco del disco muestra otro tiempo: sin anillo, escribiendo con la FIFO vacía.
- **Recuperación de `Fault`.** `Disk` se confirma solo con el pin devuelto a UART2 **y** la matriz GPIO verificada
  (la misma `route()` del banco C8). Si no se logra, el estado es `Released`: la línea queda suelta y el disco no
  reanuda. Se prueban las confirmaciones viejas o tardías y los fallos de recuperación.
- **`tapeEnded`.** Se publica verdadero solo con el reproductor al final, el TX vacío y sin pasada FSK.
- **C8b.** Se define un ensayo acotado (`h4-c-casetera.md` §3.1): un paquete de banco con las puertas cerradas,
  un patrón fijo de 2 s activado por consola, MOTOR comprobado en 0 V y medido con osciloscopio, después de C5. No
  aprueba H4 ni M3a.

## Revisión 3 del PR #99 (2026-11-24)

- **Bytes en vuelo.** `uart_get_tx_buffer_free_size()` mide solo el anillo, y la interrupción pasa hasta 128 bytes
  a la FIFO. Ahora la cinta cuenta anillo + FIFO + registro de desplazamiento, leídos en el orden que sobrestima, y
  escribe solo por debajo de 32 en total. `kReleaseTimeoutUs` = 600 ms. La prueba incluye un mutante que mira solo
  el anillo y deja 160 bytes en vuelo.
- **`DiskLine.resume()`.** Devuelve `Done` o `Failed`. `Done` solo con 19 200 baudios (±2 %) y 8N1 verificados y la
  entrada vaciada. `Failed` deja el árbitro en `DiskFault`: D1 en silencio, ningún evento de recuperación,
  reintento cada 100 ms. Riesgo nuevo R12.
- **Revisión 4.** La alternativa sin anillo escribía hasta 64 bytes y contradecía la cota de 32. Ahora escribe
  como mucho 32 bytes con la FIFO vacía: misma cota, mismo `kReleaseTimeoutUs` = 600 ms.

## T9a.1 — traspaso asíncrono (2026-11-24)

- **Hecho.**
  - `DataInHandover` (`rb_menu`): dos palabras atómicas de 32 bits. La tarea SIO escribe la petición (`Take` o
    `Release` con una secuencia de 30 bits que nunca es 0) y la casetera escribe la confirmación (`Disk`, `Tape`,
    `Released` o `Refused` con la secuencia que contesta). Sin cerrojos ni memoria dinámica.
  - `HandoverTapeLine`: el lado SIO. Publica una vez y luego consulta; pedir el otro tipo, o `abandon()`, crea una
    secuencia nueva, así que una confirmación vieja se ignora.
  - `DataInArbiter`: estados `Disk`, `TakePending`, `Tape`, `ReleasePending`, `Fault`, `Released` y `DiskFault`.
    `DiskLine` pasa a `park()`/`resume()`; `TapeLine` a `Done`/`Pending`/`Failed`/`Released`. Plazos:
    `kTakeTimeoutUs` = 20 ms, `kReleaseTimeoutUs` = 600 ms, reintentos cada 100 ms.
  - `menu_sio.cpp`: adaptadores con el comportamiento de hoy, nunca alcanzados con `kTapeOutput = false`. T9a.7
    los reemplaza.
- **Decisiones de implementación.**
  - El «estado» único del plan se guarda como dos palabras, para que ninguna tarea pise la última escritura de la
    otra. `TakeRequested`/`ReleaseRequested` = una petición sin confirmar.
  - El plazo se mira **antes** que la confirmación: una confirmación que llega justo al vencer se ignora.
  - `Fault` y `Released` reintentan sin emitir eventos; solo un cambio de estado emite uno.
  - El dueño `None` desaparece: sin `begin()` no hay «fallo al instalar». Lo que el plan llamaba «el árbitro queda
    en None» es el dueño `Released`.
- **Pruebas.**
  - `test_data_in_arbiter` (20): las reglas 1–5 portadas al contrato nuevo, más `DiskFault`, `Fault` por rechazo y
    `park()` con el TX ocupado.
  - `test_data_in_handover` (15, nueva): casetera falsa que confirma tarde, nunca, con otra secuencia, rechazando o
    soltando sin UART2. En cada paso: nunca dos transmisores en GPIO21 y el disco atiende solo en `Disk`. Cero
    reservas en la ruta SIO (contador de `operator new`).
  - RED visto: 14/20 y 15/15 fallaban con el stub. GREEN: 20/20 y 15/15. Host completo 96/96.
  - Cinco mutantes detectados: confirmación aceptada al vencer (2 fallos), `release` vencido que reanuda el disco
    (3), un evento por reintento (2), confirmación sin mirar la secuencia (15), `resume()` fallido tomado como
    `Disk` (3).
- **Atómicos en Xtensa: resuelto con el código objeto (revisión del PR #100).**
  - **Por qué hacía falta.** El GCC del ESP32 no da `std::atomic<uint32_t>` como «siempre sin cerrojo», así que el
    `static_assert` quedó solo en host (604a9ba). Que compile no prueba que no haya cerrojo.
  - **El `.elf` no sirve como prueba.** En `retrobridge.elf` (paquete `menu-sio` de 604a9ba) aparecen los símbolos de
    `DataInArbiter`, pero no los de `DataInHandover`: el firmware todavía no lo usa (T9a.7) y el enlazador lo quita.
    Se inspecciona el objeto `data_in_handover.cpp.obj`.
  - **La comprobación** (`tools/ci/check_atomic_calls.py`, paso de CI «DataInHandover atomics without locks on
    Xtensa»):
    1. **Control negativo:** compila una carga atómica de 64 bits con el mismo comando de `compile_commands.json` y
       exige ver la llamada. Resultado: `R_XTENSA_32 __atomic_load_8`.
    2. Falla si el objeto llama a `__atomic_*`, `__sync_*`, `vPortEnterCritical*`, `spinlock_acquire*` o
       `esp_cpu_compare_and_set*`.
    3. Falla si falta alguna de las cuatro funciones, o si no muestra su acceso: carga de adquisición = `l32i`
       seguido de `memw`; almacén de liberación = `memw` seguido de `s32i`.

    Autopruebas: 10, incluidas las que deben fallar (objeto vacío, función ausente, almacén sin `memw` antes, carga
    sin `memw` después).
  - **Desensamblado real** (CI 45413310899, ESP-IDF 6.1, `xtensa-esp32-elf`, `-Og`, extracto):

    ```text
    rb::host::DataInHandover::request(HandoverRequest):   23: memw / 26: s32i.n a8, a2, 0  / 2a: memw
    rb::host::DataInHandover::confirm(unsigned long, HandoverAck):  c: memw /  f: s32i.n a3, a2, 4 / 11: memw
    rb::host::DataInHandover::ackFor(unsigned long, HandoverAck&) const:  3: memw / 6: l32i.n a8, a2, 4 / 8: memw
    rb::host::DataInHandover::pending() const:              3: memw / 6: l32i.n a3, a2, 0 / 8: memw
    ```

    `request_` está en el desplazamiento 0 y `ack_` en el 4. Cada acceso es una instrucción de 32 bits alineada, con
    `memw` (barrera de memoria de Xtensa) antes y después. No hay ninguna llamada a `libatomic` ni a secciones
    críticas.
  - **Una llamada que no es cerrojo:** con `-Og`, `pending()` llama a `memset` para poner a cero el `Pending` que
    devuelve. No toma cerrojo ni reserva memoria, y corre en la tarea de la casetera, no en la ruta SIO.
  - **Conclusión:** en el ESP32, el traspaso cumple «solo atómicos, sin cerrojos». No hace falta otra
    implementación.
- **Queda para otras tareas.** Contar llamadas a `uart_driver_*` y `rmt_*` necesita el `DiskLine` real: T9a.7.
  T9a.7 debe volver a ver `DataInHandover` en el `.elf`, ya enlazado.
  `rb_menu` no incluye cabeceras de drivers. Hardware libre y `Released` en la casetera: T9a.2.

## T9a.2 — `EspDataIn` en UART compartido (2026-11-24)

- **Hecho.**
  - `EspDataIn::attachShared(pins, txRing)`: usa UART2 tal como lo instaló el disco al arrancar. No instala, no
    borra y no enruta nada; `release()` en este modo solo se desengancha.
  - `send()` en modo compartido (decisión 3): cuenta los bytes en vuelo como anillo (0 si está vacío, su tamaño
    si no: ver «Revisión del PR #101») + FIFO (128 − huecos, `uart_ll_get_txfifo_len()` de `esp_hal_uart`) + 1 del registro. Escribe solo
    si todo cabe por debajo de 32; si no, devuelve `false` sin tomar nada (el contrato de `ByteOut`). Sin anillo
    (la alternativa): como mucho 32 bytes y solo con la línea vacía.
  - `hardwareFree()`: la condición «hardware libre» de §2.4 (TX vacío, sin pasada FSK ni porciones en cola, RMT
    sin `releasePending()`, sin fallo).
  - `giveBack()` → `Busy`, `Uart` o `Lost`: termina la pasada, devuelve el pin a UART2 y, en modo compartido,
    **lee la matriz GPIO** (`U2TXD_OUT_IDX` con la salida habilitada). Si `uart_set_pin` falla o la matriz dice otra
    cosa: `gpio_reset_pin`, `faulted()` y `Lost` (la casetera confirma `Released`). El error se registra una vez
    por fallo.
  - La lectura de la matriz pasa a `rb/esp/gpio_route.hpp`; el banco C8 (`main/bench_c8.cpp`) la usa.
  - `EspSioUart(pins, txRing)`: el anillo de TX es un parámetro; el valor por defecto sigue en 0. El firmware no
    cambia: el anillo de 2048 (`kSharedTxRing`) lo pone T9a.7.
- **Decisiones de implementación.**
  - `send()` es «todo o nada» en vez de partir la escritura: el reproductor, tras un rechazo, pide un solo byte
    en el tick siguiente, así que nunca se atasca.
  - La comprobación de la matriz solo corre en modo compartido; el modo propio (paquetes `cassette` y `bench-c8`)
    se comporta igual que antes.
  - Lista exacta de llamadas del empaquetador (`tools/ci/firmware_package.py`): `uart_wait_tx_done` 2 → 3,
    `uart_get_tx_buffer_free_size` 1 y `uart_ll_get_txfifo_len` 1, nuevas; el resto igual (`uart_set_pin` 3,
    `gpio_reset_pin` 2). La lista de antes de C8 (paquete `cassette-lectura`, f405106) no cambia. `puertas.json`
    no se toca.
- **Pruebas.**
  - `fake_idf` con un modelo de TX: anillo con cabeceras por escritura, FIFO de 128, registro, 10 bits por byte e
    interrupción que mueve bytes (también entre las dos lecturas). Y la matriz GPIO del pin.
  - `test_esp_data_in_shared` (17, nueva): ninguna llamada a `uart_driver_install`/`delete`; un canal RMT por
    pasada; `send()` sin bloqueo (0 µs) y ≤ 32 bytes en vuelo, también con la interrupción entre lecturas y con la
    FIFO llena y el anillo vacío; con 512 bytes debidos la línea vuelve en ≤ 600 ms, con anillo y sin él; hardware
    libre; `giveBack()` con TX ocupado, RMT que no suelta, `uart_set_pin` fallido y matriz distinta.
  - `test_esp_data_in_recovery` (5, nueva): árbitro + traspaso + `EspDataIn` reales. `Disk` solo tras
    `uart_set_pin` y matriz correctos; si no, `Released`, ningún `resume()` y reintento cada 100 ms; al
    recuperarse, un solo `resume()`; confirmación vieja o tras el plazo, ignorada.
  - `test_esp_sio_uart`: `write()` termina en el mismo tiempo con anillo de 2048 que sin él (1, 4, 128 y 129
    bytes a 19 200).
  - Mutantes (10): 9 detectados (FIFO sin contar, sin control de espacio, `hardwareFree` sin UART, sin matriz,
    `releasePending` ignorado, confirmar con bytes en vuelo, más de 32 sin anillo, error en cada reintento).
    Sobrevive «`hardwareFree` sin mirar el canal RMT»: es equivalente, porque un canal vivo implica `sending_` o
    `releasePending_` tras cada `pump()`/`stop()`. Se deja como barrera defensiva (`!fskActive()` del plan).
- **Lo que no prueba.** Que el driver y la HAL reales se comporten como el modelo: lo ve el banco de T9b. La
  compilación con ESP-IDF 6.1 la confirma la CI.

### Revisión del PR #101 (2026-11-24)

- **1. `uart_get_tx_buffer_free_size()` no es el espacio libre.** En ESP-IDF 6.1
  (`components/esp_driver_uart/src/uart.c`, release/v6.1) devuelve una cota conservadora para la siguiente
  escritura en un anillo NOSPLIT:
  - cada `uart_write_bytes()` mete dos elementos: la descripción (cabecera de 8 + `uart_tx_data_t`, 12 bytes) y los
    datos (8 + datos redondeados a 4);
  - anillo vacío: tamaño − 8 − 20 (2020 con 2048). Con un elemento dentro, la cota baja al tamaño máximo de
    elemento − 20 (996): `txRing − free` daba 28 con el anillo vacío y 1052 con un byte dentro;
  - la interrupción devuelve el elemento de datos solo cuando lo copió entero a la FIFO.
- **Corrección.** La lectura solo dice si el anillo está vacío: `free + 28 ≥ txRing`. Vacío cuenta 0; no vacío (o
  ilegible) cuenta como lleno. `send()` escribe solo con el anillo vacío y la suma FIFO + registro + bytes ≤ 31, así
  que en vuelo nunca hay más de 32. Otra versión de IDF con otros tamaños se lee como «nunca vacío»: la cinta se
  para, que es seguro; la CI fija la 6.1.
- **El doble** (`fake_idf`) ahora sigue `uart.c`: anillo NOSPLIT con posiciones, los dos elementos por escritura,
  la interrupción que devuelve elementos en orden y la misma fórmula de `uart_get_tx_buffer_free_size()`.
- **2. `release()` y `recover()` en modo compartido.**
  - `release()` se desengancha solo si `giveBack()` devuelve `Uart` (pin en UART2 TX, comprobado en la matriz, y
    nada por salir). Con TX pendiente devuelve `false` y sigue enganchado; con `uart_set_pin` fallido o ruta
    incorrecta devuelve `false`, el pin queda reseteado y el driver en fallo y enganchado.
  - `recover()` pasa por `pinToUart()`, que lee la matriz: sin `U2TXD_OUT_IDX` con salida habilitada, el pin se
    resetea otra vez y sigue el fallo. Con bytes aún en UART2 restaura la ruta en el acto; esos bytes salen por
    UART2 y `hardwareFree()` sigue falso hasta que salen.
  - La lista de llamadas del empaquetador no cambia: ninguna llamada nueva.
- **RED (antes de corregir, 1f5ee49 con el doble nuevo):** 7 de 26 fallan en `test_esp_data_in_shared`:
  `give_back_is_busy_while_bytes_go_out` (8 bytes rechazados con la línea vacía), `an_empty_line_takes_a_30_byte_
  catch_up` (`inFlight()` = 29 con la línea vacía), `release_waits_for_the_bytes_going_out`, `release_after_a_
  failed_uart_set_pin_refuses_and_stays_safe`, `release_with_a_wrong_route_refuses`, `recover_checks_the_matrix_
  in_shared_mode` y `recover_with_bytes_pending_restores_the_route_and_keeps_the_line_busy`. Pasan ya antes:
  `the_free_size_follows_esp_idf_6_1` (prueba del doble), `a_write_still_in_the_ring_blocks_the_next` y
  `records_flow_at_600_baud_without_loss`.
- **GREEN:** 26/26 en `test_esp_data_in_shared`; host 98/98; herramientas 300/300.
- **Progreso a 600 baudios:** 10 s con el reproductor al ritmo del reloj: 600 bytes aceptados, todos llegan al
  driver, al menos 599 ya salieron por la línea, 0 µs bloqueado y nunca más de 32 en vuelo.
- **Mutantes:** 14 detectados de 15 ejecutados. Los 6 nuevos: lectura vieja `txRing − free`, anillo no vacío
  contado como vacío, sin el coste de 28, `release()` sin `giveBack()`, `release()` con `Busy` y `recover()` sin
  matriz. Sigue sobreviviendo el equivalente «`hardwareFree` sin el canal RMT».

## T9a.3 — salida en marcha (2026-11-24)

- **Hecho.**
  - `CassettePlayer::changeOutputs(previous)`: el dueño pasa otras salidas a `tick()` desde el tick siguiente.
    Se llama entre dos ticks, así que ningún `send()` ni `push()` está a medias: cada byte va entero a una sola
    salida.
  - `PlayerOutputs` (`rb/cassette/player_outputs.hpp`, nuevo): «descartar» hasta que la casetera toma DATA IN; las
    salidas vivas (`EspDataIn` en el ESP32) solo tras `select(true)`; «descartar» otra vez con `select(false)`.
    `select(true)` sin salidas vivas devuelve `false` y sigue en «descartar». No reserva memoria.
- **Decisiones de implementación.**
  - **Registros:** el registro sigue desde el byte donde está; nada se pierde ni se repite. La velocidad se vuelve a
    enviar a la salida nueva antes de su primer byte (sin esto, `EspDataIn` sacaría los bytes a la velocidad que
    dejó el disco, 19 200).
  - **FSK ya empezado** (`FskPlay`, o `FskDrain` con la salida vieja sin vaciar): se corta en la salida vieja
    (`stop()`, DATA IN a marca) y la pasada se repite entera desde su hueco en la salida nueva, como un corte de
    MOTOR (PR #1804). La salida nueva nunca recibe media pasada. Esperar al final de la pasada no cabe en los plazos
    (`kTakeTimeoutUs` = 20 ms, `kReleaseTimeoutUs` = 600 ms).
  - **FSK que ya sonó entero** (`FskDrain` con la salida vieja vacía): no se repite; la cinta sigue.
  - **FSK esperando la puerta de inicio:** no ha sonado nada; no se corta y el cargador conserva su trabajo.
  - Quién llama a `select()` (la tarea de la casetera, en los pasos 3 y 6 del traspaso) es T9a.7; esta tarea deja la
    pieza lista y probada. `CassetteService` no cambia.
- **Pruebas** (`test_cassette_player`, 8 nuevas): registro partido entre salidas sin perder ni repetir bytes y con la
  velocidad en la nueva; envío rechazado por la vieja que sale por la nueva; pasada drenando y pasada a medias
  cortadas y repetidas enteras, sin reservas; pasada ya sonada sin repetir; puerta cerrada sin corte; `PlayerOutputs`
  con toma y suelta a mitad de registro y suelta a mitad de pasada (la salida viva se para y no recibe más).
- **RED** (con `changeOutputs` vacío): 6 de 8 fallan. Pasan ya antes, como pruebas de no regresión, «pasada ya
  sonada» y «puerta cerrada».
- **GREEN:** 34/34 en `test_rb_cassette_player`; host 98/98; herramientas 300/300; capas OK.
- **Mutantes:** 7 de 7 detectados: velocidad no reenviada, pasada sonada repetida, `FskPlay` sin corte, `FskDrain`
  sin corte, `select()` que corta la salida nueva en vez de la vieja, `select(true)` sin salidas vivas y `select()`
  sin avisar al reproductor.
- **Lo que no prueba.** El cambio con el hardware real (UART2 y RMT); eso lo cubren `hardwareFree()` de T9a.2 y el
  banco de T9b.

### Revisión del PR #102 (2026-11-24)

- **Riesgo confirmado.** El reproductor seguía con MOTOR mientras DATA IN aún no era de la casetera: durante
  `TakePending` un registro sin hueco se iba entero a «descartar», y una pasada FSK corta «sonaba» en
  `DiscardWaveOut` (siempre `idle()`) y al tomar no se repetía. Se perdía contenido.
- **Corrección (en T9a.3).**
  - `PlayerOutputs::tapeMayMove()` y `GatedMotor`: con salida viva configurada, la cinta se mueve solo mientras
    está seleccionada; el reproductor ve MOTOR apagado en `TakePending`, tras soltar o con la toma fallando (la
    pasada FSK sigue precargándose en su hueco). Sin salida viva (`kTapeOutput = false`) nada cambia: la cinta va
    con MOTOR a «descartar» y `cas status` sigue el intento.
  - Defensa debajo de la puerta: `changeOutputs(previous, previousDrove)`. Desde una salida que no llegaba a DATA
    IN, un registro o una pasada ya empezados se repiten enteros desde su hueco, aunque «descartar» los diera por
    sonados.
  - La política de UART2 (decisión 3, `hardwareFree()`) y las puertas M3a/H4 no cambian.
- **Contrato con T9a.7** (plan, requisito 1): `setLive()` al arrancar, cada tick con `GatedMotor`,
  `select(true)`/`select(false)` en los pasos 3 y 6. Prueba de integración obligatoria antes de `kTapeOutput = true`.
- **RED** (API con `tapeMayMove()` siempre verdadero y `previousDrove` ignorado): 6 fallan;
  `a_record_with_no_gap_waits_for_the_take` (3 bytes en «descartar») y
  `a_fsk_run_ending_during_take_pending_is_not_lost` (3 partes en «descartar») reproducen los dos escenarios.
- **GREEN:** 41/41 en `test_rb_cassette_player`; host 98/98; herramientas 300/300; capas OK.
- **Mutantes:** 14 de 14 detectados (7 nuevos: puerta siempre abierta, cinta parada sin salida viva, `GatedMotor`
  sin puerta, `previousDrove` ignorado, pasada o registro desde «descartar» sin repetir, `select()` que da el lado
  nuevo como viejo).

## T9a.4 — `tapeEnded` real (2026-11-24)

- **Hecho.**
  - `ByteOut::drained()` (nuevo, virtual puro): todo lo enviado salió de DATA IN. `DiscardByteOut`: siempre
    verdadero. `EspDataIn`: `txEmpty()` (anillo, FIFO y registro de UART2 vacíos); sin configurar, verdadero (nada
    suyo en la línea). Sin llamadas nuevas a ESP-IDF: la lista del empaquetador no cambia.
  - `CassettePlayer::ended(bytes, wave)`: cinta cargada, última parte terminada, `bytes.drained()` y
    `wave.idle()`. Vuelve a falso al rebobinar o expulsar.
  - `rb::host::TapeProgress` (`rb/menu/tape_progress.hpp`): un `std::atomic<bool>` que publica la tarea de la
    casetera y lee la tarea SIO; sin cerrojo ni reservas. En `rb_menu` porque lo lee el lado SIO, como
    `DataInHandover`.
  - El cableado (publicar cada tick y leerlo en `menu_sio.cpp`) es T9a.7: contrato en el requisito 2 del plan.
- **Pruebas** (7 nuevas):
  - `test_rb_cassette_player`: cinta de dos partes, sin fin tras la primera y con fin solo con los bytes de la
    última fuera; última parte FSK, sin fin mientras suena; falso otra vez al rebobinar y al expulsar.
  - `test_esp_data_in_shared`: `drained()` falso con bytes en el anillo y con el último en el registro; verdadero
    al salir; verdadero sin configurar aunque UART2 tenga bytes de otro.
  - `test_data_in_handover`: regla 3 con el valor publicado: MOTOR apagado al final con bytes aún saliendo, 50
    ticks sin pedir `release`; al vaciarse, `ReleaseRequested` y `Disk` con un solo `resume()`; el valor vuelve a
    falso.
- **RED** (con `ended()` = `atEnd()` y `drained()` siempre verdadero): fallan 4 (las 3 del reproductor y la de bytes
  en camino). Las 2 del traspaso pasan ya antes: la regla 3 existe desde T9a.1; son de integración del valor
  publicado.
- **GREEN:** host 98/98; herramientas 300/300; capas OK.
- **Mutantes:** 6 de 6 detectados: `ended()` sin `loaded()`, sin `atEnd()`, sin los bytes, sin la onda;
  `drained()` siempre verdadero y `drained()` sin el caso «sin configurar».
- **Lo que no prueba.** La publicación entre las dos tareas reales del ESP32 (T9a.7) ni el UART2 real (banco T9b).

### Revisión del PR #103 (2026-11-24)

Aprobable para T9a.4, sin defecto bloqueante. Dos observaciones pasan a verificación obligatoria de T9a.7
(plan, requisito 2, y fila de T9a.7):

- **La cadena completa.** La prueba del traspaso publica `atEnd && drained` a mano; T9a.7 prueba
  `CassettePlayer::ended()` → `TapeProgress` → `DataInArbiter` con el reproductor y el árbitro reales.
- **Atómico en el ESP32.** El `static_assert` de `TapeProgress` es solo de host. Hoy ningún objeto del firmware usa
  la cabecera; T9a.7 pasa `check_atomic_calls.py` (con su control negativo) por los objetos que la usen.
- **Menores, corregidos.** Eran 7 pruebas nuevas, no 6 (se añadió la del caso «sin configurar» para matar un
  mutante). `drained()` del doble de `test_cassette_commands.cpp` estaba pegado al cierre de la función anterior.

## T9a.5 — `A` sin disco en D1 con cinta en `C:` (2026-11-24)

- **Decisiones del responsable (2026-11-24).** Requisito 4: con D1 vacía, D1 responde como unidad sin disco.
  Puertas cerradas: `A` se niega con `CINTA EN C:, AUN NO PUEDE CARGARSE`. Quedan en la spec (pregunta abierta
  cerrada).
- **Hecho** (`MenuController`).
  - `A` con D1 vacía y una cinta lista en `C:` (`tapeOnC()`: casetera presente, nombre en `C:` y sin restauración
    pendiente) llama a `bootTape()`: comprueba las puertas, vacía D1 con el mismo `unmount` que la tecla U y
    responde `kMenuActionBoot` (el 6502 salta a `COLDSV`; con START+OPTION el OS arranca la cinta).
  - D1 vacía en el `DriveBay`: el despachador SIO contesta a D1 con ERROR al instante (comportamiento de unidad
    vacía que ya existía).
  - `InFlight::TapeBoot`: al aplicarse, `menuOnD1()` pasa a falso y el registro dice `D1=empty (tape boot)`; si
    el vaciado falla, `ERROR: D1 NO SE PUDO VACIAR`. El arranque en frío vuelve a pedir el menú, como tras un
    arranque de disco.
  - Con un disco en D1 nada cambia: `A` lo arranca, haya cinta o no.
- **Pruebas** (`test_menu_cinta`, 7 nuevas): arranque de cinta con D1 vacía (D1 sin imagen, menú fuera, cinta en
  `C:`, eventos); disco en D1 con cinta (arranca el disco); puertas cerradas (aviso, menú en D1); sin cinta y con la
  cinta sacada (`D1 SIN DISCO`); arranque en frío tras la cinta (menú de vuelta, cinta en `C:`); cinta recordada sin
  confirmar (no arranca; al confirmarse, sí); cambio de cinta en lectura (no arranca).
- **RED:** con el código de main fallan 3 (arranque de cinta, puertas, arranque en frío). Las demás pasan ya antes
  y quedan como no regresión.
- **GREEN:** host 98/98; herramientas 300/300; capas OK.
- **Mutantes:** 6 de 6 detectados: nunca arranca la cinta, sin mirar el nombre, sin mirar la restauración, sin
  puertas, D1 sin vaciar y `menuOnD1()` verdadero tras el arranque de cinta. «Sin mirar la lectura en curso» era
  equivalente (`key()` no llega a `boot()` durante un trabajo): la comprobación se quitó.
- **Revisión del PR #104 (2026-11-24).** Hallazgo confirmado con RED: una cinta restaurada que terminaba en
  `Unconfirmed` (o un reemplazo sin respuesta) dejaba `tapeRestore_ = None` y el nombre en `C:`, así que `A`
  arrancaba sin saber qué cinta tenía la casetera.
  - **Corrección.** `tapeLoaded_` separa «nombre en `C:`» de «cinta confirmada». Solo `Ready` (montaje o
    restauración) lo pone verdadero; `Unconfirmed`, una restauración y la expulsión lo ponen falso. Un reemplazo
    rechazado (`Invalid`, `NoMemory`) no lo cambia: la casetera conserva su cinta (AC6, `tape_deck.hpp`). Un
    reemplazo cancelado sí lo pone falso (segunda revisión, abajo). `tapeOnC()` = casetera presente y `tapeLoaded_`. El nombre recordado no se borra.
  - **Pruebas nuevas (5):** restauración `Unconfirmed` no arranca; reemplazo `Unconfirmed` no arranca (y al montar
    otra vez con `Ready`, sí); restauración sobre una cinta confirmada no arranca; reemplazo rechazado arranca la
    cinta que había; y el arranque de disco del OS por el despachador SIO real (`READ` del sector 1 de D1):
    `COMPLETE` con el menú en D1 y `ERROR` tras `A` con cinta.
  - **RED:** con el código anterior fallan las 2 de `Unconfirmed`. **GREEN:** host 98/98 (`test_menu_cinta` 32/32);
    herramientas 300/300; capas OK.
  - **Mutantes:** 7 de 8 detectados (arranca sin confirmación; `Unconfirmed` no la quita; `Ready` de montaje o de
    restauración no la pone; la expulsión o la restauración no la quitan; un reemplazo rechazado la quita).
    Equivalente: quitar `tape_ != nullptr` de `tapeOnC()`, porque `tapeLoaded_` solo se pone con una respuesta de
    la casetera.
- **Segunda revisión del PR #104 (2026-11-24).** Hallazgo confirmado con RED: `OLD.CAS` en `C:`, `NEW.CAS`
  leyéndose, Esc o Break. `cancelJob()` soltaba `Job::Tape` con `tapeLoaded_` aún verdadero, pero `CasTapeDeck`
  vuelve a poner la cinta anterior por su cuenta (`undo()`) y eso puede fallar (`restoreDone(NotTaken)` expulsa).
  El menú no ve ese resultado y `A` arrancaba.
  - **Corrección.** Al cancelar una lectura de cinta, `tapeLoaded_` pasa a falso. El nombre sigue en `C:`; `A`
    no arranca hasta montar una cinta otra vez y que la casetera diga `Ready`.
  - **Pruebas nuevas (3):** cancelar con Esc no arranca (y al montar otra vez, sí); cancelar con Break no
    arranca; y con el `CasTapeDeck` real (canal falso): la casetera toma `NEW`, la restauración de `OLD` falla,
    la casetera expulsa y queda vacía, y `A` no arranca.
  - **RED:** con el código anterior fallan las 3. **GREEN:** host 98/98 (`test_menu_cinta` 35/35);
    herramientas 300/300; capas OK.
  - **Mutantes:** 8 de 9 detectados (los 7 anteriores y «la cancelación no la quita»). Sigue el mismo
    equivalente (`tape_ != nullptr`).
- **Lo que no prueba.** El 65XE real: que el OS, con START+OPTION, haga el arranque de disco, reciba el ERROR de
  D1 y cargue la cinta (banco T9b). El ERROR de D1 ya se prueba en host con el despachador SIO real.

## Siguiente

Revisar este PR de documentos. Los PR de código de T9a empiezan solo después de aprobarlo, uno por tarea.
