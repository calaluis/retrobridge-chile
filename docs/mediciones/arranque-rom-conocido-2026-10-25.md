# Comportamiento conocido de arranque: `flash read err, 1000` en la FujiNet FN32ROV-1.3-Q24 (2026-10-25)

> Estado: **registrado como comportamiento conocido de esta unidad.** El fallo ocurre en la ROM del ESP32, antes del
> bootloader y de RetroBridge, y se recupera solo con un reinicio `RTCWDT`. **No hay causa raíz confirmada.**
>
> Consolida T1 (campaña de arranques), T6 (inspección visual) y la revisión documental posterior. Solo USB, sin el
> Atari. No se modificó firmware, flash, eFuses ni hardware.

**Convención:**
- **[H]** hecho comprobado en el equipo (registros de consola o fotografías);
- **[D]** confirmado por documentación oficial del fabricante;
- **[V]** verificado además de forma independiente en el PC;
- **[I]** hipótesis o inferencia;
- **[P]** pendiente.

**Informes relacionados:**
- `h3c-validacion-fisica-2026-10-24.md` §5, observación 1 (primer `flash read err`);
- `h4-validacion-diagnostico-2026-10-24.md` §5, observación O1 (`OtherWatchdog`);
- `h4-regresion-h3c-2026-10-25.md` §5 (O1 reiterado);
- `../hardware/fujinet-atari-v1x.md` (identificación de la placa).

## 1. Resumen

1. **El fallo ocurre en la ROM** [H]: la primera lectura del bootloader en `0x1000` falla
   (`flash read err, 1000`, `ets_main.c 371`), el watchdog RTC de la ROM reinicia el chip (`rst:0x10`) y el
   segundo intento arranca con normalidad. Es anterior al bootloader de ESP-IDF y a RetroBridge.
2. **También ocurrió con el firmware FujiNet anterior** [H], antes de instalar RetroBridge (§3.2).
3. **El `OtherWatchdog` de `rb_hw_diag` es ese reinicio `RTCWDT`** [H]: en los 8 arranques capturados con la
   aplicación, `rst:0x10` fue seguido de `reset reason OtherWatchdog, boot 1 ... (watchdog resets 1, panics 0)`.
   O1 deja de ser un fallo independiente; su causa raíz es la misma que la del fallo de la ROM.
4. **RES-3.8 es una hipótesis compatible, no una causa confirmada** (§4). Además, la errata **no explica por sí sola**
   los fallos tras un reset por EN/RTS.
5. **El arranque en modo de descarga (`boot:0x3`, GPIO0 en bajo)** es una anomalía separada (§5).
6. **VDD_SDIO a 1,8 V por GPIO12: descartado** [H][D]. La configuración por eFuse **no se leyó** [P].
7. **Revisión física confirmada: FN32ROV-1.3-Q24** [H]. Las reparaciones publicadas de la 1.3 (VIO/VDD, R6/R7,
   C5/C6) **siguen sin confirmar** (§6).

## 2. Firma del fallo [H]

```
rst:0x1 (POWERON_RESET),boot:0x13 (SPI_FAST_FLASH_BOOT)
flash read err, 1000
ets_main.c 371
   (≈ 0,3 s)
rst:0x10 (RTCWDT_RTC_RESET),boot:0x13 (SPI_FAST_FLASH_BOOT)
... bootloader ESP-IDF: DIO, 40 MHz, 16 MB, spi_flash: detected chip: gd ...
retrobridge: reset reason OtherWatchdog, boot 1 since power-on (watchdog resets 1, panics 0)
```

- Nunca hubo más de una línea `rst:0x1` por encendido, ni pánicos, ni brownout.
- Las imágenes del proyecto no intervienen: la configuración DIO/40 MHz/16 MB (`sdkconfig.board.fujinet-atari-v1x`)
  la aplica el bootloader, que todavía no se ha ejecutado cuando falla la ROM.

## 3. Evidencias

### 3.1 T1: arranques en frío con el interruptor S5 (2026-10-25) [H]

Captura pasiva por UART0 (COM3 abierto con S5 apagado, DTR/RTS sin modificar, nada enviado), firmware `15b153f`.

| Resultado | Detalle |
|---|---|
| Fallo de la ROM | **9 de 9** encendidos con S5 (intento 0 y campaña 1–8), con apagados de 3,6 s a 313 s |
| Intervalo hasta `rst:0x10` | 0,28–0,39 s |
| Aplicación | `OtherWatchdog`, boot 1, wdt 1 en los 8 arranques de la campaña |
| Intento 9 (apagado ≈ 1,1 s) | `boot:0x3`, modo de descarga (§5). Campaña detenida por ese criterio |

El tiempo de apagado depende del tiempo transcurrido desde el byte espurio de la línea serie al cortar S5, corregido
en +0,5 s por el volcado diferido del script de captura. La campaña terminó por un defecto de la herramienta
(`UnicodeEncodeError` al imprimir un byte no UTF-8), sin pérdida de datos.

**Evidencia local, no versionada:** respaldo `T1-2026-10-25` con los registros `t1-intento0.log`, `t1-campana.log`
y sus `.raw`, el informe `T1-resumen-tecnico.md` y la incidencia `INC-T1-01-captura-unicode.md`, verificados con
`MANIFEST-T1.sha256`.

### 3.2 Antecedente con el firmware FujiNet (proyecto WSS, 2026-10-12 a 2026-10-24) [H]

- Informe de identificación de la puerta G1 del proyecto WSS: con el firmware FujiNet instalado, **cada reset por
  RTS** (pulso en EN con +3V3 presente) produjo `rst:0x1`, `flash read err, 1000`, `rst:0x10` y un arranque normal.
- Se repitió en las capturas de arranque posteriores a cada escritura (G2c y G3).
- **Evidencia local, no versionada:** registros `g1-04`, `g1-08`, `g2c-10` y `g3-10` del proyecto WSS. Contienen
  datos privados (MAC y redes Wi-Fi) que no se reproducen aquí.

### 3.3 Arranque tras reconectar el USB [H][I]

- En H4 (`h4-validacion-diagnostico-2026-10-24.md` §4.1), tras desconectar y reconectar el USB, `diag status`
  informó `PowerOn` con 0 watchdogs: ese arranque **no** mostró el fallo.
- Es un solo caso, no se capturó la ROM y no consta la posición de S5. No basta para afirmar que la conexión del USB
  evita el fallo [P].

## 4. RES-3.8 [D][I]

*ESP32 Series SoC Errata*, RES-3.8: afecta a las revisiones v0.0, v1.0 y v1.1. Si el ESP32 lee la flash antes de
que esté lista, el arranque falla hasta un reinicio por watchdog; ocurre al encender y al despertar de deep-sleep
cuando la flash se alimenta de VDD_SDIO. Soluciones: una flash que arranque en menos de 800 µs, o un chip v3.0.
ESP-IDF solo la corrige al despertar de deep-sleep.

| Aspecto | Valoración |
|---|---|
| Revisión del chip | ESP32-D0WD **v1.0** [H]: afectada |
| Flash en VDD_SDIO | Sí en el ESP32-WROVER-B (esquema del módulo) [D] |
| Síntoma | **Coincide** con la firma de §2 |
| Encendido con S5 (T1) | **Cubierto** por la descripción de la errata |
| Reset por EN/RTS (§3.2) | **No mencionado.** Solo sería compatible si VDD_SDIO se corta durante el reset, lo que no consta en la documentación revisada [I] |
| Tiempo de arranque de la flash GigaDevice (`c8`/`4018`) | No consultado [P]; necesario para compararlo con los 800 µs |
| Corrección por firmware | **No existe** para el encendido |

**Conclusión:** hipótesis compatible y aplicable a este chip. **No es una causa confirmada.** Confirmarla requiere
medir los tiempos de alimentación (osciloscopio) o datos de la flash que no se han revisado.

## 5. Anomalía separada: `boot:0x3` con GPIO0 en bajo [H][D]

- Intento 9 de T1, con un apagado de S5 de ≈ 1,1 s: `rst:0x1 (POWERON_RESET),boot:0x3 (DOWNLOAD_BOOT(...))` y
  `waiting for download`.
- Según el *ESP32 Technical Reference Manual* (Register 6.13 `GPIO_STRAP_REG`), los bits 5–0 corresponden a MTDI,
  GPIO0, GPIO2, GPIO4, MTDO y GPIO5. `0x13` y `0x3` solo difieren en **GPIO0**, que estaba en bajo.
- No hubo `flash read err` porque en modo de descarga la ROM no lee la flash.
- Un solo caso. Hipótesis compatibles, sin confirmar [I]: el auto-reset (Q1, DTR/RTS del CP2102 con VIO en estado
  desconocido) o carga residual en IO0/EN/+3V3 en un ciclo corto.

## 6. Estado de la placa (T6, 2026-10-25) [H]

| Elemento | Estado |
|---|---|
| Revisión física | **FN32ROV-1.3-Q24 confirmada** por la serigrafía de la cara inferior |
| U2 | CP2102N QFN24 identificado. Pines 5 = VIO y 6 = VDD según la hoja de datos de Silicon Labs (Rev. 1.5, figura 5.2) [D] |
| Puente VIO–VDD (pines 5–6) | **Sin confirmar**: no concluyente visualmente |
| R6/R7 a 10 k | **Sin confirmar**: marcas ilegibles |
| Condensadores añadidos en C5/C6 | **Sin confirmar**: un cuerpo visible en cada posición; no se descarta uno oculto |
| Material verde y mancha junto a U2 | Presentes en la zona de la reparación publicada; naturaleza desconocida |
| S3 (Reset) | Sin cúpula montada |
| GPIO12 (TDI) | Punto de prueba sin conexiones |

La medición en frío con multímetro (T6b) está propuesta pero suspendida por falta de instrumento.

**Evidencia local, no versionada:** respaldo `T6-2026-10-25` (informe `T6-resultados-visuales-v2.md`, 8 fotografías
y `T6b-propuesta-multimetro.md`), verificado con `MANIFEST-T6.sha256` y `MANIFEST-T6-v2.sha256`.

## 7. Configuración descartada o pendiente

| Elemento | Estado | Evidencia |
|---|---|---|
| Firmware RetroBridge o bootloader como causa | **Descartado** [H] | §3.2 y §2 |
| Bootloader ausente o corrupto en `0x1000` | **Descartado** [H] | El segundo intento lo carga; escrituras con `Hash of data verified` (H4 §3) |
| Arranque en modo HSPI (GPIO2) | **Descartado** [H][D] | `boot:0x13`, GPIO2 = 0 |
| Cifrado de flash o secure boot | **Descartado** [H] | `FLASH_CRYPT_CNT` = 0 y `ABS_DONE_0/1` = 0, leídos con `esptool read_mem` en la puerta G1 del proyecto WSS |
| VDD_SDIO a 1,8 V por GPIO12 | **Descartado** [H][D] | MTDI = 0 en todos los arranques capturados (`boot:0x13` y `boot:0x3`); con MTDI = 0, VDD_SDIO se alimenta de VDD3P3_RTC (TRM §6.8.1; hoja de datos del WROVER-B §3.2) |
| VDD_SDIO por eFuse (`SDIO_FORCE`, `SDIO_TIEH`) | **Pendiente** [P], improbable [I] | `EFUSE_BLK0_RDATA4_REG` (TRM Register 5.5) nunca se leyó. A 1,8 V la flash fallaría de forma permanente, no solo en el primer intento |
| Defecto grave de la flash | **Descartado** [H] | Dos volcados completos de 16 MiB idénticos (D5 = D6) y `verify_flash` correcto |
| Rampa de alimentación, carga residual, tiempos de VDD_SDIO | **No comprobable** sin osciloscopio | — |

## 8. Decisión y criterios de vigilancia

- **Decisión:** se acepta como comportamiento conocido de esta unidad. No bloquea H4 ni las fases siguientes y no se
  implementa ninguna corrección en el firmware.
- **`OtherWatchdog` con `boots_since_power_on=1` y `watchdog_resets=1`** en el primer arranque tras un encendido o
  un reset por EN corresponde a este comportamiento.
- **Se tratará como un problema nuevo** si aparece cualquiera de estos patrones:
  - más de un `rst:0x1` seguido sin llegar al prompt, o más de un `RTCWDT` por encendido;
  - `boot:` distinto de `0x13` sin que se haya pulsado ButtonA;
  - pánicos, brownout o `watchdog_resets` mayor que 1 en un mismo encendido;
  - fallos de lectura de la flash después del arranque.

## 9. Trazabilidad y privacidad

- **Firmware en T1:** `15b153f` (paquete `retrobridge-fw-diag-fujinet-atari-v1x-15b153f`, ejecución `40133891881`).
- **Documentación consultada:** ver `../fuentes.md` (entradas del 2026-10-25).
- **No se versionan:** registros de consola, fotografías, respaldos, direcciones MAC, nombres de redes ni rutas
  personales.
