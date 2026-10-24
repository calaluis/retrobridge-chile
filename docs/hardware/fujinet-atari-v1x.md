# Identificación de hardware: FujiNet Atari ESP32 (H3c)

> Estado: **revisión confirmada: FN32ROV-1.3-Q24**, con módulo **ESP32-WROVER-B** y lector microSD integrado.
> Evidencia: fotografías de ambas caras de la placa usada en el proyecto WSS, 2026-10-23. Las fotografías son
> privadas y no se versionan.
>
> El perfil `RB_BOARD_FUJINET_ATARI_V1X` corresponde a esta placa. `RB_BOARD_UNCONFIRMED` sigue siendo el perfil
> predeterminado del firmware.
>
> Trabajo solo documental: no se abrió el puerto serie, no se ejecutó esptool y no se modificó la flash ni los
> eFuses. **Antes de cualquier instalación siguen siendo obligatorios los respaldos D5/D6 verificados del firmware
> WSS** (`procedimiento-instalacion-recuperacion.md`).

**Etiquetas:**

- **[F]** confirmado por fotografía de la placa física (2026-10-23);
- **[C]** confirmado por registros del propio equipo (pruebas TNFS/WSS, octubre de 2026);
- **[E]** confirmado por el esquema público de la revisión indicada;
- **[D]** confirmado por la hoja de datos del fabricante;
- **[I]** inferido;
- **[P]** pendiente de verificación física o de medición.

**Política de fuentes (ADR-012):** los pines provienen de los **esquemas públicos de hardware** (CERN OHL 2.0) y de
los registros del equipo. **No** se leyó código fuente de FujiNet; en particular, no se leyó la cabecera de pines del
objetivo `PINMAP_ATARIV1`.

## 1. Revisión de la placa

| Dato | Valor | Etiqueta |
|---|---|---|
| Objetivo de firmware usado en WSS | `fujinet-atari-v1` (placa `fujinet-v1`, flash de 16 MB, DIO, 40 MHz) | [C] |
| Detección del firmware FujiNet | "Detected Hardware Version: 1.1-1.5" en el arranque | [C] |
| **Revisión de la PCB** | **FN32ROV-1.3-Q24** | **[F]** |
| Módulo | **Espressif ESP32-WROVER-B** | **[F]** |
| Lector microSD | Integrado en la placa (J4) | [F] / [E] |

**Coherencia:** el registro "1.1-1.5" solo indicaba un grupo de revisiones y por sí solo no permitía identificar la
placa. La fotografía resuelve la duda y es compatible con ese grupo.

**Esquema de referencia:** desde ahora el esquema aplicable es **FN32ROV-1.3-Q24** [E]. Los datos de la 1.5 quedan
solo como comparación.

## 2. Diferencias entre revisiones públicas (wiki oficial y README de `fujinet-hardware`)

**1.0 → 1.3:**

- líneas SIO hacia el ESP32 a través de dos 74LS07;
- transistores P/N que desconectan el ESP32 del Atari cuando FujiNet está apagado;
- CP2102 en encapsulado QFN-24;
- reset por firmware ("safe reset") en lugar de un reset duro;
- se quita el puerto JTAG (quedan puntos de prueba);
- pull-down en MOTOR.

Error conocido de la 1.3: en algunos equipos no se puede flashear, y hay una reparación de hardware publicada
(`CP210x_RESET-BUG_FIX_FINAL.jpg` en la carpeta 1.3 del repositorio). **En esta unidad** el flasheo y la verificación
con esptool funcionaron a 460 800 baudios durante las pruebas WSS (`write_flash` y `verify_flash` OK) [C]. No se sabe
si la reparación está aplicada en esta placa [P], pero hasta ahora no ha hecho falta.

**Lo que la 1.3 de esta placa NO tiene**, porque llegó con la 1.5 (lista siguiente):

- divisor en VBUS y demás cambios de flasheo del CP2102;
- diodos TVS en el USB;
- pull-up de 4,7 k en SIO_DATAIN. Es relevante para H4/H5, no para H3c;
- **pull-ups de 10 k en la microSD** (ver §3).

**1.3 → 1.5** (no se limita a la microSD):

- **Flasheo y USB:**
  - divisor resistivo en VBUS del CP2102;
  - condensadores del CP2102;
  - VIO del CP2102 conectado a VDD;
  - pull-ups de auto-reset de 1 k a 10 k;
  - capacidad de auto-reset/EN de 4,7 µF.
- **Protección:** diodos ESD (TVS) en la entrada USB, y conector USB con huella ranurada.
- **SIO:** pull-up de 4,7 k en SIO_DATAIN, y resistencia de SIO AUDIO_IN a 10 k (en el esquema 1.3, R8 = 470 Ω).
- **microSD:** pull-ups de 10 k (R17–R19) en MISO, MOSI y CS.

**1.5 → 1.6:**

- detección de tarjeta en **GPIO15** (pin de arranque);
- nuevo divisor de SIO 5V;
- MOTOR a través de un buffer (10 k de pull-up del lado del ESP y 2 k de pull-down del lado del Atari);
- cambios mecánicos.

**1.6 → 1.7:** USB-C y un condensador de 220 µF en el SIO 5V.

## 3. microSD

Cableado de J4 (DM3D-SF) en **FN32ROV-1.3-Q24**, la revisión confirmada. Es igual al de la 1.5 salvo los
pull-ups [E][F]:

| Señal | GPIO | Notas |
|---|---|---|
| CLK | 18 | Pin nativo de VSPI |
| CMD (MOSI) | 23 | Pin nativo de VSPI. **Sin pull-up externo** en la 1.3 |
| DAT0 (MISO) | 19 | Pin nativo de VSPI. **Sin pull-up externo** en la 1.3 |
| CD/DAT3 (CS) | 5 | Pin nativo de VSPI. **Pin de arranque**. **Sin pull-up externo** en la 1.3 |
| DAT1, DAT2 | — | Sin conectar → la tarjeta funciona en **modo SPI** |
| Interruptor de tarjeta | — | Sin conectar (la detección en GPIO15 llega con la 1.6) |
| Alimentación | 3,3 V | |

**Pull-ups de la microSD en la 1.3 (importante para H3c):**

- La 1.3 **no tiene** las resistencias de 10 k (R17–R19) en MISO, MOSI y CS que agregó la 1.5 [E].
- La guía de ESP-IDF *SD Pull-up Requirements* indica que en modo SPI las líneas CMD y DAT0–DAT3 deben tener
  pull-ups de 10 kΩ [D].
- Aun así, con esta misma placa el firmware FujiNet montó la tarjeta y leyó imágenes ATR SD (720×128) y DD (720×256)
  de forma estable durante las pruebas WSS [C]. Con qué configuración de pull-ups internos lo logró es un detalle de
  ese firmware que no se consultó.
- **Para H3c [P]:**
  1. Medir la fiabilidad del montaje y de las lecturas con la configuración por defecto del controlador SDSPI
     (errores de `atr crc`, reintentos, latencias de `atr bench`), con al menos dos tarjetas.
  2. Si aparecen fallos, una mitigación posible es habilitar los **pull-ups internos del ESP32** (débiles, del orden
     de decenas de kΩ) **solo en GPIO 5, 18, 19 y 23**, nunca en pines del SIO. Requiere una decisión y aprobación
     aparte, porque implica configurar GPIO de la microSD.
  3. Una alternativa de hardware (agregar resistencias como en la 1.5) queda fuera del alcance del software.
- **GPIO5 al arrancar:** sin pull-up externo, el nivel de arranque de GPIO5 depende de su pull-up interno por
  defecto (alto) y de la carga de la tarjeta. Las pruebas WSS arrancaron con la tarjeta insertada sin problemas [C].
  Se mantiene la advertencia de pin de arranque del validador.

**Host SPI:**

- Los cuatro pines son los nativos de VSPI, es decir, **`SPI3_HOST`** [E].
- El perfil usa `SPI3_HOST`, y el controlador aplica **el mismo valor** al bus (`spi_bus_initialize`), al host
  SDSPI (`host.slot`), al dispositivo (`slot.host_id`) y a la liberación (`spi_bus_free`).

## 4. Alimentación y control (esquema 1.3, aplicable a esta placa) [E]

- **Fuentes de alimentación:**
  - USB_5V y SIO_5V llegan a VCC mediante diodos Schottky PMEG2010;
  - el regulador XC6220B331 (3,3 V) se habilita con el interruptor S5;
  - la microSD y el ESP32 se alimentan de 3,3 V.
- **La placa puede alimentarse del +5 V del Atari**, cuyo pin 10 está especificado para 50 mA como máximo (Anexo A).
  Durante H3c: **solo USB y sin Atari conectado**.
- **74LS07 (U3/U4):** se alimentan de VCC_BUF, conmutado por Q2/Q3, para aislar el bus con la placa apagada.
- **Auto-reset:** el CP2102 controla EN/IO0 con DTR/RTS a través de Q1. En la 1.3 los pull-ups de auto-reset son
  de 1 k y no hay divisor en VBUS. En esta unidad el flasheo funcionó en las pruebas anteriores [C].
- **Protección:** la 1.3 **no tiene** diodos TVS en la entrada USB, porque llegaron con la 1.5 [E]. Conviene
  manipular la placa y el cable con precaución electrostática.
- **Botones:** Safe Reset en GPIO14, ButtonA en GPIO0 y ButtonB en GPIO34.
- **LED:** GPIO2, GPIO4 y GPIO13, activos en bajo.
- **GPIO12 y GPIO15:** solo llegan a puntos de prueba en 1.3/1.5.

## 5. Bus SIO (esquema 1.3, aplicable a esta placa) [E], solo para descartar conflictos

| Señal | GPIO | Dirección en el ESP32 |
|---|---|---|
| COMMAND | 39 | entrada (vía 74LS07) |
| DATA OUT del Atari | 33 | entrada |
| DATA IN hacia el Atari | 21 | salida a un 74LS07 de colector abierto |
| CLOCK OUT | 32 | entrada |
| CLOCK IN | 27 | salida |
| PROCEED | 22 | salida |
| INTERRUPT | 26 | salida |
| MOTOR | 36 | entrada |
| SIO 5V (divisor) | 35 | ADC |
| AUDIO IN | 25 | DAC |

- **Sin conflictos:** los pines de la microSD (5, 18, 19, 23) no coinciden con ningún pin del SIO, de los botones
  ni de los LED. `test_fujinet_profile` lo comprueba.
- **Sin configurar nada:** RetroBridge no configura ninguno de esos GPIO del SIO. Con el ESP32 sin configurarlos, las
  entradas TTL de los 74LS07 se leen en alto y las salidas de colector abierto quedan liberadas [I]. Debe medirse en
  H4 [P].
- **Para H4/H5:** en la 1.3 SIO_DATAIN no tiene el pull-up de 4,7 k que agregó la 1.5. El nivel alto de la línea
  depende del lado del Atari.

## 6. Chip, flash, PSRAM y particiones

| Dato | Valor | Etiqueta |
|---|---|---|
| Chip | ESP32-D0WD rev. v1.0, doble núcleo, 240 MHz, cristal de 40 MHz | [C] |
| Flash | Fabricante 0xC8 (GigaDevice), dispositivo 0x4018, 16 MB | [C]; modelo GD25Q128 [I] |
| Modo y frecuencia de las imágenes que corrieron | DIO, 40 MHz | [C] |
| USB-serie | Silicon Labs CP210x (CP2102N-QFN24 en los esquemas) | [C] / [E] |
| Módulo | ESP32-WROVER-B | [F] |
| Tamaño físico de la PSRAM | **8 MB**: todas las variantes del WROVER-B (N4R8, N8R8, N16R8) llevan 8 MB de PSRAM | [D] (variante exacta: [I] N16R8, por la flash de 16 MB detectada) |
| PSRAM direccionable | Unos 4 MB mapeados directamente por el ESP32 (heap inicial de 4 280 828 B con FujiNet); el resto requiere himem | [C] |
| Particiones de **FujiNet** instaladas | nvs 0x9000/28 KiB · main (factory) 0x10000/10 MiB · update 0xA10000/384 KiB · storage (LittleFS) 0xA70000/5,56 MiB · sin otadata | [C] (análisis del volcado D3) |

RetroBridge **no** reutiliza esa tabla. Tiene la suya en `partitions/retrobridge-16mb.csv`.

## 7. Perfil de microSD de esta placa

**Corresponde a esta placa:** el perfil `RB_BOARD_FUJINET_ATARI_V1X` se escribió para FN32ROV-1.3/1.5. Con la
revisión confirmada como 1.3 [F], los pines, el host SPI3 y la ausencia de detección de tarjeta coinciden con el
esquema 1.3. Los pull-ups de la microSD no forman parte del perfil (§3).

- **Perfil:** `rb::board::fujinet_atari_v1x_profile()`, en `components/rb_board/include/rb/board/fujinet_atari_v1x.hpp`:
  - SPI con sck 18, mosi 23, miso 19 y cs 5;
  - `SpiHost::Spi3`;
  - sin detección de tarjeta;
  - `hasPsram = true`.

  **Sin pines SIO.** El validador lo acepta con la única advertencia de pin de arranque (GPIO5).
- **Selección:** con la opción `RB_BOARD_FUJINET_ATARI_V1X` de `main/Kconfig.projbuild`, **que no es la
  predeterminada**, y el fragmento `sdkconfig.board.fujinet-atari-v1x` (16 MB, DIO, 40 MHz y tabla propia).
- **CI:** compila ambos perfiles, solo para comprobar la compilación, y verifica que el predeterminado siga siendo
  `RB_BOARD_UNCONFIRMED`.

## Fuentes

- **Fotografías de ambas caras de la placa** (2026-10-23, privadas, no versionadas): serigrafía FN32ROV-1.3-Q24,
  módulo ESP32-WROVER-B y lector microSD integrado.
- **Espressif, hoja de datos *ESP32-WROVER-B*:** variantes N4R8, N8R8 y N16R8, todas con 8 MB de PSRAM —
  https://www.espressif.com/sites/default/files/documentation/esp32-wrover-b_datasheet_en.pdf
- **Espressif, ESP-IDF *SD Pull-up Requirements*:** pull-ups de 10 kΩ en CMD y DAT0–DAT3 en modo SPI —
  https://docs.espressif.com/projects/esp-idf/en/v5.1/esp32/api-reference/peripherals/sd_pullup_requirements.html
- **Registros del equipo (locales, no versionados):**
  - esptool `flash_id`, `verify_flash`, `read_flash` y `write_flash`;
  - análisis del volcado D3;
  - capturas UART del arranque G5-1 a G5-6;
  - configuración de la placa en el proyecto WSS (`boards/fujinet-v1.json`, `platformio-fujinet-atari-v1.ini`).
- **Wiki de FujiNet:** [Official Hardware Versions](https://github.com/FujiNetWIFI/fujinet-firmware/wiki/Official-Hardware-Versions).
- **Repositorio [`fujinet-hardware`](https://github.com/FujiNetWIFI/fujinet-hardware):**
  - `ATARI/Old-Versions/FN32ROV-1.3-Q24/FN32ROV-1.3-Q24_schematic.jpg`
  - `ATARI/Old-Versions/FN32ROV-1.5-Q24/FN32ROV-1.5-Q24_schematic.jpg` y `README.md`
  - `ATARI/Old-Versions/FN32ROV-1.6/README.md`
