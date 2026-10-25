# H4: validación física del diagnóstico autónomo en la FujiNet FN32ROV-1.3-Q24 (2026-10-24)

> Estado: **diagnóstico de software (`rb_hw_diag`) validado funcionalmente en el equipo**, por USB, microSD y UART0.
> **H4 eléctrico sigue EN PREPARACIÓN y no está aprobado.** No se probó nada del bus SIO, y la placa estuvo
> **desconectada del Atari** durante todas las pruebas.

**Convención:**
- **[H]** hecho comprobado en el equipo, según la campaña del 2026-10-24;
- **[V]** verificado además en el PC o por cálculo independiente;
- **[I]** hipótesis;
- **[P]** pendiente.

## 1. Objetivo

Comprobar en hardware real que el firmware H4 (`15b153f`), compilado y empaquetado por CI, arranca, ofrece el
diagnóstico autónomo (`diag status`, `memory`, `storage`, `events`, `report`) y sigue leyendo la microSD y las
imágenes ATR como el firmware H3c. Diseño del módulo: `docs/diseno/h4-diagnostico-autonomo.md`; procedimiento:
`docs/hardware/procedimiento-instalacion-recuperacion.md` §6.

## 2. Entorno

| Elemento | Valor |
|---|---|
| Placa | FujiNet Atari **FN32ROV-1.3-Q24**, ESP32-WROVER-B (`docs/hardware/fujinet-atari-v1x.md`) |
| Procesador | ESP32-D0WD, revisión 1.0 |
| Flash | 16 MB; DIO a 40 MHz |
| Firmware | `15b153f267cf6766978bd786eb1b463ea0e4ac50`; el ESP32 reporta la versión `15b153f` |
| Componente validado | `rb_hw_diag`, con el adaptador `EspSystemProbe` y el comando `diag` |
| Compilación | ESP-IDF v6.1, C++17 |
| Origen del binario | Job `firmware-package-h4`, ejecución `40133891881` de GitHub Actions, variante `diag` |
| Artefacto | `retrobridge-fw-diag-fujinet-atari-v1x-15b153f` |
| SHA-256 del zip | `bf818b48a39f18ae673a6b54c6a79f648c9b75bb620de30354314a9354e2e2e5` |
| Consola | UART0, COM3, 115 200 baudios |
| Conexiones | **Solo USB y microSD.** Atari desconectado y SIO sin conectar |
| Recuperación disponible | Respaldos WSS D5/D6 y paquete H3c `1b1527d`. Se guardan de forma privada y **no se versionan** |

## 3. Metodología

1. **Integridad del paquete** [H]: se comprobó el SHA-256 del zip y luego `SHA256SUMS`, con 17 archivos correctos y
   0 errores.
2. **Escritura** [H]: con esptool.py v4.5.1, en DIO, 40 MHz y 16 MB, el bootloader en `0x1000`, la tabla de
   particiones en `0x8000` y la aplicación en `0x10000`. Las tres imágenes devolvieron `Hash of data verified`. No se
   usó `erase_flash`.
3. **Consola por UART0**, con esta cronología confirmada por el equipo:
   1. `diag status`, `diag events`, `diag memory` y `diag storage`;
   2. `diag report`, con **exactamente 2 eventos** registrados (`Boot` y `StorageMounted`);
   3. `atr bench retrobridge/h3c/dd-1440.atr rand 100`, que agregó `BenchmarkCompleted`. `diag storage` mostró
      después sus contadores (§4.3);
   4. `atr crc retrobridge/h3c/dd-720.atr`, que agregó `VerifyCompleted`. `diag storage` lo registró (§4.4);
   5. `diag events`, con **4 eventos** (§4.5).
4. **Verificación del informe en el PC** (Windows): `python tools/check_diag_report.py diag-report-15b153f.txt`.
5. **Ciclo de alimentación controlado**: se desconectó y se volvió a conectar el USB para observar el motivo de
   reinicio (§4.1).

Todos los comandos del paso 3 son de lectura: no escriben en la microSD, en la flash ni en NVS, y no tocan pines del
SIO.

## 4. Resultados

### 4.1 Arranque e identidad (`diag status`) [H]

| Campo | Resultado |
|---|---|
| Proyecto / versión / placa | `retrobridge` / `15b153f` / `fujinet-atari-v1.3/1.5` |
| Task watchdog | Activado, 5 000 ms, sin pánico |
| Interrupt watchdog | Activado, 300 ms |
| microSD | Montada, sin errores de almacenamiento |
| **Primer arranque tras el flasheo** | `boot reason=OtherWatchdog boots_since_power_on=1 watchdog_resets=1 panic_resets=0 brownout_resets=0` |
| **Después de desconectar y reconectar el USB** | `boot reason=PowerOn boots_since_power_on=1 watchdog_resets=0 panic_resets=0 brownout_resets=0` |

Los dos registros son coherentes con el diseño del historial de reinicios:
- en el primer arranque, la memoria RTC no traía un registro válido, así que el historial empezó en el arranque 1 y
  contó el reinicio por watchdog;
- el encendido posterior reinició el historial (`PowerOn`).

Ver la observación O1 (§5).

### 4.2 Memoria (`diag memory`) [H]

| Medición | Bytes |
|---|---:|
| Libre | 280 128 |
| Mínima histórica | 276 264 |
| Bloque mayor | 147 456 |
| Interna | 280 128 |
| Libre antes del benchmark (`atr bench`) | 279 980 |
| Libre después del benchmark | 279 892 |
| Diferencia observada | 88 |

Es una medición puntual. **No demuestra la ausencia de fugas** a largo plazo; la Meta B (H7) exige 10 000 lecturas
sin variación del heap.

### 4.3 Benchmark de almacenamiento [H]

`atr bench retrobridge/h3c/dd-1440.atr rand 100`:

| Indicador | Valor |
|---|---:|
| Lecturas / errores / muestras descartadas | 100 / 0 / 0 |
| Latencia mínima (µs) | 1 125 |
| Media (µs) | 1 826 |
| P50 (µs) | 2 081 |
| P95 (µs) | 3 051 |
| P99 (µs) | 3 190 |
| Máxima (µs) | 3 969 |
| Presupuesto / veces excedido | 5 000 µs / 0 |

- `diag storage` mostró contadores **idénticos** a los del benchmark.
- El presupuesto de 5 000 µs es del **diagnóstico de almacenamiento**. No es una validación eléctrica ni temporal
  del protocolo SIO.
- [V] Comparado con H3c (`h3c-validacion-fisica-2026-10-24.md` §4.5, aleatorio con 100 lecturas: media 1 807 µs y
  máxima 3 903 µs), la misma imagen en el mismo modo da valores del mismo orden.

### 4.4 Lectura ATR [H]

`atr crc retrobridge/h3c/dd-720.atr` terminó con `DONE dd-720 sectors=720 read_errors=0`. Después, `diag storage`
mostró:
- `verify_runs=1 sectors=720 verify_errors=0 sector_read_errors=0`;
- conservó los contadores del benchmark: `runs=1 reads=100 errors=0 budget_exceeded=0`.

En esta ejecución **no** se compararon los 720 CRC contra el `manifest.csv` histórico. Lo verificado es la lectura
sin errores de los 720 sectores y su registro en el diagnóstico. La comparación completa de 4 640 CRC ya quedó
hecha en H3c con el firmware `1b1527d`.

### 4.5 Registro de eventos (`diag events`) [H][V]

`total=4 kept=4 lost=0 capacity=32`:

| # | Tipo | Valor |
|---|---|---|
| 1 | `Boot` | — |
| 2 | `StorageMounted` | — |
| 3 | `BenchmarkCompleted` | `0x00000f81` = **3 969 µs** [V]: coincide con la latencia máxima de §4.3 |
| 4 | `VerifyCompleted` | — |

El orden coincide con la cronología de §3: los eventos 3 y 4 se agregaron después de capturar el informe de §4.6.

### 4.6 Informe verificable (`diag report`) [H][V]

| Campo | Valor |
|---|---|
| Formato / fuente | `1` / `retrobridge-hw-diag` |
| Líneas declaradas | 13 |
| CRC-32 | `b83ae52b` |
| Verificación en el PC | `python tools/check_diag_report.py diag-report-15b153f.txt` → **`OK: report verified (13 lines)`** |

Queda validado el formato, el número de líneas y el CRC-32 de un informe capturado desde el hardware físico.

**Coherencia con la cronología [H][V]:**
- El informe se generó cuando había **exactamente 2 eventos** (`Boot` y `StorageMounted`), antes de `atr bench` y
  de `atr crc` (§3, paso 3.2).
- En el formato 1 el informe tiene 11 líneas fijas más una por evento conservado (`ReportWriter`): 11 + 2 =
  **13 líneas**, que es lo declarado y lo verificado.
- Por eso este informe **no** contiene los contadores del benchmark ni de la verificación ATR. Esos datos vienen de
  las salidas posteriores de `diag storage` y `diag events` (§4.3–§4.5).
- La captura es privada y no se versiona.

## 5. Observaciones

| # | Observación | Análisis | Estado |
|---|---|---|---|
| O1 | Primer arranque tras el flasheo con `boot reason=OtherWatchdog` (`ESP_RST_WDT`, "otros watchdogs"). En el arranque siguiente, tras el ciclo de USB: `PowerOn`, sin watchdog | **Origen identificado el 2026-10-25:** es el reinicio `RTCWDT` (`rst:0x10`) de la ROM tras `flash read err, 1000`, capturado en 8 de 8 arranques de T1 (`arranque-rom-conocido-2026-10-25.md` §1 y §3.1). Ocurre antes del bootloader y de RetroBridge. Causa raíz sin confirmar | **Comportamiento conocido de esta unidad** (`arranque-rom-conocido-2026-10-25.md` §8) |
| O2 | Diferencia de 88 bytes de heap alrededor de un benchmark de 100 lecturas | Medición puntual | **[P]** Seguimiento con más lecturas (H3-13; Meta B en H7) |
| O3 | Informe de 13 líneas frente a los 4 eventos que mostró al final `diag events` | **Resuelta por la cronología confirmada** (§3, §4.6): el informe se generó con 2 eventos (11 + 2 = 13 líneas); `BenchmarkCompleted` y `VerifyCompleted` se agregaron después | **Resuelta**: no es una discrepancia |

Siguen abiertas las demás observaciones de H3c (`h3c-validacion-fisica-2026-10-24.md` §5). El
`flash read err, 1000` del primer encendido de H3c y O1 resultaron ser el mismo fenómeno (`arranque-rom-conocido-2026-10-25.md`).

**Seguimiento de O1:** el `OtherWatchdog` de arranque **se repitió el 2026-10-25**, en una sesión sin flasheo
(`h4-regresion-h3c-2026-10-25.md` §5). La captura de la ROM (T1) lo relacionó con el reinicio `RTCWDT` y se
registró como comportamiento conocido: `arranque-rom-conocido-2026-10-25.md`. RES-3.8 sigue siendo una hipótesis, sin confirmar.

## 6. Matriz de pruebas

| Prueba | Origen | Estado | Evidencia |
|---|---|---|---|
| Integridad del paquete (zip y 17 archivos) | `toolchain.md`, paquete H4 | Aprobada | §3 |
| Escritura y verificación de las tres imágenes | Procedimiento §6 | Aprobada | Tres `Hash of data verified` |
| Identidad del firmware (`15b153f`) y perfil | Procedimiento §6 | Aprobada | §4.1 |
| Configuración del watchdog | `h4-diagnostico-autonomo.md` §3 | Aprobada | §4.1 |
| Historial de reinicios (encendido) | Ídem | Aprobada | `PowerOn`, arranque 1 (§4.1) |
| Historial de reinicios (watchdog) | Ídem | Observado y explicado: `RTCWDT` de la ROM (2026-10-25) | O1; `arranque-rom-conocido-2026-10-25.md` |
| `diag memory` | Ídem | Aprobada | §4.2 |
| Benchmark y su registro en `diag storage` | Ídem | Aprobada | §4.3 |
| Lectura ATR y su registro | Ídem | Aprobada | §4.4 |
| `diag events` | Ídem | Aprobada | §4.5 |
| `diag report` verificado en el PC | Ídem, §4.1 | Aprobada | §4.6: 13 líneas y CRC `b83ae52b`, generado con 2 eventos según la cronología confirmada |
| Comparación de los 720 CRC contra el manifiesto en esta sesión | Procedimiento §6.4 | No ejecutada (24-10) | Hecha en H3c con `1b1527d`. Con H4, la comparación completa de 4 640 CRC se hizo el 2026-10-25 (`h4-regresion-h3c-2026-10-25.md`) |
| Regresión completa de H3c (5 imágenes, `drive read`, `seq`) | Procedimiento §6.4 | Parcial (24-10) | Solo `dd-1440` en modo aleatorio y `dd-720` con `atr crc`. **Completada el 2026-10-25** en una campaña aparte (`h4-regresion-h3c-2026-10-25.md`) |
| MOTOR CONTROL / GPIO36 (M3a) | `h4-puerta-sio.md` | Pendiente | — |
| Niveles y aislamiento del bus SIO (M0–M6) | Ídem | Pendiente | — |
| Recepción y transmisión SIO (H5–H7) | Arquitectura §11 | Pendiente | — |
| Estabilidad prolongada | — | Pendiente | — |

## 7. Alcance

**Validado [H]:** el funcionamiento del diagnóstico interno `rb_hw_diag` en hardware real (identidad, reinicios,
watchdog, memoria, almacenamiento, latencias, eventos e informe verificable por CRC), con el firmware `15b153f` por
USB, microSD y UART0.

**No validado:**
- el comportamiento eléctrico del bus SIO conectado al Atari;
- la evaluación de MOTOR CONTROL y GPIO36 (M3a, punto de detención) y las mediciones M0–M6;
- la seguridad de los niveles de tensión del bus;
- la integración con el Atari y la recepción y transmisión SIO (H5–H7);
- la estabilidad de larga duración;
- la causa raíz del fallo de la ROM que produce el `OtherWatchdog` inicial (`arranque-rom-conocido-2026-10-25.md`).

El software **no sustituye** a un multímetro ni a un osciloscopio, y **no protege** por sí mismo al ESP32 contra
sobretensiones (`h4-diagnostico-autonomo.md` §6).

## 8. Conclusiones

1. El firmware H4 `15b153f`, compilado y verificado por CI (variante `diag`), arranca en la FN32ROV-1.3-Q24 y ofrece
   los cinco comandos `diag` con datos coherentes entre sí y con las operaciones realizadas.
2. El informe `diag report` capturado del equipo **verifica con la herramienta de PC** (formato, líneas y CRC-32).
3. La lectura de la microSD y de las imágenes ATR sigue funcionando: 100 lecturas aleatorias y 720 sectores sin
   errores, con latencias del mismo orden que en H3c.
4. **H4 eléctrico continúa EN PREPARACIÓN.** Esta validación no aprueba la puerta de hardware SIO.

## 9. Trazabilidad y privacidad

- **Firmware:** `15b153f267cf6766978bd786eb1b463ea0e4ac50`.
- **Paquete:** `retrobridge-fw-diag-fujinet-atari-v1x-15b153f`, de la ejecución `40133891881`; zip
  `bf818b48a39f18ae673a6b54c6a79f648c9b75bb620de30354314a9354e2e2e5`.
- **Firmware anterior:** paquete H3c `1b1527d` (`h3c-validacion-fisica-2026-10-24.md`).
- **Lo que no se versiona:** volcados D5/D6, capturas completas de consola (incluida `diag-report-15b153f.txt`),
  direcciones MAC, credenciales ni rutas privadas. Este informe resume los resultados suministrados por el equipo y no
  reproduce capturas.
