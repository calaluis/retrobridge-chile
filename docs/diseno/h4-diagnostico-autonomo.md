# H4: diagnóstico autónomo del firmware (`rb_hw_diag`)

> Complemento de software de H4. **No aprueba H4** ni sustituye ninguna medición eléctrica
> (`docs/hardware/h4-puerta-sio.md`, `docs/hardware/procedimiento-medicion-pasiva-sio.md`).
>
> El módulo observa **solo el estado interno** del firmware: no configura GPIO del SIO, no activa ningún ADC y no
> mide tensiones. Todo lo que ofrece se puede verificar con el ESP32 por USB y **sin el Atari**.

## 1. Objetivo

Ver qué hace el firmware y registrar sus fallos sin depender de un osciloscopio para las pruebas de software.

## 2. Arquitectura (ADR-002, ADR-011, ADR-014)

```
main (raíz de composición)
 ├── EspSystemProbe (rb_esp_platform)      adaptador: esp_reset_reason, RTC no-init, heap, sdkconfig, app desc
 │      implementa ▼
 ├── rb_hw_diag (C++17 puro, solo rb_core) puerto SystemProbe · DiagnosticsRecorder · EventLog · ReportWriter
 └── rb_console                            comando "diag" + registro de lo que hacen sd, atr y drive
```

- **`rb_hw_diag` es independiente:**
  - no conoce el dominio Atari ni ESP-IDF;
  - `tools/check_layering.py` lo trata como capa pura y solo le permite `rb/core/` y `rb/hwdiag/`; una prueba
    comprueba que una inclusión de ESP-IDF o del dominio se detecta.
- **Puerto `SystemProbe`:** devuelve un `SystemSnapshot` con tiempo desde el arranque, firmware, historial de
  reinicios, memoria y configuración del watchdog. El adaptador ESP32 es `EspSystemProbe`; las pruebas de host usan
  valores fijos.
- **`DiagnosticsRecorder`:** guarda los contadores de almacenamiento, las últimas latencias y un registro acotado
  de eventos.
  - Memoria fija y operaciones O(1), **sin heap**.
  - Lo alimentan `main` (arranque y montaje inicial) y la consola (`sd`, `atr crc`, `atr bench`, `drive read`).
  - Se usa desde una sola tarea, el contexto de almacenamiento (ADR-014). No es seguro para uso concurrente ni
    desde interrupciones.
- **`ReportWriter`:**
  - escribe líneas `DIAG <sección> clave=valor` con claves estables (formato 1);
  - arma cada línea en un búfer fijo de 256 bytes: los valores largos se recortan, nunca desbordan;
  - envía las líneas a un puerto `LineSink` (en el equipo, la consola por UART0).
- **Reutilización:**
  - las latencias vienen de `LatencyStats`/`BenchmarkReads`;
  - las verificaciones, de `VerifySectors`;
  - la memoria, de `capture_memory()` (el mismo dato que `mem`);
  - los nombres de error, de la tabla de la consola;
  - el tiempo, del puerto `Clock`.

## 3. Qué monitorea y cómo verificarlo sin el Atari

| Ámbito | Datos | Origen | Verificación con el ESP32 aislado |
|---|---|---|---|
| Firmware | Proyecto, versión, ESP-IDF, perfil de placa | `esp_app_get_description()`, perfil | `diag status` tras flashear un artefacto verificado |
| Reinicios | Motivo del último (encendido, software, pánico, watchdogs, brown-out…), arranques desde el encendido y conteo de reinicios por watchdog, pánico y brown-out | `esp_reset_reason()` + registro en **RTC no-init** con firma y valor de control | Encender (`PowerOn`, arranque 1); reiniciar con el botón o con el auto-reset del monitor y ver cómo cambia el motivo y aumenta el contador |
| Watchdog | Task WDT (activo, tiempo y pánico) e interrupt WDT (activo y tiempo) compilados | `sdkconfig` (`CONFIG_ESP_TASK_WDT_*`, `CONFIG_ESP_INT_WDT*`) | `diag status`, contra el `sdkconfig` del paquete |
| Memoria | Libre, mínima histórica, bloque mayor e interna | `heap_caps_*` | `diag memory` antes y después de `atr bench` (H3-13) |
| microSD | Montada, intentos, fallos, último `esp_err` y último error de RetroBridge. Con el perfil `RB_BOARD_UNCONFIRMED`, el montaje rechazado sin tocar hardware cuenta como fallo, con `last_error=BoardNotConfirmed` | `SdCardStorage` y consola | Montar y desmontar; sacar la tarjeta y volver a montar para provocar un fallo controlado |
| Errores de lectura | Sectores verificados, errores en `atr crc`, errores de `drive read` (por ejemplo, `SectorOutOfRange`) | Consola | `drive read 721` sobre `dd-720.atr` (error esperado, H3c) |
| Latencias | Último resumen de `atr bench` y conteo de ejecuciones sobre el presupuesto de 5 000 µs | `LatencyStats` | `atr bench` y `diag storage` |
| Eventos | Los últimos 32, con secuencia, tiempo, tipo y valor; los perdidos se cuentan | `EventLog` | `diag events` |

## 4. Comandos (consola por UART0, 115 200 baudios)

| Comando | Salida |
|---|---|
| `diag status` | `DIAG firmware`, `uptime`, `boot`, `watchdog`, `summary` |
| `diag memory` | `DIAG memory` |
| `diag storage` | `DIAG storage`, `reads`, `latency` |
| `diag events` | `DIAG events` y una línea `DIAG event` por registro |
| `diag report` | Todo lo anterior entre `DIAG BEGIN format=1 …` y `DIAG END lines=<n> crc32=<hex>` |

Todos los comandos son de **lectura**: no escriben en la microSD, en la flash ni en NVS, y no tocan pines.

### 4.1 Informe reproducible y verificable

- El CRC-32 (el mismo que `zlib.crc32`) cubre todas las líneas desde `BEGIN` hasta la anterior a `END`, cada una
  terminada en `\n`.
- `tools/check_diag_report.py captura.txt`:
  - acepta una captura cruda de la consola e ignora el prompt y los registros;
  - comprueba el número de líneas, el CRC y el formato;
  - imprime el último informe completo.
- "Reproducible" significa con **formato estable y comprobable**: dos informes no son idénticos byte a byte, porque
  incluyen el tiempo desde el arranque y las marcas de los eventos.
- **En CI:** la prueba de host `test_report_writer` genera un informe de ejemplo y `check_diag_report_sample` lo
  verifica con la herramienta de PC. Si el firmware y la herramienta divergen, el CI falla.

## 5. Memoria

| Elemento | Tamaño aproximado | Dónde |
|---|---|---|
| `EventLog<32>` | 32 × 24 B ≈ 0,8 KB | Estático (dentro de `DiagnosticsRecorder`) |
| `StorageHealth` y contadores | < 100 B | Estático |
| `ReportWriter` (búfer de línea) | unos 300 B | Pila de la tarea de consola (6 KB), solo durante `diag` |
| Registro de reinicios en RTC | 24 B | RTC no-init |

No hay asignaciones dinámicas en el módulo.

## 6. Limitaciones

1. **El firmware no puede detectar ni prevenir por sí solo una sobretensión antes de que llegue al pin físico.**
   Cuando el ESP32 podría "ver" una tensión excesiva, el daño ya puede haber ocurrido. La protección es eléctrica:
   medición previa, adaptación de niveles y aislamiento.
2. **Ninguna lectura interna del ESP32 sustituye la medición independiente de MOTOR CONTROL.** La etapa M3a
   (Atari solo, FujiNet desconectada, osciloscopio) sigue siendo obligatoria y es un punto de detención.
3. **Sin ADC:** el módulo no lee SIO_5V (GPIO35) ni ningún otro canal ADC. Activar el ADC puede provocar pulsos de
   unos 80 ns en GPIO36/39 (errata GPIO-3.11) y requiere una revisión separada.
4. **No es un instrumento:** no mide VCC, VCC_BUF, niveles de bus ni tiempos SIO. Las latencias son de
   almacenamiento.
5. **Historial de reinicios aproximado:**
   - el registro en RTC no-init se reinicia en cada encendido, o si la memoria RTC llega corrupta (firma o valor de
     control incorrectos);
   - un reinicio que no conserva esa memoria se cuenta como un arranque nuevo.
6. **El watchdog se informa, no se fuerza:** no hay ningún comando que provoque un reinicio o un pánico.
7. **Concurrencia:** el registrador es de una sola tarea. Un uso desde varias tareas o desde interrupciones requiere
   rediseñarlo (§7).

## 7. Integración futura de un monitor SIO de solo recepción (investigación, sin implementar)

Se conectaría **solo después de aprobar H4 y H5** (`docs/diseno/h5-escucha-solo-recepcion.md`).

| Aspecto | Propuesta |
|---|---|
| Qué registraría | Tramas recibidas, checksum incorrecto, error de trama u overrun del UART, pulsos cortos de COMMAND descartados (errata GPIO-3.11), COMMAND bajo permanente (Atari apagado, riesgo R12), velocidad estimada |
| Dónde se cuenta | En la tarea SIO (H5), con **contadores propios** (`SioRxStats`), nunca dentro de la ISR. La ISR solo marca el tiempo y notifica (arquitectura §9.1) |
| Cómo llega a `diag` | La consola lee una **copia** de esos contadores (instantánea atómica o tomada con una sección crítica corta) y `ReportWriter` agrega una sección `DIAG sio …`. `DiagnosticsRecorder` sigue siendo de una sola tarea |
| Eventos nuevos | `SioFrame`, `SioChecksumError`, `SioFramingError`, `SioCommandGlitch` y `SioCommandStuckLow`, en un registro de la tarea SIO con la misma plantilla `EventLog<N>` |
| Puertos | El monitor depende de `SioTransport`/`Clock`, como define el diseño de H5. `rb_hw_diag` no depende de `rb_sio`: se agregaría un tipo neutro (contadores) en `rb_hw_diag` que el monitor rellena |
| Restricciones | Sin ADC durante la escucha; GPIO21 nunca se configura; el monitor no decide nada eléctrico. Sus datos son lógicos (bytes y tiempos) y **no** validan niveles ni tolerancias |

## 8. Pruebas

| Nivel | Qué cubre |
|---|---|
| Host (CTest + Unity) | `test_event_log` (anillo, pérdidas, orden), `test_diagnostics_recorder` (contadores, presupuesto, eventos), `test_report_writer` (líneas estables, CRC incremental, encuadre, recorte, números máximos) |
| Host (Python) | `tools/tests/test_diag_report.py` (CRC, líneas faltantes, capturas incompletas, ruido de consola, formato desconocido); prueba de capas de `rb_hw_diag` |
| Cruzada | `check_diag_report_sample`: la herramienta de PC verifica el informe generado por el C++ |
| CI ESP-IDF | El job `esp-idf-build` compila los dos perfiles con el módulo. El job `firmware-package` sigue empaquetando el commit fijado `1b1527d` y no se ve afectado |
| Paquete H4 (CI) | Job `firmware-package-h4`: artefacto `retrobridge-fw-diag-fujinet-atari-v1x-15b153f`, variante `diag` (marcas del diagnóstico en la aplicación y en el ELF, versión = commit) y modo `verify` en CI (`docs/toolchain.md`) |
| Regresión en el equipo (2026-10-25) | Funciones de H3c con el firmware H4: cinco geometrías, 4 640/4 640 CRC32, unidad D1 con su límite y benchmarks registrados en `diag`; informe final de 21 líneas verificado; `OtherWatchdog` de arranque reiterado y pendiente (`docs/mediciones/h4-regresion-h3c-2026-10-25.md`) |
| En el equipo (2026-10-24) | Hecho por USB, sin el Atari: `diag status/memory/storage/events/report` y el informe verificado con `tools/check_diag_report.py`; lectura ATR y benchmark sin errores; ciclo de alimentación con `PowerOn`. Pendientes: la causa del `OtherWatchdog` inicial y la regresión completa de H3c (`docs/mediciones/h4-validacion-diagnostico-2026-10-24.md`) |
