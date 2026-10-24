# H4: puerta de hardware SIO en la FujiNet FN32ROV-1.3-Q24

> Estado: **EN PREPARACIÓN. H4 NO está aprobado.** Este documento completa el análisis documental del pinout y de la
> adaptación eléctrica. Las mediciones físicas (`procedimiento-medicion-pasiva-sio.md`) y la revisión final de
> seguridad siguen pendientes.
>
> Mientras H4 no esté aprobado, ningún pin del SIO se configura como salida, y el firmware instalado (`1b1527d`,
> validado en H3c) no toca ninguno de esos GPIO. Este documento no cambia código, firmware, particiones ni
> `sdkconfig`.

**Etiquetas:**
- **[E]** esquema público `FN32ROV-1.3-Q24_schematic.jpg`, revisado directamente el 2026-10-24;
- **[E1.5]** esquema `FN32ROV-1.5-Q24`, solo como comparación;
- **[D]** hoja de datos o documentación del fabricante (TI, Espressif);
- **[M]** manual de Atari C016555, §9;
- **[C]** comprobado en esta placa (H3c o pruebas WSS);
- **[I]** inferencia;
- **[P]** pendiente de medición o de revisión.

Sigue el ADR-012: solo documentación pública de hardware y de fabricantes. No se consultó código de FujiNet.

## 1. Estado de partida

| Ámbito | Comprobado | Documentado sin medir | Pendiente |
|---|---|---|---|
| Placa y módulo | FN32ROV-1.3-Q24 y ESP32-WROVER-B por fotografía; ESP32-D0WD v1.0 y flash de 16 MB por esptool [C] | — | — |
| Firmware | `1b1527d` arranca y no configura GPIO del SIO; Meta A cumplida en el equipo [C] | — | — |
| Pinout SIO | — | Las 10 asignaciones de §2, contra el esquema [E] | Continuidad física (procedimiento, M0) |
| Adaptación de niveles | — | 74LS07, pull-ups y polaridades (§3) [E][D] | Niveles en reposo y en uso (M1–M5) |
| Alimentación | Funciona por USB [C] | Diodos OR, Q2/Q3 y VCC_BUF (§4) [E] | VCC, VCC_BUF y corriente del pin 10 (M1–M3, M6) |
| MOTOR (GPIO36) | — | Conexión directa, sin buffer (§3.4) [E] | **Tensión real de MOTOR CONTROL** (M4) |
| Protocolo SIO | Lógica pura en el host (H2) | Tiempos del manual (arquitectura §1, §9.3) [M] | Velocidad, t0 y t1 reales (H5) |
| Puertos de software | `SioTransport`, `Clock`, `SioSession` y `SioCommandDispatcher`, con pruebas de host (H2) | — | `EspSioTransport` (H5) |

## 2. Pinout verificado en el esquema 1.3 [E]

Todas las líneas del bus pasan por dos SN74LS07 (U3 y U4), **excepto MOTOR, SIO 5V y AUDIO**.

| Señal (lado Atari) | Conector J1/J2 | Recorrido en la placa | GPIO | Dirección en el ESP32 |
|---|---|---|---|---|
| COMMAND− | pin 7 | SIO_CMD → U3 1A → U3 1Y (colector abierto) + **R11 de 10 k a +3V3** → IO39 | **39** | Entrada (GPI) |
| DATA OUT del Atari | pin 5 | SIO_DATAOUT → U4 4A → U4 4Y (colector abierto) → IO33, **sin pull-up externo** | **33** | Entrada |
| DATA IN hacia el Atari | pin 3 | IO21 → U4 3A → U4 3Y (colector abierto) → SIO_DATAIN, **sin pull-up en la 1.3** | **21** | Salida (prohibida hasta H6) |
| CLOCK OUT del Atari | pin 2 | SIO_CKOUT → U4 2A → U4 2Y (colector abierto) → IO32, **sin pull-up externo** | **32** | Entrada |
| CLOCK IN hacia el Atari | pin 1 | IO27 → U4 1A → U4 1Y (colector abierto) → SIO_CKIN | **27** | Salida (no se usa) |
| PROCEED− | pin 9 | IO22 → U3 3A → U3 3Y (colector abierto) → SIO_PROC | **22** | Salida (no se usa) |
| INTERRUPT− | pin 13 | IO26 → U3 4A → U3 4Y (colector abierto) → SIO_INT | **26** | Salida (no se usa) |
| MOTOR CONTROL | pin 8 | SIO_MCTL → **directo** a IO36, con **R2 de 10 k a GND** | **36** | Entrada (GPI) |
| +5V/READY | pin 10 | SIO_5V → D5 (PMEG2010) → VCC; y divisor **R4 2 k / R5 3,9 k** → IO35 | **35** | Entrada analógica (ADC1_CH7) |
| AUDIO IN del Atari | pin 11 | IO25 (DAC) → **R8 470 Ω** → **C3 4,7 µF** → SIO_AUDIOIN | **25** | Salida analógica (no se usa) |
| +12 V | pin 12 | Solo pasa entre J1 y J2; no llega al circuito | — | — |
| GND | pines 4 y 6 | Masa común | — | — |

Las entradas sin uso (U3: 2A, 5A, 6A; U4: 5A, 6A) están a GND, como recomienda TI para entradas no usadas [E][D].

### 2.1 Otros GPIO de la placa: sin conflictos [E]

| Uso | GPIO | Notas |
|---|---|---|
| microSD (SPI3) | 18 CLK, 23 MOSI, 19 MISO, **5 CS** | GPIO5 es pin de arranque; el validador de perfil avisa con `warnings 0x01` (H3c) |
| Botones | 0 ButtonA (arranque), 14 Safe Reset (R3 de 10 k a 3V3, también TMS), 34 ButtonB (R1 de 10 k a 3V3) | — |
| LED (activos en bajo, ánodo a 3V3) | 2 (R10 de 1 k), 4 (R12 de 1,2 k), 13 (R13 de 1 k, también TCK) | GPIO2 es pin de arranque |
| Consola USB (CP2102) | 1 TXD0, 3 RXD0; EN e IO0 por Q1 (auto-reset) | UART0, reservada para diagnóstico |
| Puntos de prueba JTAG | 12 TDI, 15 TDO, 13 TCK, 14 TMS | GPIO12 y GPIO15 son pines de arranque; solo puntos de prueba |
| Flash y PSRAM internas del módulo | 6–11, 16–17 | Reservados |

Ningún GPIO del SIO coincide con la microSD, los botones, los LED, la consola ni la flash o la PSRAM.
`test_fujinet_profile` ya lo comprueba para los pines de la microSD.

## 3. Adaptación eléctrica

### 3.1 SN74LS07 [D]

Es un buffer hexagonal **no inversor** con salidas de **colector abierto** que soportan hasta 30 V.

| Parámetro (TI SN74LS07) | Valor |
|---|---|
| VCC recomendado | 4,75 – 5,25 V |
| VIH mín. / VIL máx. | 2,0 V / 0,8 V |
| IIH / IIL | 20 µA / −0,2 mA |
| VOL | 0,4 V a 16 mA; 0,7 V a 40 mA |
| Corriente de fuga de salida a 30 V | 0,25 mA máx. |
| ICC máx. | 14 mA (salidas altas) / 45 mA (salidas bajas) |

**Funcionamiento de una salida de colector abierto:**
- Con la entrada en **bajo**, el transistor de salida conduce y lleva la línea a bajo.
- Con la entrada en **alto**, el transistor queda abierto y la línea toma el nivel de su pull-up (en el lado
  ESP32, R11 a 3,3 V; en el lado Atari, la resistencia del computador).

Así, el 74LS07 **traduce 5 V → 3,3 V** hacia el ESP32 sin que ningún nivel de 5 V llegue a sus pines, y deja
**liberadas** las líneas del bus cuando no transmite.

### 3.2 Polaridades: GPIO frente a bus [E]

**Ninguna línea se invierte en la placa:** el 74LS07 no es inversor.

| Señal | En el bus SIO [M] | En el GPIO | Consecuencia |
|---|---|---|---|
| COMMAND− | Alto en reposo, **bajo durante la trama de comando** | Igual: alto (3,3 V por R11) en reposo y bajo con la trama | El inicio de la trama es un **flanco descendente** en GPIO39 |
| DATA OUT | Serie asíncrona, reposo en alto (marca) | Igual, **pero solo si GPIO33 tiene pull-up** (§3.3) | UART sin inversión (`uart_set_line_inverse` no hace falta) |
| DATA IN | Serie, reposo en alto (marca), colector abierto del periférico | GPIO21 alto = línea **liberada**; GPIO21 bajo = línea **a bajo** | El reposo de un TX UART (alto) libera la línea; un GPIO21 en bajo **bloquearía el bus** |

**Un nivel alto del ESP32 no es un nivel alto del bus:**
- En DATA IN, un alto en GPIO21 solo **abre** el transistor del 74LS07. El nivel alto del bus lo pone el pull-up del
  Atari; la 1.5 agrega R16 de 4,7 k a SIO_5V, y la 1.3 no tiene ninguno.
- En las entradas, el alto en el GPIO lo pone R11 (COMMAND) o el pull-up que configure el firmware (DATA OUT y
  CLOCK OUT).

### 3.3 Pull-ups y pull-downs

| Línea | 1.3 [E] | 1.5 [E1.5] | Implicación |
|---|---|---|---|
| IO39 (COMMAND, lado ESP32) | R11 de 10 k a 3V3 | R12 de 10 k a 3V3 | Reposo definido en alto, incluso con U3 sin alimentación |
| IO33 (DATA OUT, lado ESP32) | **ninguno** | **ninguno** | Sin pull-up, la línea **flota** cuando el bus está en marca. **H5 debe activar el pull-up interno de GPIO33** (unos 45 kΩ [D]) o se recibirá basura |
| IO32 (CLOCK OUT, lado ESP32) | ninguno | ninguno | No se usa en modo asíncrono; se deja sin configurar |
| SIO_DATAIN (lado bus) | **ninguno** | R16 de 4,7 k a SIO_5V | En la 1.3, el alto de DATA IN depende solo del Atari; sin Atari, la línea flota |
| IO36 (MOTOR) | R2 de 10 k a GND | R3 de 10 k a GND | Bajo sin Atari; el nivel alto depende del Atari (§3.4) |
| microSD | sin pull-ups | R17–R19 de 10 k | Ver `fujinet-atari-v1x.md` §3 |

**Sobre el pull-up interno de GPIO33 [D][I]:**
- GPIO32 y GPIO33 son pads RTC. La errata **GPIO-3.6** indica que sus pull-ups solo se controlan por registros
  RTC; los controladores GPIO de ESP-IDF ≥ 2.1 lo resuelven solos.
- Con unos 45 kΩ y una capacidad estimada de 10–20 pF, la constante de tiempo es de 0,5–0,9 µs, frente a un bit de
  unos 52 µs a 19 200 baudios. Parece suficiente, pero **hay que medir el flanco de subida** (M5).
- Un pull-up externo sería una modificación de hardware, fuera del alcance de RetroBridge.

### 3.4 MOTOR: conexión directa a GPIO36 [E][M][D]

- SIO_MCTL llega a IO36 **sin buffer ni divisor**, solo con R2 de 10 k a GND.
- El manual de Atari solo dice "alto = encendido" y **no especifica la tensión**.
- El ESP32 admite como máximo **VDD + 0,3 V (unos 3,6 V)** en sus entradas [D].
- **[P] Antes de cualquier uso de GPIO36 hay que medir la tensión máxima de MOTOR CONTROL** con el Atari
  encendiendo el motor del casete (M4).
- [C] La placa ya funcionó con este Atari durante las pruebas WSS sin daño aparente, pero eso no demuestra que el
  nivel esté dentro del margen.
- RetroBridge no necesita MOTOR en la v1, que no emula casete.

### 3.5 GPIO 34–39 y erratas del ESP32 [D]

- **Solo entrada:** GPIO 34–39 no pueden ser salidas y **no tienen pull-ups ni pull-downs internos**. En esta placa
  cada uno tiene una resistencia externa: IO34 (R1), IO35 (divisor), IO36 (R2) e IO39 (R11).
- **Errata GPIO-3.11:** al encender SAR ADC1, SAR ADC2 o el AMP, las entradas **GPIO36 y GPIO39 se fuerzan a bajo
  unos 80 ns**. Esto afecta a nuestra revisión v1.0. Leer SIO_5V con ADC1 (IO35) mientras se escucha COMMAND podría
  provocar flancos falsos.
  - Medida: no usar el ADC durante la escucha, **y** filtrar en software: un COMMAND real dura milisegundos.
  - La documentación GPIO de ESP-IDF v6.1 lo advierte expresamente.
- **Errata GPIO-3.14:** GPIO32–GPIO39 comparten registros de interrupción. Si un pin del grupo usa interrupción por
  **flanco**, ningún otro pin del grupo puede tener interrupciones (ButtonB en 34, MOTOR en 36).
  - Medida: emular los flancos con **interrupciones por nivel**, como propone la errata (§5 del diseño H5), o
    garantizar que COMMAND sea la única interrupción del grupo.

### 3.6 GPIO5, pin de arranque y CS de la microSD [D][C]

- Como pin de arranque, GPIO5 solo fija, junto con MTDO, la temporización del **esclavo SDIO** (hoja de datos del
  ESP32, tabla de pines de arranque). No afecta al arranque desde la flash ni al SIO.
- La tarjeta no maneja CS, así que no fuerza su nivel al arrancar.
- `warnings 0x01` (`PinWarning::kStrappingPin`) es el aviso esperado del perfil.
- [P] El `flash read err, 1000` transitorio de H3c sigue abierto (observación 1 del informe H3c). No hay evidencia
  de relación con GPIO5.

## 4. Alimentación y aislamiento [E][M][D]

- **OR de diodos:** USB_5V → D4 (PMEG2010) → VCC ← D5 (PMEG2010) ← SIO_5V. La fuente de mayor tensión alimenta, y
  los diodos impiden que una fuente retroalimente a la otra; en particular, que USB alimente el pin 10 del Atari.
- **Regulador:** el interruptor S5 habilita el XC6220B331, que da 3,3 V para el ESP32 y la microSD.
- **Q2/Q3 (VCC_BUF):**
  - Con +3V3 presente, Q3 (DMG2302U, canal N, puerta por R16 de 1 k) conduce, lleva a bajo la puerta de Q2
    (DMP2035U, canal P, R14 de 10 k a VCC) y Q2 entrega **VCC_BUF = VCC** a U3 y U4.
  - Sin +3V3 (S5 apagado), Q2 queda abierto, los 74LS07 se quedan sin alimentación, sus salidas quedan abiertas y
    el bus queda **aislado** del ESP32.
- **[I] Tensión de los 74LS07:** VCC_BUF ≈ VCC = 5 V menos la caída del Schottky (unos 0,3–0,4 V), es decir, unos
  4,6–4,7 V, **por debajo de los 4,75 V mínimos recomendados** por TI. Hay que medirlo (M1).
- **Corriente del pin 10:** está especificado en **50 mA** [M], y solo los dos 74LS07 pueden consumir hasta
  2 × 45 mA [D], más el ESP32. **[I] Alimentar la placa solo desde el SIO probablemente excede la especificación de
  Atari.** Durante H4/H5: **siempre por USB**. Hay que medir el reparto de corriente con las dos fuentes (M6).
- **Atari apagado con la FujiNet encendida por USB:**
  - [I] Las entradas del 74LS07 verán las líneas del Atari sin alimentación, probablemente en bajo, y entonces
    **IO39 indicará COMMAND activo**. H5 debe tolerar un COMMAND bajo permanente sin bloquearse.
  - D5 impide alimentar el Atari.
- **Carga de las líneas del Atari:**
  - El manual pide IIL ≤ 5 µA al periférico [M], y el 74LS07 consume hasta 0,2 mA [D].
  - [I] Las salidas TTL del Atari lo admiten con holgura, pero es una desviación de la especificación, ya presente en
    el hardware y en el uso diario de la placa.
- **Masas:** la masa del PC (USB) y la del Atari se unen por el GND del SIO. Hay riesgo de lazos de masa con equipos
  en distintos enchufes (procedimiento, §2).

## 5. Matriz de riesgos eléctricos

| # | Riesgo | Causa | Impacto | Prevención | Verificación |
|---|---|---|---|---|---|
| R1 | Sobretensión en GPIO36 | MOTOR directo a IO36 sin divisor; nivel de Atari desconocido | Daño del ESP32 | No usar MOTOR; medir antes con el ESP32 desconectado del bus si es posible | M4 [P] |
| R2 | Bus bloqueado en bajo | GPIO21 configurado en bajo (o TX mal enrutado) | El Atari no puede comunicarse con ningún periférico | H5 sin TX; verificar con `gpio_dump_io_configuration` que GPIO21 no está asignado | Revisión de código y M5 |
| R3 | Recepción de basura en DATA OUT | IO33 sin pull-up externo | Tramas corruptas | Pull-up interno en GPIO33 (H5); medir el flanco | M5 [P] |
| R4 | Flancos falsos de COMMAND | Errata GPIO-3.11 (ADC) | Tramas fantasma | Sin ADC durante la escucha; filtro por duración | Pruebas de host y M5 |
| R5 | Interrupciones perdidas | Errata GPIO-3.14 (flanco en el grupo 32–39) | COMMAND perdido | Interrupción por nivel emulando flancos | Pruebas de host y M5 |
| R6 | Sobrecarga del pin 10 del Atari | Placa alimentada desde el SIO (límite de 50 mA) | Caída del +5 V del Atari | Alimentar siempre por USB en H4/H5 | M6 [P] |
| R7 | 74LS07 fuera del rango de VCC | Caída en D4/D5 y Q2 | Márgenes reducidos | Medir VCC_BUF con carga | M1 [P] |
| R8 | Lazos de masa | PC y Atari en distintas tomas | Ruido, errores o daño | Misma toma, o portátil con batería | Procedimiento §2 |
| R9 | Daño de un instrumento | Sonda de 3,3 V en una línea de 5 V | Daño del instrumento o de la placa | Usar solo instrumentos con tolerancia a 5 V confirmada, u osciloscopio con sonda 10× | Procedimiento §1 |
| R10 | Descarga electrostática por USB | La 1.3 no tiene TVS | Daño del CP2102 o del ESP32 | Pulsera antiestática; conectar primero la masa | — |
| R11 | Estabilidad de la microSD | La 1.3 no tiene pull-ups en la microSD | Errores de lectura | Pruebas prolongadas (H3c, observación 5) | Pendiente |
| R12 | COMMAND bajo permanente con el Atari apagado | Entradas del 74LS07 con líneas sin alimentación | H5 bloqueado o falsos comandos | Detección de COMMAND bajo prolongado | M6 [P] y pruebas de host |
| R13 | Salida de audio indeseada | Uso accidental del DAC en GPIO25 | Ruido en el audio del Atari | GPIO25 nunca se configura en la v1 | Revisión de código |

## 6. Anexo A de la arquitectura: estado

| # | Requisito | Estado |
|---|---|---|
| 1–3 | Modelo y revisión, módulo, USB-serie y auto-reset | **Completo** [C][E] |
| 4 | Mapa de pines completo | **Completo en el esquema** [E]; continuidad física pendiente (M0) |
| 5 | Adaptación de nivel y dirección de cada línea | **Analizada** (§3) [E][D]; mediciones pendientes |
| 6 | Pines prohibidos, de arranque y de solo entrada | **Analizado** (§2.1, §3.5, §3.6) |
| 7 | Origen de la alimentación y protección contra retroalimentación | **Analizado** (§4) [E]; medición pendiente (M6) |
| 8 | Corriente tomada del pin 10 | **Pendiente** (M6); riesgo R6 |
| 9 | Que no se use el pin 12 | **Completo** [E]: solo pasa entre J1 y J2 |
| 10 | Requisitos eléctricos del manual | **Contrastados** [M][D]; IIL del 74LS07 fuera de especificación (§4) |
| 11 | Sin suponer tolerancia a 5 V en el ESP32 | **Cumplido en el análisis**; el riesgo restante es MOTOR (R1) |
| 12 | Niveles en reposo medidos con el Atari encendido | **Pendiente** (M3, M4) |
| 13–15 | Atari (modelo, NTSC/PAL, ROM), otros dispositivos, instrumentos | **Pendiente** (lo aporta el equipo) |
| 16–17 | microSD FAT32 e imágenes ATR con derechos claros | **Completo** (H3c; imágenes sintéticas de `make_atr.py`) |

## 7. Checklist de aceptación de H4

H4 se aprueba **solo** cuando todos los puntos estén marcados y el equipo firme la revisión:

- [ ] Continuidad de J1 a U3/U4 y a los GPIO de §2, y paso de J1 a J2, con la placa desenergizada (M0).
- [ ] R11, R2, R4/R5 y su relación con IO39, IO36 e IO35 comprobadas (M0).
- [ ] VCC, VCC_BUF y +3V3 medidos solo con USB, con S5 encendido y apagado (M1, M2).
- [ ] Aislamiento: con S5 apagado, VCC_BUF ≈ 0 V y las líneas del bus no son forzadas por la placa (M2, M3).
- [ ] Niveles en reposo de COMMAND, DATA IN y DATA OUT con el Atari encendido y el firmware sin configurar el SIO
      (M3, M4).
- [ ] **Tensión máxima de MOTOR CONTROL ≤ 3,6 V en IO36**, o decisión explícita de mitigación (M4).
- [ ] +5 V del pin 10 y reparto de corriente con USB y SIO a la vez; sin retroalimentación hacia el Atari (M6).
- [ ] Observación pasiva de COMMAND y DATA OUT con un instrumento de tolerancia a 5 V confirmada: velocidad del bus,
      polaridad y forma del flanco en IO33 (M5).
- [ ] Atari documentado: modelo, NTSC/PAL, ROM y otros dispositivos (Anexo A, 13–15).
- [ ] Perfil de pines SIO en `rb_board` con validación (entregable de código de H4, en un PR aparte y aprobado).
- [ ] Matriz de riesgos revisada y firmada; resultados en `docs/mediciones/`.

## 8. Fuentes

- `FujiNetWIFI/fujinet-hardware`, `ATARI/Old-Versions/FN32ROV-1.3-Q24/FN32ROV-1.3-Q24_schematic.jpg` y `README.md`;
  `FN32ROV-1.5-Q24/FN32ROV-1.5-Q24_schematic.jpg` (CERN OHL 2.0, documentación de hardware).
- Texas Instruments, *SN74LS07 Hex Buffers and Drivers With Open-Collector High-Voltage Outputs*.
- Espressif, *ESP32 Series Datasheet* v5.3: características DC, máximos absolutos y pines de arranque.
- Espressif, *ESP32 Series SoC Errata*: GPIO-3.6, GPIO-3.11 y GPIO-3.14.
- Espressif, ESP-IDF v6.1: *GPIO & RTC GPIO* y *UART*; `components/esp_driver_uart/Kconfig`.
- Atari, *Home Computer Technical Reference Notes* (C016555), §9: conector y especificaciones eléctricas.

El detalle, con fechas y URL, está en `docs/fuentes.md`.
