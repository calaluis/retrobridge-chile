# ADR-016 — Transporte NetSIO para servir a un Atari emulado

- Estado: Propuesta (2026-11-04), Issue #32, entrega E1a. Decisiones D1–D6 del diseño aprobadas el 2026-11-04.
  Enmendada el 2026-11-19 por [ADR-019](0019-escritura-de-sectores.md): la escritura de sectores reemplaza el
  `write_size` 0 y la `09` vacía.
- Contexto: objetivo de producto acordado en la Issue #32. RetroBridge debe poder usarse desde un Atari emulado
  (Altirra), no solo desde el hardware.

## Contexto

`SioSession` ejecuta la transacción SIO completa contra el puerto `SioTransport`:

1. `waitCommandFrame()`;
2. un `send()` de ACK o NAK;
3. un `send()` con COMPLETE o ERROR y la trama de datos;
4. `releaseDataLine()`.

NetSIO (FEP-003) transporta las señales y los bytes del bus SIO en datagramas UDP. El emulador **pausa** al Atari
tras `COMMAND OFF + SYNC` (`18 <sync>`) hasta recibir una *Sync response* (`81 <sync> <ack_type> <ack_byte>
<write_size>`). En la E0 de la Issue #32 (Altirra 4.31 + hub de laboratorio) se observó:

- El hub reenvía cada mensaje del Atari con **un byte final adicional** que se incrementa de uno en uno: 498
  datagramas sin saltos. FEP-003 no lo describe.
- El hub acepta `81` de 6 bytes **sin** ese byte final: 392 de 392 aceptadas, con una latencia media de 1,19 ms hasta
  que Altirra recibe la respuesta.
- Sin respuesta a la sincronización, el hub devuelve el control a Altirra tras unos 100–115 ms.
- El hub elimina a un dispositivo que no transmite. `C4` (*Alive*) cada 5 s lo mantiene registrado.

## Decisión

1. **Componente puro `rb_netsio`.**
   - Incluye solo `rb/core`, `rb/app` y `rb/netsio`; `check_layering.py` lo verifica y prohíbe sockets y `rb_sio`.
   - Contiene el códec (`decode`/`encode*`), el puerto de salida propio `DatagramPort` (enmienda H1 de ADR-002) y
     `NetSioSioTransport : SioTransport`.
   - No se modifican `rb_sio`, `rb_application`, `rb_domain` ni `rb_board`.
2. **Correspondencia con el puerto:**

   | Puerto | NetSIO |
   |---|---|
   | `waitCommandFrame()` | `11` abre la trama; `02`/`01` la llenan (se guardan 5 bytes y se cuentan todos); `18 <sync>` la cierra y deja la sincronización pendiente. Las marcas de tiempo son del reloj del PC, **no tiempo SIO** |
   | `send()` con sincronización pendiente | Debe ser 1 byte (ACK o NAK); se envía como `81 <sync> 01 <byte> 00 00`. `write_size` vale 0: unidad de solo lectura (con escritura, ver [ADR-019](0019-escritura-de-sectores.md)) |
   | `send()` sin sincronización pendiente | Un `02 <bytes>` de 1 a 512 bytes; consume 1 crédito. Sin crédito: `C6 00`, espera de `C7` y, si vence, `TransportError` |
   | `releaseDataLine()` | Una sincronización aún pendiente (trama ignorada o ACK fallido) se responde vacía: `81 <sync> 00 00 00 00` |

   Una sincronización sin trama y una `09` (dato + sincronización de escritura) se responden vacías al instante. Con
   escritura activa, la `09` cierra los datos de una `W`/`P` ([ADR-019](0019-escritura-de-sectores.md)).
   `FE`/`FF` descartan la trama a medias.
3. **Perfil explícito del par (D2):**
   - `Fep003Strict` (por defecto): longitudes exactas de FEP-003.
   - `NetsiohubTrailingByte`: a los mensajes que van del Atari al dispositivo se les quita exactamente un byte final,
     que se registra (los saltos se cuentan) y nunca se interpreta.
   - No hay autodetección.
   - **Un perfil equivocado nunca produce un ACK falso.** La trama queda con longitud o checksum inválidos y el
     despachador la ignora.
4. **Enlace:** `connect()` envía `C2` hasta recibir `C3` (con un número de intentos limitado y espera completa ante
   un rechazo) y después `C1`. Mientras hay conexión, envía `C4` cada `aliveIntervalUs` (5 s) desde dentro de las
   esperas. `disconnect()` responde lo pendiente y envía `C0`.
5. **Memoria (ADR-011):** búferes fijos de recepción y envío de 514 B cada uno. Ninguna reserva durante
   `SioSession::step()`; lo verifica `test_netsio_transport_allocations`.
6. **ESP-IDF (D3):** el `CMakeLists.txt` registra el componente con `idf_component_register`, pero `main` no lo
   requiere. Con `MINIMAL_BUILD` el firmware no lo compila ni lo integra.

## Supuestos pendientes de validar

- **Crédito (D5):** se supone que `81` **no** consume crédito, porque FEP-003 enumera como consumidores solo *data
  bytes, proceed, interrupt*. Se verifica en E1b con Altirra. Si fuera falso, el cambio es local al transporte.
- **Byte final:** se trata como propio del hub (hipótesis H1, validada solo por observación). Atari800 ≥ 6 sin hub
  (perfil `Fep003Strict`) queda sin probar.
- **t5:** `kCompleteDelayUs` se cumple en tiempo del PC. El tiempo emulado entre el ACK y COMPLETE lo decide el
  emulador; no se afirma t5.

## Alternativas descartadas

- **Adaptar `SioSession` a NetSIO:** duplicaría la lógica de transacción y separaría el código del ESP32 del código
  probado contra el emulador.
- **Autodetectar el byte final:** sería heurístico y podría ocultar un cambio de formato del hub.
- **Usar el hub o FujiNet-PC como biblioteca:** contradice ADR-012 y la independencia acordada en la Issue #32.

## Verificación

- `test_netsio_codec`, `test_netsio_transport` y `test_netsio_transport_allocations` (pruebas de host, nivel A del
  diseño E1).
- `tools/tests/test_checks.py` cubre la regla de capas de `rb_netsio`.
- Los niveles B (STATUS con Altirra) y C (READ SECTOR, arranque y reinicio) corresponden a E1b y E1c.
