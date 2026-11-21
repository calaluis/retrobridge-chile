# H4-C: criterio acotado a la casetera (DATA IN y MOTOR)

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
| C5 | Pico de MOTOR ≤ 3,3 V medido en el Atari solo, o un adaptador validado | M3a con osciloscopio | **[P] Bloquea la conexión** |
| C6 | Orden de conexión y criterios de detención escritos | Procedimiento | [P] |
| C7 | Registro de placa, commit, archivo y resultado | Plantilla | Listo |
| C8 | DATA IN liberado en reposo, al terminar la reproducción y tras un fallo de UART o RMT (§3) | Prueba de host `test_esp_data_in` | [F]; **la comprobación física sigue [P]** |
| C9 | VCC_BUF (R7) y corriente del pin 10 (M6): justificar cómo quedan cubiertos en la configuración de prueba, o medirlos | — | **[P] Pendiente de evaluar** |

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

## 4. Lo que sigue abierto

- **C5:** MOTOR. Medición M3a por un tercero (ficha del responsable) o un adaptador que conserve MOTOR, validado.
- **C9:** VCC_BUF y pin 10.
- **C8:** la comprobación física de DATA IN.
- **C6:** el procedimiento de conexión.
- La aprobación de H4-C y su reflejo en el perfil y en `puertas.json`, en un PR aparte con evidencia.

## 5. Fuentes

- `h4-puerta-sio.md` (§2, §3.4, §7 y R1), `h4-puertas-escucha-transmision.md`.
- `docs/mediciones/cassette-lectura-usb-2026-11-20.md`.
- Registro histórico WSS G5-2 T5 (privado, no versionado).
