# H4: regresión funcional de H3c con el firmware de diagnóstico 15b153f (2026-10-25)

> Estado: **regresión de almacenamiento H3c → H4 aprobada en el equipo.** Incorporar `rb_hw_diag` no alteró la
> lectura ATR validada en H3c: la Meta A se reprodujo con 4 640 de 4 640 CRC32.
>
> **H4 eléctrico sigue EN PREPARACIÓN y no está aprobado.** Solo USB y microSD, **sin el Atari**, sin acceso al bus
> SIO y sin mediciones eléctricas.

**Convención:**
- **[H]** hecho comprobado en el equipo, según la campaña del 2026-10-25;
- **[V]** verificado además en el PC o por cálculo independiente;
- **[I]** hipótesis;
- **[P]** pendiente.

**Campañas relacionadas,** que se mantienen separadas:
- `h3c-validacion-fisica-2026-10-24.md`: H3c, firmware `1b1527d`, la referencia de esta regresión;
- `h4-validacion-diagnostico-2026-10-24.md`: primera validación del diagnóstico H4.

## 1. Objetivo

Comprobar que el firmware H4 `15b153f`, que agrega el diagnóstico autónomo `rb_hw_diag` y el comando `diag`,
conserva las funciones de almacenamiento validadas en H3c:
- geometrías ATR;
- la Meta A (CRC32 de todos los sectores);
- la unidad virtual D1 con límites y recuperación;
- el rendimiento de lectura.

Además, comprobar que el diagnóstico registra correctamente todas esas operaciones. Es el paso 4 del procedimiento
§6 (`docs/hardware/procedimiento-instalacion-recuperacion.md`).

## 2. Entorno

| Elemento | Valor |
|---|---|
| Placa | FujiNet Atari **FN32ROV-1.3-Q24**, ESP32-WROVER-B |
| Procesador | ESP32-D0WD, revisión 1.0 |
| Firmware | `15b153f267cf6766978bd786eb1b463ea0e4ac50`, el mismo instalado el 2026-10-24 desde la ejecución `40133891881`. **Esta campaña no volvió a flashear** |
| ESP-IDF | v6.1 |
| Consola | UART0, COM3, 115 200 baudios |
| Almacenamiento | microSD FAT32 de pruebas, con las cinco imágenes de `tools/make_atr.py` |
| Conexiones | **Solo USB y microSD.** Atari desconectado; sin bus SIO y sin mediciones eléctricas |

## 3. Procedimiento

1. Referencia independiente en el PC: `python tools/make_atr.py --out <dir>`, que genera `manifest.csv`.
2. En el equipo:
   - `atr info` de las cinco imágenes;
   - `atr crc` de las cinco imágenes, capturado en `crc-h4-regresion-2026-10-25.txt`;
   - operaciones de la unidad D1 (§4.3);
   - `atr bench` en modo secuencial con 100 lecturas y en modo aleatorio con 1 000;
   - `diag storage` y `diag events`;
   - `diag report` al final.
3. En el PC:
   - `python tools/compare_crc.py manifest.csv crc-h4-regresion-2026-10-25.txt`;
   - `python tools/check_diag_report.py diag-regresion-2026-10-25.txt`.

Los registros completos (`crc-h4-regresion-2026-10-25.txt` y `diag-regresion-2026-10-25.txt`) son evidencia local y
**no se versionan**.

## 4. Resultados

### 4.1 Geometrías ATR [H]

| Imagen | Sectores | Interpretación |
|---|---:|---|
| `sd-720.atr` | 720 | 128 bytes |
| `ed-1040.atr` | 1040 | 128 bytes |
| `dd-720.atr` | 720 | 256 bytes nominales |
| `dd-1440.atr` | 1440 | 256 bytes nominales, no estándar |
| `sd-trailing.atr` | 720 | 128 bytes, con 50 bytes sobrantes |

Los sectores 1–3 de las imágenes DD conservan su tamaño de 128 bytes. El resultado es igual que en H3c.

### 4.2 Meta A: CRC32 [H][V]

| Comprobación | Resultado |
|---|---|
| `compare_crc.py manifest.csv crc-h4-regresion-2026-10-25.txt` | **`compare_crc: OK (5 image(s), 4640 sector(s))`** |
| Sectores coincidentes | **4 640 / 4 640** |
| Diferencias / sectores faltantes | 0 / 0 |
| Imágenes correctas | 5 / 5 |

La Meta A de H3c se reproduce con el firmware H4. [V] El total coincide con el `manifest.csv` de `make_atr.py`:
720 + 1040 + 720 + 1440 + 720.

### 4.3 Unidad virtual D1 con `dd-720.atr` [H][V]

| Operación | Resultado |
|---|---|
| `drive mount` | Correcto |
| `drive read 1` | 128 bytes, CRC `1127658a` |
| `drive read 3` | 128 bytes, CRC `b0a05e7f` |
| `drive read 4` | 256 bytes, CRC `ef1a5c7d` |
| `drive read 720` | 256 bytes, CRC `c8647bb8` |
| `drive read 721` | `SectorOutOfRange`, **error esperado** (fuera de la geometría) |
| Segunda lectura del sector 720 | Correcta |
| `drive status` final | `last operation ok` |
| `drive unmount` | Correcto |
| `sd status` | `open image files: 0` |

- [V] Los cuatro CRC coinciden con las filas de `dd-720` del `manifest.csv` de `make_atr.py`.
- El diagnóstico conservó `last_error=SectorOutOfRange` y `sector_read_errors=1`. Ese conteo corresponde al
  **intento deliberado** de leer el sector 721, no a una lectura fallida inesperada.

### 4.4 Rendimiento (`atr bench`, `dd-1440.atr`) [H][V]

| Métrica | Secuencial, 100 | Aleatorio, 1000 |
|---|---:|---:|
| Lecturas | 100 | 1000 |
| Errores | 0 | 0 |
| Mínima (µs) | 69 | 1122 |
| Media (µs) | 602 | 1866 |
| P50 (µs) | 122 | 2083 |
| P95 (µs) | 1143 | 3069 |
| P99 (µs) | 1161 | 3148 |
| Máxima (µs) | 1939 | 3995 |
| Muestras descartadas | 0 | 0 |
| Presupuesto excedido | 0 | 0 |

**Comparación con H3c** (`h3c-validacion-fisica-2026-10-24.md` §4.5) [V]:

| Métrica | H3c secuencial, 100 | H4 | Diferencia | H3c aleatorio, 1000 | H4 | Diferencia |
|---|---:|---:|---:|---:|---:|---:|
| Media (µs) | 595 | 602 | +1,2 % | 1845 | 1866 | +1,1 % |
| Máxima (µs) | 1928 | 1939 | +0,6 % | 3953 | 3995 | +1,1 % |
| P50 (µs) | 216 | 122 | −43,5 % | 2048 | 2083 | +1,7 % |
| P99 (µs) | 1663 | 1161 | −30,2 % | 3110 | 3148 | +1,2 % |

- Las **medias y los máximos** difieren alrededor del 1 % en los dos modos.
- En el modo aleatorio, todos los percentiles también difieren menos del 2 %.
- En el modo **secuencial**, la mediana y el P99 varían bastante más, y a la baja. [I] Es compatible con la mayor
  sensibilidad de las lecturas secuenciales a efectos de caché y de agrupación de sectores, con solo 100 muestras.
- **No se observa una degradación sistemática,** pero **con una ejecución por modo no se puede afirmar equivalencia
  estadística.**
- Los dos benchmarks mostraron **el mismo heap libre antes y después** de cada ejecución. Esto **no demuestra la
  ausencia de fugas** a largo plazo; la Meta B (H7) exige 10 000 lecturas sin variación del heap.
- El presupuesto de 5 000 µs es **exclusivamente de almacenamiento**. No valida tiempos del protocolo SIO.

### 4.5 Diagnóstico de almacenamiento (`diag storage`) [H]

```
verify_runs=5 sectors=4640 verify_errors=0 sector_read_errors=1
runs=2 reads=1100 errors=0 budget_us=5000 budget_exceeded=0
```

- La microSD permaneció montada y no hubo fallos de montaje.
- Las cinco verificaciones suman los 4 640 sectores de §4.2, y los dos benchmarks suman 1 100 lecturas (§4.4).
- El único error contado es el `SectorOutOfRange` intencional (§4.3).

### 4.6 Registro de eventos (`diag events`) [H][V]

`total=10 kept=10 lost=0 capacity=32`:

| # | Tipo | Valor |
|---|---|---|
| 1 | `Boot` | — |
| 2 | `StorageMounted` | — |
| 3–7 | `VerifyCompleted` (×5) | Uno por imagen (§4.2) |
| 8 | `SectorReadError` | Sector 721, `SectorOutOfRange` (§4.3) |
| 9 | `BenchmarkCompleted` | `0x00000793` = **1 939 µs** [V], el máximo secuencial |
| 10 | `BenchmarkCompleted` | `0x00000f9b` = **3 995 µs** [V], el máximo aleatorio |

El orden coincide con la secuencia de §3.

### 4.7 Informe final (`diag report`) [H][V]

| Informe | Resultado |
|---|---|
| Primero de la sesión | `DIAG END lines=21 crc32=d0b622f5` |
| Segundo, capturado desde COM3 con Python (`diag-regresion-2026-10-25.txt`) | `python tools/check_diag_report.py diag-regresion-2026-10-25.txt` → **`OK: report verified (21 lines)`** |

- El segundo informe contiene el firmware `15b153f`, los 4 640 sectores verificados, las 1 100 lecturas de
  benchmark, los 10 eventos sin pérdidas y el error intencional de sector fuera de rango.
- [V] El número de líneas es coherente con el formato 1: 11 líneas fijas + 10 eventos = **21**.
- **22 líneas físicas frente a 21 líneas verificadas: no es una discrepancia.** El script de captura cuenta 22
  porque incluye la línea `DIAG END`. El verificador cuenta 21, porque el CRC cubre desde `DIAG BEGIN` hasta la línea
  anterior a `DIAG END`, que es lo que declara `lines=`.
- Del segundo informe solo se registra el resultado del verificador; **su CRC32 no se reproduce aquí**.

## 5. Observación: `OtherWatchdog` reiterado (O1)

| Fecha | Arranque | Contexto |
|---|---|---|
| 2026-10-24 | `boot reason=OtherWatchdog boots_since_power_on=1 watchdog_resets=1 panic_resets=0 brownout_resets=0` | Primer arranque tras el flasheo. Tras un ciclo controlado de USB: `PowerOn` y 0 watchdogs (`h4-validacion-diagnostico-2026-10-24.md` §4.1) |
| **2026-10-25** | `boot reason=OtherWatchdog boots_since_power_on=1 watchdog_resets=1 panic_resets=0 brownout_resets=0` | Sesión **sin flasheo**. El firmware funcionó más de 20 minutos y completó todas las pruebas |

- **Estado: pendiente y reiterada en una sesión posterior.** No hay evidencia de reinicios adicionales durante las
  pruebas ni durante los benchmarks: el registro muestra un solo `Boot` (§4.6).
- **Causa no determinada.** No se atribuye al hardware, al firmware, a ESP-IDF ni al reinicio por USB.
- **Lectura del registro [V]:** `boots_since_power_on=1` junto a `watchdog_resets=1` indica que en ese arranque la
  memoria RTC no traía un registro válido, como ocurre tras una pérdida de alimentación, y aun así el motivo
  informado fue un watchdog. El historial funcionó según su diseño (`h4-diagnostico-autonomo.md` §6).
- **Hipótesis de trabajo [I], no confirmada:**
  - La errata **RES-3.8** del ESP32 (*ESP32 Series SoC Errata*) afecta a las revisiones v0.0, v1.0 y v1.1. Describe
    que, si el ESP32 lee la flash antes de que esté lista tras el encendido, el arranque puede fallar hasta que se
    produce un reinicio por watchdog, cuando la flash se alimenta de VDD_SDIO.
  - El patrón sería compatible con O1 y con el `flash read err, 1000` transitorio del primer encendido en H3c
    (`h3c-validacion-fisica-2026-10-24.md` §5, observación 1).
  - Hay que confirmarlo o descartarlo con evidencia.
- **Siguiente investigación:** capturar desde el encendido el registro de arranque de la ROM y del bootloader,
  sobre todo la línea **`rst:`** y cualquier `flash read err`, en varios arranques en frío y tras reconectar el USB, y
  compararlo con `diag status`. Es una captura pasiva de la consola, sin cambios de firmware.

## 6. Matriz de la regresión

| Prueba | Referencia H3c | Estado H4 (2026-10-25) | Evidencia |
|---|---|---|---|
| Geometrías ATR (5 imágenes) | H3c §4.2 | Aprobada | §4.1 |
| **Meta A en el equipo** (4 640 CRC32) | H3c §4.3 | **Aprobada** | §4.2 |
| Unidad D1: lecturas, límite 721 y recuperación | H3c §4.4 | Aprobada | §4.3 |
| Benchmark secuencial y aleatorio | H3c §4.5 | Aprobado, sin degradación sistemática observada (una ejecución) | §4.4 |
| Heap durante los benchmarks | H3c §4.5 | Parcial: sin variación en cada ejecución | §4.4 |
| Registro en `diag storage` y `diag events` | — | Aprobado | §4.5, §4.6 |
| `diag report` final verificado en el PC | — | Aprobado (21 líneas) | §4.7 |
| `OtherWatchdog` (O1) | — | Pendiente, reiterado | §5 |
| Estabilidad prolongada | — | Pendiente | — |
| MOTOR CONTROL / GPIO36 (M3a), puerta eléctrica M0–M6 | — | Pendiente | `h4-puerta-sio.md` |
| Recepción y transmisión SIO; pruebas con el Atari | — | Pendiente | H5–H7 |

## 7. Conclusiones

1. El firmware H4 `15b153f` **conserva las funciones de almacenamiento de H3c**:
   - las cinco geometrías;
   - la Meta A completa (4 640/4 640 CRC32);
   - la unidad D1 con su límite y su recuperación;
   - un rendimiento de medias y máximos alrededor del 1 % de H3c.
2. `rb_hw_diag` registra correctamente todas esas operaciones, y el informe final verifica con la herramienta de PC.
3. El `OtherWatchdog` de arranque se repitió en una sesión sin flasheo. Su causa sigue indeterminada y se investigará
   con la captura del registro de la ROM.
4. **H4 eléctrico continúa EN PREPARACIÓN.** Esta regresión no aprueba la puerta de hardware SIO.

## 8. Trazabilidad y privacidad

- **Firmware:** `15b153f267cf6766978bd786eb1b463ea0e4ac50`, paquete `retrobridge-fw-diag-fujinet-atari-v1x-15b153f`
  (ejecución `40133891881`). Instalado el 2026-10-24; esta campaña no volvió a flashear.
- **Referencia:** `tools/make_atr.py`, con su `manifest.csv`, y los resultados de H3c del 2026-10-24.
- **Lo que no se versiona:** registros completos de consola, volcados D5/D6, direcciones MAC, credenciales ni rutas
  personales. Este informe resume los resultados suministrados por el equipo.
