# Prueba de banco C8: DATA IN en la FujiNet FN32ROV-1.3-Q24, solo por USB

> **Estado: preparada, no ejecutada.** Flashear requiere la confirmación explícita del responsable.
> **Retirada de la CI el 2026-11-25.** El paquete `bench-c8` exigía H4 y M3a sin aprobar. M3a quedó aprobada por
> aceptación de riesgo (`docs/mediciones/puertas-aceptacion-riesgo-2026-11-25.md`), y antes de T9b solo se instala `menu-sio`: el job `firmware-package-bench-c8` ya no
> existe. El código de banco sigue en el árbol. Desde F2 y F3 de C8a (`banco-c8a-aislado.md`), la variante
> `bench-c8a` lo acepta y el job `firmware-package-bench-c8a` lo compila y verifica, sin distribuirlo. Este documento queda como registro.
>
> **Cómo se recupera, cuando el responsable decida validar DATA IN (H4):**
> - Sigue en el árbol: `main/bench_c8.cpp`, `sdkconfig.bench-c8`, `CONFIG_RB_BENCH_C8`,
>   `validate_for_bench_data_in()` (no mira las puertas) y los chequeos del empaquetador (`BENCH_CHECK` y
>   `BENCH_GRANT_CHECK`), que hoy hacen que todo paquete rechace el código de banco.
> - Hace falta, en un PR aparte: una variante de banco en `tools/ci/firmware_package.py` con `bench=True` y
>   `gates=(False, True)` (el estado vigente), sus pruebas y un job de CI como el que se retiró.
> - La prueba sigue siendo solo por USB y con el Atari desconectado. No acepta R2 ni R7, y no cambia la aceptación de R6, que vale solo con USB (`docs/mediciones/riesgos-h4-2026-11-25.md`).
>
> Texto original: **H4 y M3a siguen sin aprobar.** Esta prueba no aprueba ninguna puerta y no conecta el Atari.

**Objetivo:** demostrar en el ESP32 real el criterio C8 de H4-C (`h4-c-casetera.md` §3): GPIO21 (DATA IN) pasa
de UART2 al RMT al reproducir un tramo FSK, vuelve a UART2 al terminar el tramo y también al cancelarlo, y queda
liberado al final.

**Fuera del alcance:** la verificación eléctrica del pin 3 del SIO sigue pendiente. Esta prueba lee la matriz GPIO del
ESP32, es decir, qué periférico tiene el pin. No mide tensiones en la línea.

## 1. Qué hace el firmware de banco

El paquete `bench-c8` es el firmware de `cassette-lectura` con la opción `CONFIG_RB_BENCH_C8`. Desde la fase F1 de C8a
(`banco-c8a-aislado.md` §6) **no se ejecuta al arrancar**: la orden `c8a run` de la consola USB lee SIO_5V en GPIO35
y, solo si no hay tensión del Atari y la lectura es válida, ejecuta **una vez por arranque** `rb::bench::run_c8`
(`main/bench_c8.cpp`):

| Paso | Acción | Dueño esperado de GPIO21 |
|---|---|---|
| 0 | Antes de configurar | Liberado (salida desactivada) |
| 1 | `configure()`: UART2 en DATA IN | UART2 TX |
| 2 | 4 bytes `0x55` por UART2 a 600 baudios | UART2 TX |
| 3 | Tramo A: 1500 porciones de 200 µs (0,3 s), en reproducción | RMT |
| 4 | Tramo A reproducido hasta el final | UART2 TX |
| 5 | Tramo B: 4000 porciones de 250 µs (1 s), en reproducción | RMT |
| 6 | Tramo B cancelado con `stop()`, como haría una bajada de MOTOR | UART2 TX |
| 7 | Sin fallos: `faulted`, `pinFaults` y `failures` en cero | — |
| 8 | `release()`: borra el driver de UART2 y aplica `gpio_reset_pin(21)` | Liberado (salida desactivada) |

- **Estímulo:** sustituye a MOTOR una señal cuadrada de prueba. Con M3a aprobada, `app_main` configura GPIO36
  (MOTOR) como **entrada**; el banco no lo usa.
- **Ruta del pin:** se lee en la matriz GPIO del ESP32, sin escribir ningún registro. El campo `out_sel` vale 198 para
  UART2 TX y entre 87 y 94 para los canales del RMT. `oe` es la habilitación de salida.
- **Llamadas de ESP-IDF:** cada llamada del driver que cambia el pin o sus periféricos se registra con su resultado
  (`0` = `ESP_OK`).
- **Casetera:** desde F1 no arranca en el firmware de banco: su bucle movería el mismo driver de DATA IN.

**Controles del empaquetador** (`tools/ci/firmware_package.py`, variante `bench-c8`):
- H4 y M3a sin aprobar, en el perfil y en el registro;
- `CONFIG_RB_BENCH_C8=y` y la prueba presente en la aplicación y en el ELF;
- la concesión `validate_for_bench_data_in` solo en `main/bench_c8.cpp`, que no escribe registros;
- las mismas llamadas exactas de la casetera.

Las demás variantes rechazan la opción y el código de banco.

## 2. Condiciones (obligatorias)

- FujiNet **solo por USB** al PC, en COM3.
- **Cable SIO desconectado del Atari.** Ningún otro equipo SIO.
- microSD insertada, la misma de la prueba por USB del 2026-11-20.
- La placa funciona hoy con `cassette-lectura` `f405106`. Su paquete verificado está en
  `D:\GitHub\Respaldos\FujiNet-RB-15b153f-D7\`, que es la vuelta atrás.

## 3. Preparación (sin escribir en la placa)

1. Descargar el artefacto `retrobridge-fw-bench-c8-fujinet-atari-v1x-<sha7>` del job `firmware-package-bench-c8` del PR.
   Guardarlo en `D:\GitHub\Respaldos\FujiNet-RB-bench-c8\` y descomprimirlo allí.
2. Comprobar el zip contra el `artifact-digest` que publica el resumen del job.
3. En la carpeta del paquete:

   ```powershell
   $py = "C:\Users\calal\AppData\Local\Temp\claude\6ceb9387d1847330\564fcfbb-bd9a-4a82-8fe3-20459514add9\scratchpad\venv-esptool\Scripts\python.exe"
   ```

   ```powershell
   & $py -I D:\GitHub\retrobridge-chile\tools\ci\firmware_package.py verify .
   ```

   Debe terminar con todas las comprobaciones en OK, entre ellas «prueba de banco C8 (sdkconfig y aplicación)».

## 4. Instalación (solo con la confirmación del responsable)

Desde la carpeta del paquete `bench-c8`, con la misma disposición de flash que `cassette-lectura`. No se usa
`erase_flash` ni se escribe la NVS.

```powershell
& $py -m esptool --chip esp32 --port COM3 --baud 460800 write_flash --flash_mode dio --flash_freq 40m --flash_size 16MB 0x1000 firmware\bootloader.bin 0x8000 firmware\partition-table.bin 0x10000 firmware\retrobridge.bin
```

```powershell
& $py -m esptool --chip esp32 --port COM3 --baud 460800 verify_flash --flash_mode dio --flash_freq 40m --flash_size 16MB 0x1000 firmware\bootloader.bin 0x8000 firmware\partition-table.bin 0x10000 firmware\retrobridge.bin
```

Pasa si las tres regiones dan «Hash of data verified» y «verify OK (digest matched)».

## 5. Ejecución y captura

El script de consola reinicia la placa por RTS, captura 8 s de arranque y después envía las órdenes:

```powershell
& $py C:\Users\calal\AppData\Local\Temp\claude\6ceb9387d1847330\564fcfbb-bd9a-4a82-8fe3-20459514add9\scratchpad\console.py D:\GitHub\Respaldos\FujiNet-RB-bench-c8\consola-c8.txt "cas status"
```

Para repetir la prueba basta con volver a ejecutarlo: se ejecuta en cada arranque.

> **Desde F1 de C8a** el banco no corre al arrancar: la captura debe enviar `c8a run`, y la línea `cassette ready`
> de §6 ya no aparece. Para C8a, la imagen y los pasos están en `banco-c8a-aislado.md` §6 (Estado de F4); la
> instalación de §4 no cambia.

## 6. Salida esperada si pasa

Las marcas de tiempo varían. Deben aparecer, en este orden:

```text
W (…) c8: C8 BENCH BEGIN: board 'fujinet-atari-v1.3/1.5', H4 not signed, M3a not signed; USB only, the Atari must be disconnected
I (…) c8: C8 [PASS] 0 before configure: GPIO21 out_sel=… oe=0 -> released (output off)
I (…) c8: C8 call uart_driver_install -> 0
I (…) c8: C8 call uart_param_config -> 0
I (…) c8: C8 call uart_set_pin -> 0
I (…) data_in: DATA IN on GPIO21: records by UART2, FSK by RMT
I (…) c8: C8 [PASS] 1 configure (UART2 on DATA IN)
I (…) c8: C8 [PASS] 1 configured: GPIO21 out_sel=198 oe=1 -> UART2 TX
I (…) c8: C8 call uart_write_bytes -> 0
I (…) c8: C8 [PASS] 2 four bytes by UART2 at 600 baud
I (…) c8: C8 [PASS] 2 after the bytes: GPIO21 out_sel=198 oe=1 -> UART2 TX
I (…) c8: C8 [PASS] 3 run A queued (1500 x 200 us)
I (…) c8: C8 call rmt_new_tx_channel -> 0
I (…) c8: C8 call rmt_new_simple_encoder -> 0
I (…) c8: C8 call rmt_tx_register_event_callbacks -> 0
I (…) c8: C8 call rmt_enable -> 0
I (…) c8: C8 call rmt_transmit -> 0
I (…) c8: C8 [PASS] 3 run A started
I (…) c8: C8 [PASS] 3 run A playing: GPIO21 out_sel=8x/9x oe=1 -> RMT
I (…) c8: C8 call rmt_disable -> 0
I (…) c8: C8 call rmt_del_channel -> 0
I (…) c8: C8 call rmt_del_encoder -> 0
I (…) c8: C8 call uart_set_pin -> 0
I (…) c8: C8 [PASS] 4 run A played to its end
I (…) c8: C8 [PASS] 4 run A ended: GPIO21 out_sel=198 oe=1 -> UART2 TX
…   (run B: the same calls; stop() disables the channel once)
I (…) c8: C8 [PASS] 6 run B cancelled: GPIO21 out_sel=198 oe=1 -> UART2 TX
I (…) c8: C8 [PASS] 7 no fault (faulted, pinFaults, failures)
I (…) c8: C8 counters: faulted=0 pinFaults=0 failures=0 underruns=0
I (…) c8: C8 call uart_driver_delete -> 0
I (…) c8: C8 call gpio_reset_pin -> 0
I (…) data_in: DATA IN released: UART2 deleted, GPIO21 reset (output off)
I (…) c8: C8 [PASS] 8 released: GPIO21 out_sel=… oe=0 -> released (output off)
I (…) c8: C8 calls: … traced, 0 failed
I (…) c8: C8 RESULT PASS: GPIO21 UART2 -> RMT -> UART2 (end and stop), then released
W (…) c8: C8 BENCH END (electrical check of SIO pin 3 still pending)
I (…) cassette: cassette ready: MOTOR not read (M3a not signed), DATA IN not driven (H4 or M3a not signed)
```

Y la respuesta a `cas status` es `no tape, motor off (…)`.

**Pasa** si aparece `C8 RESULT PASS`, no hay ninguna línea `[FAIL]` ni `(FAIL)`, y la casetera arranca con
«DATA IN not driven».

**No pasa** si hay cualquier `[FAIL]`, una llamada distinta de `0`, `C8 RESULT FAIL`, un reinicio o un brownout. En ese
caso se guarda la captura y se restaura `cassette-lectura` (§7).

`underruns` mayor que 0 no hace fallar la prueba, pero se registra: indica que el RMT se quedó sin datos a mitad de un
tramo.

Cualquier línea `E (…) rmt:` se anota en el registro. Desde `fix/c8-rmt` no debe aparecer ninguna: el fin de un
tramo llega por la notificación del RMT (`on_trans_done`) y el canal se desactiva una sola vez.

### Historial

| Fecha | Paquete | Resultado | Causa |
|---|---|---|---|
| 2026-11-21 | `bench-c8` `d05bfe2` | **FAIL** | La ruta de GPIO21 fue la esperada en los pasos 0–8. Falló una llamada: `stop()` desactivó el canal y `releasePin()` lo desactivó otra vez (`rmt_disable -> 259`, `ESP_ERR_INVALID_STATE`). Además, `rmt_tx_wait_all_done(0)` escribió «flush timeout» como error en cada consulta (unas 110 líneas). Corregido en `fix/c8-rmt`. |

## 7. Restaurar `cassette-lectura` (`f405106`)

Desde `D:\GitHub\Respaldos\FujiNet-RB-15b153f-D7\retrobridge-fw-cassette-lectura-fujinet-atari-v1x-f405106\`:

```powershell
& $py -m esptool --chip esp32 --port COM3 --baud 460800 write_flash --flash_mode dio --flash_freq 40m --flash_size 16MB 0x1000 firmware\bootloader.bin 0x8000 firmware\partition-table.bin 0x10000 firmware\retrobridge.bin
```

```powershell
& $py -m esptool --chip esp32 --port COM3 --baud 460800 verify_flash --flash_mode dio --flash_freq 40m --flash_size 16MB 0x1000 firmware\bootloader.bin 0x8000 firmware\partition-table.bin 0x10000 firmware\retrobridge.bin
```

Al arrancar debe aparecer `App version: f405106` y `cassette ready: MOTOR not read (M3a not signed), DATA IN not driven
(H4 or M3a not signed)`.

Si la placa no arranca, el procedimiento general de recuperación (`procedimiento-instalacion-recuperacion.md` §7)
restaura el volcado completo D7 (R0) o D5 (R1).

## 8. Registro

En `docs/mediciones/banco-c8-AAAA-MM-DD.md`:
- placa, commit del paquete y SHA-256 del zip;
- la salida de esptool;
- la captura completa de la consola;
- el resultado de cada paso;
- la restauración.

Separado en dos apartados:
- **Comprobado en el ESP32:** la ruta de GPIO21 en la matriz y el resultado de las llamadas de ESP-IDF.
- **Pendiente:** la verificación eléctrica del pin 3 del SIO.
