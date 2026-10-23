# Identificación de hardware: FujiNet Atari ESP32 (H3c, preparación)

> Estado: **revisión exacta PENDIENTE** (leer la serigrafía). `RB_BOARD_UNCONFIRMED` sigue siendo el perfil
> predeterminado. Investigación solo de lectura, 2026-10-23: no se abrió el puerto serie, no se ejecutó esptool y
> no se modificó la flash ni los eFuses.

**Etiquetas:**

- **[C]** confirmado por registros del propio equipo (pruebas TNFS/WSS, octubre de 2026);
- **[E]** confirmado por el esquema público de la revisión indicada;
- **[I]** inferido;
- **[P]** pendiente de verificación física.

**Política de fuentes (ADR-012):** los pines provienen de los **esquemas públicos de hardware** (CERN OHL 2.0) y de
los registros del equipo. **No** se leyó código fuente de FujiNet; en particular, no se leyó la cabecera de pines del
objetivo `PINMAP_ATARIV1`.

## 1. Revisión de la placa

| Dato | Valor | Etiqueta |
|---|---|---|
| Objetivo de firmware usado en WSS | `fujinet-atari-v1` (placa `fujinet-v1`, flash de 16 MB, DIO, 40 MHz) | [C] |
| Detección del firmware FujiNet | "Detected Hardware Version: 1.1-1.5" en el arranque | [C] |
| **Revisión exacta** | **Pendiente.** El registro solo indica el grupo que el firmware asigna. **No prueba** que sea 1.3 ni 1.5. | [P] |

**Revisiones que el registro todavía permite:** todas las que el firmware clasifica como "1.1-1.5". Se desconoce
el criterio de detección, porque no se leyó el código.

| Revisión | Situación |
|---|---|
| 1.1, 1.2, 1.4 | Sin esquema público; podrían existir como prototipos o tiradas no publicadas |
| **1.3-Q24** y **1.5-Q24** | Esquema público disponible |
| Derivados o clones basados en esas revisiones | Posibles |

Que no sea la 1.0 ni la 1.6 o posterior (la 1.6 agrega la detección de tarjeta en GPIO15) es una **suposición sobre
el criterio del firmware** [I], no un hecho.

**Para confirmarla [P]:** fotografiar ambas caras de la placa, leer la serigrafía de la revisión y la del módulo
ESP32 (WROVER, WROVER-B, etc.), y comparar los componentes con el esquema correspondiente. Por ejemplo, la 1.5 tiene
R17–R19 junto a la microSD, diodos TVS en el USB y R16 (4,7 k) en SIO_DATAIN.

## 2. Diferencias entre revisiones públicas (wiki oficial y README de `fujinet-hardware`)

**1.0 → 1.3:**

- líneas SIO hacia el ESP32 a través de dos 74LS07;
- transistores P/N que desconectan el ESP32 del Atari cuando FujiNet está apagado;
- CP2102 en encapsulado QFN-24;
- reset por firmware ("safe reset") en lugar de un reset duro;
- se quita el puerto JTAG (quedan puntos de prueba);
- pull-down en MOTOR.

Error conocido de la 1.3: en algunos equipos no se puede flashear, y hay una reparación de hardware publicada.

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

En las revisiones **1.3 y 1.5** el bloque J4 (DM3D-SF) está cableado igual [E]:

| Señal | GPIO | Notas |
|---|---|---|
| CLK | 18 | Pin nativo de VSPI |
| CMD (MOSI) | 23 | Pin nativo de VSPI |
| DAT0 (MISO) | 19 | Pin nativo de VSPI |
| CD/DAT3 (CS) | 5 | Pin nativo de VSPI. **Pin de arranque** (valor por defecto al arrancar: alto; en la 1.5 tiene un pull-up de 10 k) |
| DAT1, DAT2 | — | Sin conectar → la tarjeta funciona en **modo SPI** |
| Interruptor de tarjeta | — | Sin conectar en 1.3/1.5 (GPIO15 en la 1.6) |
| Alimentación | 3,3 V | |

- **Funcionamiento observado:** el firmware FujiNet montó la tarjeta y leyó imágenes ATR SD (720×128) y DD
  (720×256) [C].
- **Host SPI:** los cuatro pines son los nativos de VSPI, es decir, **`SPI3_HOST`** [E]. El perfil usa `SPI3_HOST`
  y el controlador aplica **el mismo valor** al bus (`spi_bus_initialize`), al host SDSPI (`host.slot`), al
  dispositivo (`slot.host_id`) y a la liberación (`spi_bus_free`).
- **Si la placa es de otra revisión** permitida por el registro (1.1, 1.2, 1.4 o un derivado), el cableado no está
  documentado y debe confirmarse antes de seleccionar el perfil [P].

## 4. Alimentación y control (esquemas 1.3/1.5) [E]

- **Fuentes de alimentación:**
  - USB_5V y SIO_5V llegan a VCC mediante diodos Schottky PMEG2010;
  - el regulador XC6220B331 (3,3 V) se habilita con el interruptor S5;
  - la microSD y el ESP32 se alimentan de 3,3 V.
- **La placa puede alimentarse del +5 V del Atari**, cuyo pin 10 está especificado para 50 mA como máximo (Anexo A).
  Durante H3c: **solo USB y sin Atari conectado**.
- **74LS07 (U3/U4):** se alimentan de VCC_BUF, conmutado por Q2/Q3, para aislar el bus con la placa apagada.
- **Auto-reset:** el CP2102 controla EN/IO0 con DTR/RTS a través de Q1. El flasheo funcionó en las pruebas
  anteriores [C].
- **Botones:** Safe Reset en GPIO14, ButtonA en GPIO0 y ButtonB en GPIO34.
- **LED:** GPIO2, GPIO4 y GPIO13, activos en bajo.
- **GPIO12 y GPIO15:** solo llegan a puntos de prueba en 1.3/1.5.

## 5. Bus SIO (esquemas 1.3/1.5) [E], solo para descartar conflictos

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

## 6. Chip, flash, PSRAM y particiones

| Dato | Valor | Etiqueta |
|---|---|---|
| Chip | ESP32-D0WD rev. v1.0, doble núcleo, 240 MHz, cristal de 40 MHz | [C] |
| Flash | Fabricante 0xC8 (GigaDevice), dispositivo 0x4018, 16 MB | [C]; modelo GD25Q128 [I] |
| Modo y frecuencia de las imágenes que corrieron | DIO, 40 MHz | [C] |
| USB-serie | Silicon Labs CP210x (CP2102N-QFN24 en los esquemas) | [C] / [E] |
| PSRAM | Heap inicial de 4 280 828 B con FujiNet, coherente con unos 4 MB direccionables | [C] |
| Tamaño físico de la PSRAM | 8 MB según la wiki para la 1.0; el ESP32 mapea 4 MB directamente | [I]; serigrafía del módulo [P] |
| Particiones de **FujiNet** instaladas | nvs 0x9000/28 KiB · main (factory) 0x10000/10 MiB · update 0xA10000/384 KiB · storage (LittleFS) 0xA70000/5,56 MiB · sin otadata | [C] (análisis del volcado D3) |

RetroBridge **no** reutiliza esa tabla. Tiene la suya en `partitions/retrobridge-16mb.csv`.

## 7. Perfil de microSD preparado

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
