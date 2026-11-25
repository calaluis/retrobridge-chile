# H4: puerta de hardware SIO en la FujiNet FN32ROV-1.3-Q24

> Estado: **EN PREPARACIÓN. H4 NO está aprobado.** Este documento completa el análisis documental del pinout y de la
> adaptación eléctrica. Las mediciones físicas (`procedimiento-medicion-pasiva-sio.md`) y la revisión final de
> seguridad siguen pendientes.
>
> Mientras H4 no esté aprobado, ningún pin del SIO se configura como salida, y el firmware instalado (`15b153f`,
> diagnóstico H4 validado en `h4-validacion-diagnostico-2026-10-24.md` y `h4-regresion-h3c-2026-10-25.md`) no toca
> ninguno de esos GPIO. Este documento no cambia código, firmware, particiones ni `sdkconfig`.
>
> **Propuesta en revisión:** separar esta puerta en H4-L (escucha) y H4-T (transmisión):
> `h4-puertas-escucha-transmision.md` (**PROPUESTA NO APROBADA**). Mientras no se apruebe, rige el §7 completo.
>
> **Enmienda aprobada (MOTOR y GPIO36):** `enmienda-motor-gpio36.md` (PR #108). Separa el
> riesgo de la placa del comportamiento del firmware. No cambia ninguna puerta.
>
> **Estado vigente (2026-11-25):** **M3a aprobada por aceptación de riesgo** del responsable, que cubre solo R1
> (`docs/mediciones/puertas-aceptacion-riesgo-2026-11-25.md`). **H4 sigue sin aprobar:** R2 y R7 no están aceptados; R6 está aceptado solo con la FujiNet por USB (`docs/mediciones/riesgos-h4-2026-11-25.md`). **La validación eléctrica sigue
> pendiente** para las dos. Antes del banco T9b solo se instala `menu-sio`.
> Donde este documento dice «M3a pendiente», describe el estado anterior a esa decisión.

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
| MOTOR (GPIO36) | — | Conexión directa, sin buffer (§3.4) [E] | **Tensión real de MOTOR CONTROL, medida en el Atari solo** (M3a, punto de detención). 2026-11-25: B4 = 4,89 V sin carga, **«Detener»**; con la FujiNet conectada (conector posterior): 4,87 V con S5 encendido, 3,16 V con S5 apagado; picos pendientes |
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
| ICCH / ICCL máx. | 14 mA / 45 mA, **por circuito integrado**, en las condiciones de la hoja de datos: VCC máximo y **todas** las salidas en alto (ICCH) o **todas** en bajo (ICCL). Son límites de peor caso, no consumo típico |

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
| DATA OUT | Serie asíncrona, reposo en alto (marca) | Misma polaridad. Con el bus en marca, la salida del 74LS07 queda abierta, y el nivel alto en IO33 depende de lo que haya en ese nodo: el esquema no muestra pull-up (§3.3) | UART sin inversión (`uart_set_line_inverse` no hace falta) |
| DATA IN | Serie, reposo en alto (marca), colector abierto del periférico | GPIO21 alto = línea **liberada**; GPIO21 bajo = línea **a bajo** | El reposo de un TX UART (alto) libera la línea; un GPIO21 en bajo **bloquearía el bus** |

**Un nivel alto del ESP32 no es un nivel alto del bus:**
- En DATA IN, un alto en GPIO21 solo **abre** el transistor del 74LS07. El nivel alto del bus lo pone el pull-up del
  Atari; la 1.5 agrega R16 de 4,7 k a SIO_5V, y la 1.3 no tiene ninguno.
- En las entradas, el alto en el GPIO lo pone R11 (COMMAND). En DATA OUT y CLOCK OUT, el esquema no muestra
  resistencia; el nivel real se mide en H4 y la solución, si hace falta, se decide en H5 (§3.3).

### 3.3 Pull-ups y pull-downs

| Línea | 1.3 [E] | 1.5 [E1.5] | Implicación |
|---|---|---|---|
| IO39 (COMMAND, lado ESP32) | R11 de 10 k a 3V3 | R12 de 10 k a 3V3 | Reposo definido en alto, incluso con U3 sin alimentación |
| IO33 (DATA OUT, lado ESP32) | **ninguno dibujado** | **ninguno dibujado** | Ver el análisis debajo de esta tabla |
| IO32 (CLOCK OUT, lado ESP32) | ninguno dibujado | ninguno dibujado | No se usa en modo asíncrono; se deja sin configurar |
| SIO_DATAIN (lado bus) | **ninguno** | R16 de 4,7 k a SIO_5V | En la 1.3, el alto de DATA IN depende solo del Atari; sin Atari, la línea flota |
| IO36 (MOTOR) | R2 de 10 k a GND | R3 de 10 k a GND | Bajo sin Atari; el nivel alto depende del Atari (§3.4) |
| microSD | sin pull-ups | R17–R19 de 10 k | Ver `fujinet-atari-v1x.md` §3 |

**IO33 e IO32: qué se sabe y qué no**

| Nivel | Contenido |
|---|---|
| **Hecho en el esquema [E]** | En la 1.3 y en la 1.5 no hay ninguna resistencia dibujada entre IO33 (o IO32) y una alimentación. La única carga visible es la salida de colector abierto del 74LS07, que solo puede llevar la línea a bajo |
| **Comportamiento real [P]** | Desconocido hasta medirlo (M4, M5). Puede haber elementos que el esquema no muestra, y el estado del pad del ESP32 depende de su configuración |
| **Hipótesis [I]** | Si el pad queda sin pull-up y la salida del 74LS07 está abierta (bus en marca), la entrada podría quedar **flotante**, con un nivel indefinido que una UART podría interpretar mal. Es una hipótesis, **no una conclusión** |
| **Decisión futura (H5)** | Si la medición confirma un nivel alto indefinido o un flanco lento, una opción es el pull-up interno de GPIO33 (unos 45 kΩ [D]). La decisión, su valor y su verificación pertenecen a **H5**, con medición previa; H4 no la toma |

Datos técnicos para esa decisión [D][I]:
- GPIO32 y GPIO33 son pads RTC. La errata **GPIO-3.6** indica que sus pull-ups solo se controlan por registros
  RTC; los controladores GPIO de ESP-IDF ≥ 2.1 lo resuelven solos.
- Estimación, no medición: con unos 45 kΩ y 10–20 pF supuestos, la constante de tiempo sería de 0,5–0,9 µs, frente
  a un bit de unos 52 µs a 19 200 baudios.
- Un pull-up externo sería una modificación de hardware, fuera del alcance de RetroBridge.

### 3.4 MOTOR: conexión directa a GPIO36 [E][M][D]

**Revisión de la auditoría eléctrica del 2026-10-26.** Además de las etiquetas del documento, esta sección usa
**[E-Atari]** (esquema de terceros del 65XE o de la XC12), **[T]** (cálculo teórico) y **[H]** (registro histórico
del equipo, proyecto WSS).

**Recorrido en la FN32ROV-1.3-Q24 [E]** (esquema y BOM oficiales, `fujinet-hardware` en el commit `0e539b8`):

| Elemento | Conexión |
|---|---|
| J1, pin 8 «MCTL» | Red `SIO_MCTL`; también va a J2, pin 8, de paso |
| R2, 10 kΩ (BOM: «R2 10k RES_0402») | De `SIO_MCTL` a GND: fija el reposo en bajo |
| U1, pin 4 (SENSOR_VP/IO36/ADC1_CH0) | **Directo** a `SIO_MCTL` |

MOTOR **no** pasa por los 74LS07, ni por los transistores de aislamiento Q2/Q3, ni por ningún divisor o resistencia
en serie [E]. El ESP32 admite como máximo **VDD + 0,3 V (unos 3,6 V)** en sus entradas [D].

**Lado del Atari: 65XE NTSC del equipo (2026-10-26).** En su esquema (redibujo de terceros, hoja 5), MOTOR CONTROL
sale del colector de **Q7, un PNP con el emisor en +5 V**, hacia el pin 8 a través de L11 (bobina), **sin resistencia
en serie**; la base la controla el PIA (CA2) por R91 (2k7; legible en la copia de 2026-11-23, abajo). El modelo de Q7 no es legible
[E-Atari]. El manual de Atari solo dice "alto = encendido" y **no especifica la tensión** [M].

**Tensión prevista, no medida:**
- Con el motor encendido: **≈ 4,7–4,9 V** en el pin 8 (5 V menos la saturación de Q7, con la carga de R2 o de la
  casetera) [T]. Es una **previsión a partir de un esquema de terceros, no una medición** [P].
- No hay resistencia en serie ni limitador dedicado para IO36: la corriente disponible depende de Q7 y de la carga
  conectada [T][I].
- Con el motor apagado, Q7 queda abierto y el nivel lo fija la carga del periférico (R2 en la FujiNet) [I].
- La carga del lado del casete no limita esa tensión. En la XC12, el pin 8 entra por R3 (27 k) a la base del
  transistor que conmuta el motor **y además alimenta la electrónica de la casetera** (ver «Ampliación del
  2026-11-23»); la carga es mayor que R2 sola, pero tampoco hay limitación en serie [E-Atari][I].

**Riesgos (no son daños confirmados):**
- **Exceso de tensión en IO36:** la tensión prevista supera el máximo admisible; con la placa alimentada, la
  protección interna del pin conduciría hacia +3V3 sin limitación de corriente en el circuito [I].
- **Alimentación parásita:** con la FujiNet apagada y MOTOR en alto, el ESP32 podría alimentarse parcialmente a través
  de la protección del pin [I].
- La forma de alimentar la placa (USB o +5 V del SIO) no cambia el recorrido de MOTOR [I].

**Ampliación del 2026-11-23: referencias mandantes del responsable.** El responsable volvió a aportar los dos
esquemas de terceros (65XE, Jerzy Sobola, 2003, 9 hojas; XC12, mismo autor, 2002) y decidió que **mandan** para el
diseño de la casetera: «esos datos que te pasé son los que mandan, aunque sean referencias». Siguen siendo redibujos
de terceros [E-Atari], no se versionan y **no sustituyen a M3a**. Lectura de la copia aportada:

| Punto | 65XE [E-Atari] (hojas 5 y 9) |
|---|---|
| Quién enciende MOTOR | PIA U23, pin 39 (CA2) → R91 **2k7** → base de Q7 (PNP, emisor en +5A). **CA2 en bajo = motor encendido** [I] |
| Salida | Colector de Q7 → L11 (ferrita) → `MTR CTRL` → SIO pin 8. C81 (1 nF) a masa. **Sin resistencia en serie** |
| Motor apagado | Q7 abierto y **ninguna resistencia del Atari baja el pin 8**: queda flotando hasta que el periférico lo fija (R2 en la FujiNet) |
| Pin 10 (+5 V / READY) | +5A, el +5 V principal del Atari, por L10 (ferrita), con C79 (22 µF) y C80 (1 nF) a masa. Coincide con los 4780 mV medidos en el banco T8 de `menu-cinta` [C] |

| Punto | XC12 [E-Atari] |
|---|---|
| Motor | Lo alimenta el **pin 10** por S1 (PLAY) y Q1 (PNP). Q2 (NPN) enciende Q1; su base recibe el **pin 8** por R3 (27 k). FWD (S2) y REW (S3) puentean Q2: el motor gira sin MOTOR solo para avanzar o rebobinar |
| Electrónica | El **pin 8** también alimenta la electrónica de la casetera: los amplificadores LM324, Q7 (salida hacia DATA IN, pin 3) y el LED (R4, CR7). Sin MOTOR, la XC12 **no envía datos**, aunque PLAY esté pulsado |
| Carga sobre el pin 8 | No es solo R3: es toda esa electrónica. La caída en Q7 del Atari será algo mayor que con R2 sola [I]; la tensión sigue sin medir [P] |
| Otros pines | 3 DATA IN, 4 GND, 5 DATA OUT (grabación), 11 audio. Los demás, sin conectar |

**Uso en el 65XE, según el responsable (2026-11-23), testimonio:** arrancar con START (+OPTION) → el Atari pita → **RETURN**
enciende MOTOR → carga; al **terminar** el programa o ante un **error**, el sistema de carga apaga MOTOR. No se sabe aún
si algunos programas lo apagan y encienden entre bloques [P].

**Consecuencias para el diseño:**
- Una casetera fiel **obedece el pin 8**: montar un `.cas` en `C:` equivale a dejar PLAY pulsado, y los datos salen
  solo mientras el Atari mantiene MOTOR en alto. La opción «casetera sin MOTOR» (reproducir con las pausas del `.cas`
  y un botón de «play») **queda descartada**: ninguna XC12 funciona así.
- Por eso leer MOTOR es imprescindible para T9 de `menu-cinta`, y M3a era el punto de detención (histórico: se
  aprobó por aceptación de riesgo el 2026-11-25, sin validación eléctrica).
- Según el esquema del 65XE, del lado del Atari **no hay resistencia en serie ni limitador dedicado** que proteja
  IO36: la corriente disponible depende del circuito de Q7. R1 sigue sin validar eléctricamente; desde el
  2026-11-25 es un riesgo aceptado por el responsable.

**Evidencia histórica [H] (proyecto WSS, G5-2, prueba T5, `FUJINET_WSS_P5_G5_2_REPORT.md` §4):**
- El registro de la FujiNet muestra «MOTOR ON: activating cassette» a las 23:01:22 y la emisión del casete hasta
  las 23:16:49: **MOTOR estuvo activo durante 15 min 27 s**, con la carga completada («PROGRAMA CARGADO») y la placa
  funcionando después.
- **Límites:** el informe no indica qué Atari se usó (65XE u 800XL) ni la alimentación exacta, y no hubo ninguna
  medición eléctrica. Demuestra **funcionamiento satisfactorio en esas condiciones**, no que el nivel o la corriente
  estén dentro de especificación ni la ausencia de degradación. **No sustituye a M3a.**

**Revisiones posteriores de FujiNet [E]:** según el README de la FN32ROV-1.6, esa revisión **pasa MOTOR por un buffer**
(«Run Motor Control signal through buffer»), con un pull-up de 10 k en el lado del ESP32 y un pull-down de 2 k en el
lado del Atari. El esquema de la 1.6 se revisó el 2026-11-23 (U3 puerta 2, R24 2K, R2 10K a 3V3):
ver `proteccion-motor-gpio36.md` §2, propuesta de protección **no aprobada**.

**Medición M3a parcial del 2026-11-25:** con el Atari solo y el motor encendido (`POKE 54018,52`), un tester
marcó **4,89 V** en el pin 8, sin ningún periférico (sin carga). Supera los 3,6 V de V_IH máx: el resultado es **«Detener»**
(`docs/mediciones/m3a-motor-2026-11-25.md`). Los picos (B2, B5) siguen sin medir, porque falta un osciloscopio.
**Decisión D3 del responsable (`enmienda-motor-gpio36.md` §7):** **aceptación de riesgo acotada**. El responsable asume el riesgo eléctrico preexistente de su FN32ROV-1.3-Q24 y
permite conectarla al SIO del 65XE **solo para disco, menú y TNFS**, hasta tener una protección de MOTOR validada.
Condiciones:
- sin casete ni lectura de MOTOR: `kTapeOutput` sigue en `false` y GPIO36 no se configura;
- regla operativa: no iniciar cargas de casete con la FujiNet conectada. Reduce la exposición, no la elimina:
  MOTOR puede activarse por otro camino (un programa que maneje CA2, por ejemplo);
- **no es una validación eléctrica:** no aprueba M3a, H4 ni DATA IN, no autoriza las etapas M3–M6 del
  procedimiento ni ninguna medición que conecte MOTOR a la placa, y no cambia `puertas.json`.
- D3 solo trata el riesgo de MOTOR. El uso de DATA IN sigue regido por sus propias puertas y paquetes.

Es una excepción por decisión del responsable a la regla de este punto de detención («no se autoriza ninguna
conexión»), limitada a ese uso. D3 no aprobó M3a.

**Actualización 2026-11-25 (opción C y revisión del PR #110):** el responsable aprobó **M3a por aceptación de riesgo**,
que cubre **solo R1**, con estas palabras: «Acepto R1 como riesgo eléctrico preexistente de mi FujiNet original. No
autorizo automáticamente R2, R6 ni R7. Mantener H4 pendiente hasta evaluar esos riesgos por separado. La aceptación
de R1 no constituye validación eléctrica.» Registro: `docs/mediciones/puertas-aceptacion-riesgo-2026-11-25.md`.
- Para MOTOR, la aprobación reemplaza a D3. Para DATA IN, D3 no cambia.
- B4 sigue en «Detener», y los puntos sin medir siguen sin medir.
- **H4 sigue sin aprobar.** `kTapeOutput` sigue en `false` hasta el banco T9b.

**Mediciones del mismo día con la FujiNet conectada (fuera de la ficha M3a):** en el conector SIO posterior de la
placa, sin USB, el pin 8 marcó **4,87 V con S5 encendido** y **3,16 V con S5 apagado**. No son mediciones del pad de
GPIO36 ni la medición C. Con S5 encendido, la carga de la placa casi no baja MOTOR. Con S5 apagado, algo en la placa
carga MOTOR mucho más que R2, y la causa no se conoce: una de las hipótesis no verificadas es la alimentación
parásita por IO36. Detalle e hipótesis HS1–HS4: `docs/mediciones/m3a-motor-2026-11-25.md` §4.

**[P] Punto de detención para validar M3a eléctricamente (sin cambios; la aceptación de riesgo no lo reemplaza):** la primera medición del nivel alto de MOTOR CONTROL se hace **en el
Atari solo, con la FujiNet desconectada del SIO** (procedimiento, etapa M3a), con el motor del casete encendido y un
osciloscopio para ver también los picos. Debe confirmar el nivel real y los picos en el 65XE físico, que puede no
coincidir con un esquema de terceros.
- No se conectan los dos equipos para descubrir después que la tensión supera el límite del ESP32.
- **Si la señal puede superar los niveles admisibles del ESP32, no se autoriza ninguna conexión** de la FujiNet al
  Atari hasta diseñar, revisar y validar una mitigación aprobada.
- **Excepción operativa D3** (más arriba): uso acotado a disco, menú y TNFS por aceptación de riesgo del responsable.
  No se aplica a las mediciones ni a las validaciones: M3–M6 y cualquier prueba con MOTOR conectado a la placa
  siguen bajo la regla anterior.

**Alternativa de mitigación, pendiente de análisis y aprobación (no recomendada todavía):** excluir el pin 8 entre el
Atari y la FujiNet, por ejemplo con un cable o adaptador de prueba sin ese pin. RetroBridge v1 no usa MOTOR, que no
emula casete. Habría que analizar, entre otras cosas, el efecto sobre los equipos conectados después de la FujiNet
(una casetera en J2 dejaría de recibir MOTOR) y cómo se validaría (por ejemplo, continuidad del pin 8 con multímetro).
Implica un cambio de cableado o de hardware y requiere una aprobación aparte.
Especificación de esta alternativa: `mitigacion-c-motor-pin8.md` (**PROPUESTA NO APROBADA**).

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
- **Tensión de los 74LS07, estimación [I], no medición:** VCC_BUF ≈ VCC = 5 V menos la caída del Schottky y de Q2.
  Con una caída supuesta de unos 0,3–0,4 V, quedaría en unos 4,6–4,7 V, **posiblemente por debajo de los 4,75 V
  mínimos recomendados** por TI. El valor real depende de la tensión del USB, de la corriente y de los componentes.
  **Solo la medición (M1) dirá si el riesgo existe**; hasta entonces es un riesgo por confirmar (R7).
- **Corriente del pin 10:**
  - Está especificado en **50 mA** [M].
  - Los 45 mA del 74LS07 son el **máximo de la hoja de datos con todas las salidas en bajo y VCC máximo** [D]. No son
    el consumo permanente de la placa: en reposo las entradas de los buffers están en alto y la referencia es ICCH
    (máx. 14 mA por circuito), al que se suma el consumo del ESP32, que esta revisión no midió.
  - [I] Aun así, la suma ESP32 + buffers + microSD probablemente supera los 50 mA, así que alimentar la placa
    **solo** desde el SIO podría exceder la especificación de Atari.
  - Durante H4/H5: **siempre por USB**. El consumo real y su reparto entre USB y SIO se miden en M6.
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
| R1 | Sobretensión en GPIO36 y alimentación parásita | MOTOR (pin 8) directo a IO36, solo con R2 de 10 k a GND [E]. En el 65XE del equipo, el esquema indica un PNP saturado a +5 V sin resistencia en serie: ≈ 4,7–4,9 V previstos (§3.4) [E-Atari][T][I]; **medido el 2026-11-25: 4,89 V sin carga en el Atari solo**, y en el conector posterior de la FujiNet, sin USB, **4,87 V con S5 encendido** y **3,16 V con S5 apagado** (causa sin verificar; `docs/mediciones/m3a-motor-2026-11-25.md` §4). Con la FujiNet apagada, posible alimentación parásita por la protección del pin [I]. Antecedente: MOTOR activo 15 min 27 s en G5-2 T5 con funcionamiento satisfactorio [H], sin medición eléctrica. La FN32ROV-1.6 pasó MOTOR por un buffer (README) | Riesgo de daño o degradación del ESP32 (no confirmado) | **Medir primero en el Atari solo, con la FujiNet desconectada; sin conexión si supera el límite** hasta tener una mitigación validada (excepción operativa D3: uso acotado por aceptación de riesgo, sin mediciones ni casete); no usar MOTOR en la v1. Alternativa pendiente de análisis y aprobación: excluir el pin 8 (§3.4) | M3a: B4 medido, **«Detener»**; picos, A, D y E pendientes; causa de la caída con S5 apagado sin verificar [P]. **Riesgo aceptado por el responsable el 2026-11-25**, sin validación eléctrica (`docs/mediciones/puertas-aceptacion-riesgo-2026-11-25.md`) |
| R2 | Bus bloqueado en bajo | GPIO21 configurado en bajo (o TX mal enrutado) | El Atari no puede comunicarse con ningún periférico | H5 sin TX; verificar con `gpio_dump_io_configuration` que GPIO21 no está asignado | Revisión de código y M5. M5 no observa DATA IN: su liberación la cubren C8, C8a y C8b (`docs/hardware/h4-c-casetera.md` §3). **No aceptado** por el responsable (2026-11-25, `docs/mediciones/riesgos-h4-2026-11-25.md`) |
| R3 | Posible recepción errónea en DATA OUT [I] | El esquema no muestra pull-up en IO33; el comportamiento real no está medido | Tramas corruptas, si la hipótesis se confirma | Medir nivel y flanco; decidir en H5 (por ejemplo, el pull-up interno) | M4, M5 [P] |
| R4 | Flancos falsos de COMMAND | Errata GPIO-3.11 (ADC) | Tramas fantasma | Sin ADC durante la escucha; filtro por duración | Pruebas de host y M5 |
| R5 | Interrupciones perdidas | Errata GPIO-3.14 (flanco en el grupo 32–39) | COMMAND perdido | Interrupción por nivel emulando flancos | Pruebas de host y M5 |
| R6 | Sobrecarga del pin 10 del Atari [I] | Placa alimentada desde el SIO (límite de 50 mA); consumo real no medido | Caída del +5 V del Atari | Alimentar siempre por USB en H4/H5 | M6 [P]. **Aceptado solo con la FujiNet por USB** (2026-11-25, `docs/mediciones/riesgos-h4-2026-11-25.md`); solo por SIO, pendiente de evaluación |
| R7 | 74LS07 posiblemente por debajo de 4,75 V [I] | Caída estimada en D4/D5 y Q2 (estimación, no medición) | Márgenes de entrada y salida reducidos | Medir VCC_BUF con carga | M1 [P]: diagnóstico inicial, sin el Atari y sin carga; bajo carga operativa, C9 (`docs/hardware/h4-c-casetera.md` §3). **No aceptado** por el responsable (2026-11-25, `docs/mediciones/riesgos-h4-2026-11-25.md`) |
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
| 13–15 | Atari (modelo, NTSC/PAL, ROM), otros dispositivos, instrumentos | **Parcial (2026-10-26):** Atari **65XE NTSC**, ampliado a **128 KB** de RAM; otros dispositivos SIO: disquetera **1050** y caseteras **XC12** y **1010**; **sin instrumentos** para M3a. Pendiente: versión de la ROM del sistema operativo y si alguna casetera, con una cinta, permite activar MOTOR para M3a |
| 16–17 | microSD FAT32 e imágenes ATR con derechos claros | **Completo** (H3c; imágenes sintéticas de `make_atr.py`) |

## 7. Checklist de aceptación de H4

H4 se aprueba **solo** cuando todos los puntos estén marcados y el equipo firme la revisión:

- [ ] **MOTOR CONTROL medido en el Atari solo, con la FujiNet desconectada, incluidos sus picos:** dentro de lo
      que admite un pin del ESP32, o mitigación aprobada y validada. **Sin esto no hay conexión para medir ni validar**
      (M3a). La excepción operativa D3 es una aceptación de riesgo y **no marca este punto**. Tampoco lo marca
      la aprobación de M3a por aceptación de riesgo del 2026-11-25.
- [ ] Continuidad de J1 a U3/U4 y a los GPIO de §2, y paso de J1 a J2, con la placa desenergizada (M0).
- [ ] R11, R2, R4/R5 y su relación con IO39, IO36 e IO35 comprobadas (M0).
- [ ] VCC, VCC_BUF y +3V3 medidos solo con USB, con S5 encendido y apagado (M1, M2).
- [ ] Aislamiento: con S5 apagado, VCC_BUF ≈ 0 V y las líneas del bus no son forzadas por la placa (M2, M3).
- [ ] Niveles en reposo de COMMAND, DATA IN y DATA OUT con el Atari encendido y el firmware sin configurar el SIO
      (M3, M4).
- [ ] IO36 en reposo con los dos equipos conectados, solo después de M3a (M4).
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
- `fujinet-hardware`, commit `0e539b8`: `FN32ROV-1.3-Q24_schematic.jpg`, `FN32ROV-1.3-Q24_BOM.ods` y
  `README.md` de la 1.3, y `FN32ROV-1.6/README.md` (recorrido de MOTOR y cambio de la 1.6). El archivo DipTrace
  `FN32ROV-1.3-Q24.dip` no se abrió. La BOM indica un ESP32-WROVER-E; la unidad del equipo tiene un WROVER-B.
- Proyecto WSS, informe G5-2 §4 y registro `g5-2-03-T5-zorro-cas.txt`: MOTOR activo durante 15 min 27 s.
- Esquemas del Atari 65XE (redibujo de Jerzy Sobola, 2003, 9 hojas) y de la casetera XC12 (mismo autor, 2002):
  MOTOR CONTROL (Q7) y conector SIO. Documentos de terceros, no oficiales de Atari.
- Atari, *1010 Cassette Recorder Field Service Manual* y *1050 Field Service Manual* (escaneos, solo referencia).

El detalle, con fechas y URL, está en `docs/fuentes.md`.
