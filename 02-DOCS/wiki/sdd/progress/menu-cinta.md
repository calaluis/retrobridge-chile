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
  - `send()` en modo compartido (decisión 3): cuenta los bytes en vuelo como anillo (tamaño − libre, cabeceras
    incluidas) + FIFO (128 − huecos, `uart_ll_get_txfifo_len()` de `esp_hal_uart`) + 1 del registro. Escribe solo
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

## Siguiente

Revisar este PR de documentos. Los PR de código de T9a empiezan solo después de aprobarlo, uno por tarea.
