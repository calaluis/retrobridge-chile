# H3c: primera validación física en la FujiNet FN32ROV-1.3-Q24 (2026-10-24)

> Estado: **Meta A cumplida en el equipo.** Validación inicial del almacenamiento: arranque, microSD, geometrías ATR,
> lectura de sectores, CRC32 y rendimiento. **No** se validó el bus SIO ni el uso con un Atari.
>
> Firmware: commit `1b1527d2dfd29a5f66471cbb2a14a3d49585dd8e`, compilado por GitHub Actions (ejecución
> `40114258572`). El firmware no se modificó para estas pruebas.

**Convención:**
- **[H]** hecho comprobado en el equipo, según la campaña del 2026-10-24;
- **[V]** verificado además de forma independiente en el PC;
- **[I]** hipótesis;
- **[P]** pendiente.

## 1. Objetivos

Corresponde a la integración I-2 y al criterio H3c de la arquitectura (§9.3 y plan de hitos):

1. Arrancar el firmware independiente RetroBridge en el ESP32 de la placa real, sin pines SIO.
2. Montar la microSD por SPI y FAT32 con el perfil `fujinet-atari-v1.3/1.5`.
3. Cumplir la **Meta A en el equipo**: el CRC32 de todos los sectores de las cinco imágenes de `tools/make_atr.py`
   debe coincidir con el calculado en el PC.
4. Ejercitar la unidad virtual D1: montaje, lectura, límites y recuperación tras un error.
5. Medir la latencia de lectura de sectores (H3-12) y observar el heap (H3-13).

## 2. Entorno

| Elemento | Valor |
|---|---|
| Placa | FujiNet Atari **FN32ROV-1.3-Q24**, módulo ESP32-WROVER-B (`docs/hardware/fujinet-atari-v1x.md`) |
| Procesador | ESP32-D0WD, revisión 1.0 |
| Flash | 16 MB, GigaDevice; DIO a 40 MHz |
| Firmware | `1b1527d2dfd29a5f66471cbb2a14a3d49585dd8e`; el ESP32 reporta la versión `1b1527d` |
| Perfil de placa | `RB_BOARD_FUJINET_ATARI_V1X` (`fujinet-atari-v1.3/1.5`): microSD por SPI3 en GPIO 18/23/19/5, sin pines SIO |
| ESP-IDF | v6.1 |
| Origen del binario | Artefacto del job `firmware-package` (ejecución `40114258572`); se verificaron sus 17 archivos con SHA-256 (`SHA256SUMS`) |
| Herramienta de flasheo | esptool.py v4.5.1 |
| Consola | UART0, COM3, 115 200 baudios |
| microSD | FAT32, unos 16 GB, dedicada a pruebas |
| Imágenes de prueba | Las cinco de `tools/make_atr.py` y su `manifest.csv` |
| Respaldo previo | `D5.bin` = `D6.bin` (16 MiB, firmware FujiNet WSS instalado). Se guardan de forma privada y **no se versionan** |
| Atari / SIO | **Desconectados.** El firmware no configura UART ni GPIO del SIO |

## 3. Procedimiento reproducible

Las precondiciones están en `docs/hardware/procedimiento-instalacion-recuperacion.md`: respaldo verificado,
autorización explícita, solo USB y Atari desconectado.

1. **Paquete:** descargar el artefacto de la ejecución, ejecutar `sha256sum -c SHA256SUMS` y, opcionalmente,
   `python -I tools/ci/firmware_package.py verify <paquete>` (`docs/toolchain.md`).
2. **Imágenes:** generar las cinco imágenes con `python tools/make_atr.py --out <dir>` y copiar los `.atr` a la
   microSD de pruebas. `manifest.csv` queda en el PC como referencia.
3. **Escritura** (con autorización): bootloader en `0x1000`, tabla en `0x8000` y aplicación en `0x10000`, según
   `flash/flash_args.paquete`, con esptool.py v4.5.1. Esta es la forma de referencia; para reproducirla, usar la
   documentación de esptool:
   ```bash
   esptool.py --chip esp32 -p COM3 -b 460800 write_flash --flash_mode dio --flash_freq 40m --flash_size 16MB \
       0x1000 firmware/bootloader.bin 0x8000 firmware/partition-table.bin 0x10000 firmware/retrobridge.bin
   ```
4. **Consola** a 115 200 baudios en UART0 y comandos de diagnóstico (§3.1).
5. **Comparación de CRC32:** guardar la salida de `atr crc` de las cinco imágenes y ejecutar
   `python tools/compare_crc.py manifest.csv <registro>`.

### 3.1 Comandos de diagnóstico

Ninguno de estos comandos escribe en las imágenes ni en la microSD. `sd mount` monta FAT con
`format_if_mount_failed = false`; ESP-IDF v6.1 no ofrece montaje de solo lectura para SD (ADR-015, precisión H3b).

| Comando | Qué hace |
|---|---|
| `board` | Perfil de placa y estado de validación del hardware |
| `crctest` | Vector de referencia del CRC-32 de la ROM (`123456789` → `cbf43926`) |
| `sd mount` / `sd status` / `sd ls [dir]` / `sd unmount` | Montaje, estado (imágenes abiertas), listado y desmontaje de la microSD |
| `atr info <ruta>` | Geometría de la imagen |
| `atr crc <ruta>` | Una línea `CRC <sector> <crc32>` por sector, el formato que lee `compare_crc.py` |
| `atr bench <ruta> [seq\|rand] [count]` | Latencia de lectura: media, mínima, mediana, P95, P99, máxima, muestras y descartadas, frente a un presupuesto de 5 000 µs |
| `drive mount <ruta>` / `drive read <n>` / `drive status` / `drive unmount` | Unidad virtual D1: casos de uso del contexto de unidad ejecutados en secuencia (H3-7) |
| `mem` | Cifras del heap |

## 4. Resultados

### 4.1 Arranque y plataforma [H]

- Se escribieron el bootloader, la tabla de particiones y la aplicación, con tres `Hash of data verified`.
- Arrancó el bootloader de ESP-IDF 6.1 y la aplicación RetroBridge mostró el prompt `retrobridge>`.
- Se reconoció el perfil `fujinet-atari-v1.3/1.5`.
- La microSD se montó correctamente y se confirmó la navegación FAT32 y la lectura de archivos y directorios.

### 4.2 Geometrías ATR [H]

| Imagen | Sectores | Interpretación de `atr info` |
|---|---:|---|
| `sd-720.atr` | 720 | SD, 128 bytes |
| `ed-1040.atr` | 1040 | ED, 128 bytes |
| `dd-720.atr` | 720 | DD, 256 bytes nominales |
| `dd-1440.atr` | 1440 | No estándar, 256 bytes nominales |
| `sd-trailing.atr` | 720 | SD; se detectan 50 bytes sobrantes |

En las imágenes de 256 bytes, los sectores 1–3 tienen 128 bytes. Las cinco interpretaciones coinciden con el
diseño de las imágenes (`tools/make_atr.py`: `images.csv` marca `header_mismatch=1` y `trailing_bytes=50` solo
para `sd-trailing`).

### 4.3 Meta A: CRC32 [H][V]

| Comprobación | Resultado |
|---|---|
| `crctest` | `cbf43926`, correcto (vector estándar CRC-32 de `123456789`) |
| `compare_crc.py` sobre las cinco imágenes contra `manifest.csv` | **4 640 / 4 640 coincidentes** |
| Sectores faltantes / diferencias / errores | 0 / 0 / 0 |

[V] El total de 4 640 corresponde a 720 + 1040 + 720 + 1440 + 720 sectores, igual que el `manifest.csv` que genera
`make_atr.py`.

### 4.4 Unidad virtual D1 con `dd-720.atr` [H][V]

| Operación | Resultado |
|---|---|
| `drive mount` | Correcto |
| `drive read 1` | 128 bytes, CRC32 `1127658a`; coincide con `manifest.csv` [V] |
| `drive read 3` | 128 bytes |
| `drive read 4` | 256 bytes, CRC32 `ef1a5c7d`; coincide con `manifest.csv` [V] |
| `drive read 720` | 256 bytes |
| `drive read 721` | **Rechazado** con `SectorOutOfRange` (fuera de la geometría) |
| `drive read 720` después del error | Correcto; el estado vuelve a `last operation ok` |
| `drive unmount` y `sd status` | Correcto; cero imágenes abiertas |

Esto confirma en el equipo la validación de parámetros de `VirtualDrive::validateRead`, la misma que usará la
fase 1 del ADR-005 para responder NAK, y la recuperación de la unidad tras un error.

### 4.5 Rendimiento de lectura (`atr bench`, `dd-1440.atr`) [H]

| Indicador | Secuencial, 100 | Aleatorio, 100 | Aleatorio, 1000 |
|---|---:|---:|---:|
| Lecturas | 100 | 100 | 1000 |
| Errores | 0 | 0 | 0 |
| Latencia media (µs) | 595 | 1807 | 1845 |
| Mínima (µs) | 66 | 1079 | 1062 |
| Mediana (µs) | 216 | 2043 | 2048 |
| P95 (µs) | 1112 | 3001 | 3024 |
| P99 (µs) | 1663 | 3141 | 3110 |
| Máxima (µs) | 1928 | 3903 | 3953 |
| Muestras descartadas | 0 | 0 | 0 |

- **Presupuesto:** la máxima observada (3 953 µs) queda por debajo de los 5 000 µs del benchmark diagnóstico
  (`kSectorReadBudgetUs`, arquitectura §9.3). Es un **máximo observado** en 1 200 lecturas, no un peor caso
  garantizado.
- **Heap:** en la ejecución aleatoria de 1 000 lecturas, `mem` informó la misma memoria libre antes y después
  (280 880 bytes). Esa ejecución no tuvo disminución neta, pero **esto no descarta fugas de memoria**. La Meta B
  (H7) exige 10 000 lecturas sin variación del heap.
- **Alcance:** estas latencias son de **almacenamiento** (microSD + FAT + adaptador). **No validan los tiempos
  eléctricos del protocolo SIO** (t0–t5), que se medirán en H4–H7 (`docs/mediciones/sio-tiempos.md`, arquitectura
  §9.3).

## 5. Observaciones pendientes

Se registran sin corregir; ninguna se da por resuelta.

| # | Observación [H] | Análisis | Clasificación y acción propuesta |
|---|---|---|---|
| 1 | En el primer encendido apareció de forma transitoria `flash read err, 1000`, seguido de un arranque correcto. | `0x1000` es la dirección del bootloader. **Caracterizado el 2026-10-25:** el fallo ocurre en la ROM, antes del bootloader y de RetroBridge; se repitió en 9 de 9 encendidos con S5 (T1) y ya ocurría con el firmware FujiNet anterior tras cada reset por RTS. Causa raíz sin confirmar; RES-3.8 es una hipótesis compatible (`arranque-rom-conocido-2026-10-25.md`). | **Comportamiento conocido de esta unidad** (`arranque-rom-conocido-2026-10-25.md` §8). No bloquea. |
| 2 | El perfil de almacenamiento informa `warnings 0x01`. | En el código es `PinWarning::kStrappingPin` (`components/rb_board/include/rb/board/board_profile.hpp`): el perfil usa un pin de arranque, **GPIO5** (CS de la microSD), como ya advertía `docs/hardware/fujinet-atari-v1x.md` §3. | **Aviso esperado, no un error.** Se mantiene; se relaciona con el seguimiento del nivel de GPIO5 al arrancar (§3 del informe de hardware). |
| 3 | La consola imprime a veces dos veces `retrobridge>`. | Causa no confirmada. [I] Podría venir de la interacción entre el REPL de `esp_console` y los finales de línea del terminal. | **[P]** Incidencia: reproducir con distintos ajustes de fin de línea del terminal. |
| 4 | Al salir de Miniterm, las tres luces de la FujiNet quedaron encendidas. | RetroBridge no configura los GPIO de los LED (2, 4 y 13; informe de hardware §5). [I] Su estado depende de los valores al arrancar o de las líneas DTR/RTS al cerrar el puerto. | **[P]** Caracterizar más adelante; no afecta al almacenamiento. Cualquier control de LED requiere un perfil y una decisión aparte. |
| 5 | La revisión 1.3 no tiene los pull-ups de 10 k de la microSD que agregó la 1.5. | Informe de hardware §3. En esta campaña no hubo errores: 0 en 4 640 CRC32 y en 1 200 lecturas. | **[P]** Estabilidad prolongada con al menos dos tarjetas antes de considerar pull-ups internos, que requieren decisión y aprobación aparte. |

## 6. Alcance de la validación

**Validado inicialmente [H]:**
- Arranque de un firmware independiente en un ESP32 físico.
- Acceso a la microSD por SPI y FAT32.
- Interpretación de geometrías ATR, incluidas la no estándar y la de bytes sobrantes.
- Lectura de sectores, con 128 bytes en los sectores 1–3 de las imágenes DD.
- Manejo de límites (`SectorOutOfRange`) y recuperación tras un error.
- Integridad CRC32 contra las referencias del PC (Meta A).
- Rendimiento inicial del almacenamiento.

**No validado todavía [P]:**
- Comunicación SIO física con un Atari XL/XE (H4–H7).
- Carga de juegos desde un Atari real mediante RetroBridge (Meta B, H7).
- Escritura de sectores ATR: la v1 es de solo lectura (arquitectura §0.1).
- Estabilidad prolongada.
- Funcionalidad equivalente a FujiNet: fuera del alcance de la v1 (arquitectura §0.1).

## 7. Matriz de pruebas

| Prueba o criterio | Origen | Estado | Evidencia |
|---|---|---|---|
| Flasheo con aprobación explícita y respaldo previo | H3 (plan de hitos) | Aprobada | D5 = D6 verificados; tres `Hash of data verified` |
| Arranque del firmware y consola | I-2 | Aprobada | Prompt `retrobridge>` con el perfil `fujinet-atari-v1.3/1.5` |
| Montaje de la microSD por SPI y FAT32 | H3c | Aprobada | `sd mount` y `sd ls` |
| Geometrías ATR (5 imágenes) | H3a/H3c | Aprobada | §4.2 |
| **Meta A en el equipo** | H3, I-2 | **Aprobada** | 4 640 / 4 640 CRC32 (§4.3) |
| Lectura por la unidad D1, límites y recuperación | H2/H3 (`VirtualDrive`, ADR-005) | Aprobada | §4.4 |
| Lectura de sector ≤ 5 ms | §9.3, H3-12 | Aprobada como máximo observado | 3 953 µs en 1 200 lecturas; falta caracterizar el peor caso |
| Lectura sin cambios en el heap | H3-13 | Parcial | Una ejecución de 1 000 lecturas sin cambio neto |
| `flash read err, 1000` en el primer encendido | Observación 1 | Comportamiento conocido; causa raíz sin confirmar | §5; `arranque-rom-conocido-2026-10-25.md` |
| Doble prompt en la consola | Observación 3 | Pendiente | §5 |
| Estado de los LED al cerrar Miniterm | Observación 4 | Pendiente | §5 |
| Estabilidad de la microSD en la revisión 1.3 | Observación 5 | Pendiente | §5 |
| Tiempos eléctricos SIO (t0–t5) | §9.3, H4–H7 | Pendiente | — |
| Meta B: arranque de un ATR desde un Atari real | H7 | Pendiente | — |

## 8. Conclusiones

1. El firmware independiente RetroBridge (`1b1527d`) arranca y funciona en la FujiNet FN32ROV-1.3-Q24 con el perfil
   de microSD preparado en H3c, sin tocar los pines del SIO.
2. La **Meta A se cumple en el equipo**: los 4 640 sectores de las cinco imágenes leídos por el ESP32 coinciden con
   el CRC32 calculado en el PC.
3. La lectura de sectores respeta la geometría ATR, rechaza los sectores fuera de rango y se recupera.
4. La latencia de almacenamiento observada (máximo de 3 953 µs) queda dentro del presupuesto de 5 ms. No es un peor
   caso garantizado ni una medición del protocolo SIO.
5. Quedan cuatro observaciones abiertas (§5). Ninguna impidió las pruebas, pero deben caracterizarse antes de H4.
   La observación 1 se caracterizó el 2026-10-25 como comportamiento conocido de esta unidad (`arranque-rom-conocido-2026-10-25.md`).

## 9. Trazabilidad y privacidad

- **Firmware:** `1b1527d2dfd29a5f66471cbb2a14a3d49585dd8e`.
- **Paquete:** job `firmware-package`, ejecución `40114258572` de GitHub Actions; 17 archivos verificados con
  SHA-256.
- **Lo que no se versiona:** volcados de flash (D5/D6), registros completos de consola, direcciones MAC ni
  configuraciones privadas. Este informe resume los resultados suministrados por el equipo y no reproduce
  capturas.
