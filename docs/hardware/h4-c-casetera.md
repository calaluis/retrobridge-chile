# H4-C: criterio acotado a la casetera (DATA IN y MOTOR)

> **Estado vigente (2026-11-25):** **M3a aprobada por aceptación de riesgo** del responsable, que cubre solo R1
> (`docs/mediciones/puertas-aceptacion-riesgo-2026-11-25.md`). **H4 sigue sin aprobar:** R2 y R7 no están aceptados; R6 está aceptado solo con la FujiNet por USB (`docs/mediciones/riesgos-h4-2026-11-25.md`). **La validación eléctrica sigue
> pendiente** para las dos. Antes del banco T9b solo se instala `menu-sio`.
> Esa aprobación no viene de H4-C. Las variantes `cassette-lectura` y `bench-c8` se retiraron, y `menu-sio` exige
> ahora M3a aprobada y H4 sin aprobar. El §4 describe el estado anterior.
>
> **PROPUESTA NO APROBADA.** Este documento no aprueba H4 ni M3a, no cambia `docs/mediciones/puertas.json` y no
> autoriza ninguna conexión al Atari ni ningún flasheo. Todo lo que dice «cubierto» es evidencia; la decisión es
> del responsable.

**Convención:**
- **[F]** firmware revisado o probado en el PC;
- **[E]** esquema;
- **[H]** histórico;
- **[P]** pendiente.

## 1. Alcance

Para leer una cinta, la FujiNet FN32ROV-1.3-Q24 usa solo dos señales del SIO:
- **DATA IN** hacia el Atari: **pin 3**. Ruta: GPIO21 → U4 74LS07 (colector abierto) → SIO_DATAIN (`h4-puerta-sio.md` §2).
- **MOTOR**: **pin 8**, que va directo a GPIO36, con R2 de 10 k a GND.

Para esta prueba sobran COMMAND, DATA OUT, la escucha (H4-L) y los tiempos del bus (M5). La continuidad se comprueba
en los pines 3 y 8.

## 2. Requisitos

| # | Requisito | Evidencia | Estado |
|---|---|---|---|
| C1 | Entre los pines del SIO, la casetera configura solo GPIO21 (salida hacia U4) y GPIO36 (entrada) | Revisión de código; `firmware_package.py` («GPIO/UART del SIO solo en rb_esp_cassette») | [F] |
| C2 | Mismos pines y periféricos que la beta del responsable, `v1.6.2-atari-fsk-beta.1`: UART2 y RMT en GPIO21, GPIO36 como entrada | Comparación de código | [F] |
| C3 | DATA IN pasa por el 74LS07 de colector abierto | Esquema de la FN32ROV-1.3-Q24 | [E] |
| C4 | Carga turbo completada con MOTOR activo durante 15 min 27 s (G5-2 T5). El Atari usado no está identificado (65XE u 800XL) | Registro histórico WSS | [H], solo funcional |
| C5 | Pico de MOTOR ≤ 3,3 V medido en el Atari solo, o una protección de GPIO36 validada (etapas V de `evaluacion-74lvc4245-motor.md`) | M3a con osciloscopio; ensayo de la protección | **[P] Bloquea la conexión** |
| C6 | Orden de conexión y criterios de detención escritos | Procedimiento | [P] |
| C7 | Registro de placa, commit, archivo y resultado | Plantilla | Listo |
| C8 | DATA IN liberado en reposo, al terminar la reproducción y tras un fallo de UART o RMT (§3) | Prueba de host `test_esp_data_in` | [F] en host |
| C8a | **Matriz GPIO y control de UART2/RMT** en el ESP32: GPIO21 pasa de UART2 al RMT y vuelve, al terminar y al cancelar | Banco C8 (`bench-c8`), por USB y **sin el Atari** | [P]. **No** demuestra niveles eléctricos |
| C8b | **Nivel eléctrico del pin 3** en reposo, tras un tramo FSK y tras soltar la línea | Ensayo acotado de §3.1, con el Atari conectado | [P]. Solo **después** de C5 (MOTOR resuelto) |
| C9 | VCC_BUF (R7) y corriente del pin 10 (M6): justificar cómo quedan cubiertos en la configuración de prueba, o medirlos | — | **[P] Pendiente de evaluar.** `brownout=0` y la lectura del pin 10 por sí solos **no** lo cubren |

La prueba con el 65XE quedará registrada aparte, como evidencia aportada por el responsable.

## 3. C8: DATA IN liberado

**Nivel de reposo.** El 74LS07 suelta la línea solo cuando GPIO21 está en alto. Si GPIO21 queda en bajo, el bus queda
forzado a bajo. Reposo es *mark*, es decir, alto.

**Comportamiento del driver** (`components/rb_esp_cassette/src/esp_data_in.cpp`):

| Caso | Qué hace | Prueba de host |
|---|---|---|
| Configuración | La UART2 toma GPIO21; su TX en reposo está en alto | `test_configure_gives_the_pin_to_the_uart` |
| Tramo FSK | El RMT arranca en alto (`init_level`) y termina en alto (`eot_level`); luego el pin vuelve a la UART | `test_a_run_starts_and_ends_at_mark` |
| `stop()`: baja MOTOR o `cas eject` | Se aborta el RMT y el pin vuelve a la UART | `test_stop_mid_run_gives_the_pin_back_to_the_uart` |
| **La UART no acepta el pin de vuelta** (`uart_set_pin` falla) | 1) registra el error; 2) **recuperación inmediata**: `gpio_reset_pin(GPIO21)` lo devuelve a su estado por defecto: salida desactivada y pull-up, así que el 74LS07 suelta la línea; 3) vacía la cola FSK; 4) queda en fallo y **rechaza** `send()`, `setBaud()`, `push()` y `pump()` | `test_release_failure_after_a_run_…`, `…_on_stop_…`, `…_after_rmt_enable_fails_…` |
| Falla también `gpio_reset_pin` | Sigue en fallo, rechaza todo y lo registra como «NOT reset» | `test_a_failed_safe_state_is_still_a_fault` |
| **Recuperación explícita** | `recover()` reintenta devolver el pin a la UART. Si funciona, sale del fallo; si no, el pin sigue sin salida. Nada se reintenta solo | `test_recover_keeps_the_fault_…`, `test_recover_gives_the_pin_back_…`, `test_recover_without_a_fault_…` |

Hoy no hay ningún comando de consola que llame a `recover()`. Tras un fallo, la salida de la casetera queda apagada
hasta reiniciar la placa. Conectar `recover()` a la consola es un paso posterior.

**Bytes ya en cola cuando baja MOTOR (comportamiento documentado):**
- El driver **no vacía** la UART. Los bytes de un bloque `data` que ya se entregaron a `uart_write_bytes` **terminan de
  salir**, y después la línea queda en reposo, en alto. `stop()` solo afecta a los tramos FSK
  (`test_stop_leaves_bytes_already_handed_to_the_uart`).
- El reproductor entrega los bytes al ritmo del reloj de la cinta, y solo mientras MOTOR está activo. A 600 baudios y
  con un ciclo de 2 ms, normalmente entrega **un byte cada vez**, unos 17 ms de señal. Después de que baja MOTOR no se
  entrega ninguno más.
- El caso peor es una pausa larga del ciclo con MOTOR activo: puede haber hasta `CassettePlayer::kMaxSend` (512) bytes
  en cola.
- Es lo mismo que pasa con una casetera real: la cinta sigue un instante después de cortar el motor. No fuerza la
  línea a bajo una vez terminados.

**Lo que esta prueba no demuestra:**
- **Niveles eléctricos en el bus**, tiempos, ni que el ESP-IDF real se comporte como el modelo (`test/host/fakes/idf`).
- Que el pin quede en alto durante el instante entre borrar el canal RMT y devolverlo a la UART.

**Comprobación física pendiente [P]**, por USB y sin el Atari: leer GPIO21, o medir el pin 3 de J1, en reposo, después
de un tramo FSK y después de `cas eject`.

**Prueba de banco C8 preparada** (`procedimiento-prueba-banco-c8.md`, paquete `bench-c8`): comprueba en el ESP32, por la
matriz GPIO, que GPIO21 pasa de UART2 al RMT y vuelve a UART2 al terminar y al cancelar un tramo, y registra cada llamada
de ESP-IDF. No mide el pin 3: la verificación eléctrica sigue [P].

### 3.1 Ensayo acotado C8b (propuesta, 2026-11-24)

**La dependencia circular.** C8b pide medir DATA IN con el Atari. Pero la casetera solo transmite con H4 y M3a
aprobadas, y H4-C pide C8b para que el responsable pueda decidir H4. La salida es la misma que en C8a: un paquete
de **banco**, con las puertas cerradas, que transmite un patrón fijo y nada más. Ese paquete **no** aprueba H4 ni
M3a, y no reproduce cintas.

**Condiciones previas:**

1. C5 resuelto: M3a medida, y una protección de GPIO36 validada o un pico ≤ 3,3 V.
2. C8a hecho (banco C8 por USB, sin el Atari).
3. Autorización escrita del responsable, una por sesión de ensayo.

**El paquete.** Es una variante de banco nueva, por ejemplo `bench-c8b`, añadida en un PR aparte:

- igual que `bench-c8`: `closed_gates=True` y `bench=True`, con `puertas.json` sin cambios;
- no lee MOTOR;
- el arranque normal del firmware no transmite nada; el patrón sale solo con una orden por la consola USB
  (`c8b run`);
- el patrón es fijo y dura como mucho 2 s:
  1. 200 ms en reposo (marca);
  2. 16 bytes `0x55` a 600 baudios por UART2;
  3. 500 ms de FSK por el RMT;
  4. vuelta a UART2 en marca;
  5. liberación de GPIO21;
- después de cada paso, el paquete verifica la matriz GPIO con `route()` y escribe el resultado en la consola.

**El montaje:**

- el Atari está encendido en el `READY` de BASIC o en la pantalla del menú, **sin START**;
- MOTOR se comprueba en 0 V con el multímetro en el pin 8 antes de empezar, porque el paquete no lo lee;
- el osciloscopio mide el pin 3: nivel en marca, nivel bajo, flancos y el nivel después de soltar la línea.

**Criterios de parada:** cualquier lectura de `route()` distinta de la esperada, MOTOR ≠ 0 V, un `brownout` o un
`panic`, o un nivel en el pin 3 fuera de 0–5,25 V.

**Lo que no prueba:** la carga de una cinta (eso es T9b), ni nada de MOTOR.

## 4. Lo que sigue abierto

- **C5:** MOTOR. Medición M3a por un tercero (ficha del responsable) o un adaptador que conserve MOTOR, validado.
- **C9:** VCC_BUF y pin 10. `brownout=0` o la lectura del pin 10 no bastan.
- **C8a:** el banco C8 por USB y sin el Atari. Valida la matriz GPIO y el control de UART2/RMT, no los niveles.
- **C8b:** el nivel eléctrico del pin 3 con el Atari conectado. Va después de C5, con el ensayo acotado de §3.1.
- **C6:** el procedimiento de conexión.
- La aprobación de H4-C y su reflejo en el perfil y en `puertas.json`, en un PR aparte con evidencia.

**H4-C no reemplaza a H4 en el empaquetador (2026-11-24).** `tools/ci/firmware_package.py` abre DATA IN y MOTOR
solo con H4 y M3a aprobadas en `docs/mediciones/puertas.json` (variante `cassette`, `gates=True`); las variantes
`cassette-lectura`, `bench-c8` y `menu-sio` exigen las dos cerradas. H4-C es el criterio acotado con que el
responsable podría decidir H4 para la casetera. Mientras no lo haga, en un PR aparte, las puertas siguen
cerradas y el empaquetador no cambia. T9a de `menu-cinta` (código y host) no depende de H4-C; T9b (banco) sí.

## 5. Fuentes

- `h4-puerta-sio.md` (§2, §3.4, §7 y R1), `h4-puertas-escucha-transmision.md`.
- `docs/mediciones/cassette-lectura-usb-2026-11-20.md`.
- Registro histórico WSS G5-2 T5 (privado, no versionado).
