# Evaluación del SN74LVC4245A para proteger GPIO36 frente a MOTOR (FujiNet FN32ROV-1.3-Q24)

> **INFORME TÉCNICO. PROPUESTA NO APROBADA.** No se fabricó, montó ni midió nada.
>
> - No aprueba M3a, H4 ni la condición C5 de H4-C. No cambia `docs/mediciones/puertas.json`.
> - T9 de `menu-cinta` sigue **bloqueado**. No se modifica firmware ni hardware.
> - Ninguna solución se implementa sin **revisión independiente** y aprobación del responsable.
> - Cada medición necesita **autorización explícita del responsable** antes de empezar (§8).
>
> Origen: un integrante de la comunidad Atari sugirió el 74LVC4245. Este informe comprueba si sirve y lo compara.
> Complementa la propuesta del PR #93, ya fusionada: [`proteccion-motor-gpio36.md`](proteccion-motor-gpio36.md)
> (https://github.com/calaluis/retrobridge-chile/pull/93). Ese documento verificó los esquemas 1.3 y 1.6.

## 0. Etiquetas

| Etiqueta | Significado |
|---|---|
| **[E]** | Leído en el esquema oficial de FujiNet |
| **[B]** | Leído en la BOM oficial |
| **[D]** | Hoja de datos de TI, en la revisión citada (§9) |
| **[E-Atari]** | Esquema de terceros del 65XE o de la XC12 (no versionado) |
| **[T]** | Cálculo propio |
| **[I]** | Inferencia: razonable, pero no leída ni medida |
| **[P]** | Pendiente: no verificado. No se deduce |
| **[R]** | Evidencia aportada por el revisor del PR #93, con fuente citada, que este informe **no pudo leer** |

## 1. Situación de partida

| Dato | Valor | Origen |
|---|---|---|
| MOTOR en la 1.3 | SIO pin 8 (`SIO_MCTL`) **directo** a U1 pin 4 (IO36); R2 10k a GND | [E] (PR #93 §2.2) |
| MOTOR en la 1.6 | Por **U3 SN74LS07DR, puerta 2**; R24 2K a GND (lado Atari); R2 10K a 3V3 (lado ESP32) | [E][B] (PR #93 §2.1) |
| Límite de GPIO36 | VDD + 0,3 V ≈ 3,6 V; solo entrada | [D] ESP32 |
| VIH / VIL del ESP32 | 0,75·VDD ≈ 2,48 V / 0,25·VDD ≈ 0,83 V | [D] ESP32. Confirmar en la WROVER [P] |
| MOTOR encendido | ≈ 4,7–4,9 V previstos; peor caso de cálculo 5,25 V | [E-Atari][T]. **No medido** [P] |
| MOTOR apagado | Q7 del Atari abierto: el pin queda **flotante** sin carga | [E-Atari] |
| Fuente del 65XE | Original, 5 V / 1 A. Pin 10 medido: 4,78 V en el banco T8 | Responsable; banco T8 |
| `VCC` de la FujiNet 1.3 | `USB_5V` o `SIO_5V` por diodos Schottky (D4, D5). Con S5 en OFF, el LDO queda deshabilitado, pero `VCC` **sigue vivo** desde el pin 10 del Atari | [E] |
| 3V3 de la FujiNet 1.3 | Salida del LDO U5. Con S5 en OFF: ≈ 0 V | [E][I] |

**Caso crítico:** Atari encendido y FujiNet apagada (S5 en OFF, o sin USB y sin S5). Hoy MOTOR alto entra por la
protección de IO36 hacia un 3V3 en 0 V. Solo lo limita Q7 del Atari [I]. Puede haber alimentación parásita [I].

## 2. SN74LVC4245A: lo que dice la hoja de datos [D]

Fuente: TI **SCAS375K**, marzo de 1994, **revisada en mayo de 2026** (§9).

| Punto | Dato [D] | Sección |
|---|---|---|
| Función | Transceptor de bus **octal** (8 canales), no inversor, 3 estados, con traducción de nivel | §1, §3 |
| Alimentación | **VCCA = 4,5–5,5 V** (puerto A, 5 V); **VCCB = 2,7–3,6 V** (puerto B, 3,3 V) | §5.4, §5.5 |
| Dirección | Pin **DIR**: H = A→B; L = B→A. **OE** en H = aislamiento. DIR y OE se alimentan de VCCA | Tabla 7-1, §3 |
| Entradas del puerto A (VCCA 4,5–5,5 V) | VIH mín. **2 V**; VIL máx. **0,8 V** (niveles TTL) | §5.4 |
| Salidas del puerto B | IOH hasta −24 mA con VCCB = 3 V | §5.5 |
| Máximo absoluto, puerto A | VI y VO: −0,5 V a **VCCA + 0,5 V** (limitado a 6 V) | §5.1, nota (2) |
| Máximo absoluto, puerto B | −0,5 V a VCCB + 0,5 V; VCCB máx. 4,6 V | §5.2 |
| **Ioff** | Puertos A y B: **±0,5 µA típ., ±2 µA máx.**, con VCCA = 0–5,5 V, **VCCB = 0 V**, VI o VO = 0–5,5 V | §5.7 |
| Aislamiento de VCC | Si VCCA **o** VCCB < 100 mV o flotando, **todas las E/S quedan en alta impedancia** | §7.5 |
| Secuencia de alimentación | Sin requisitos de orden; sin glitches. Conectar primero GND | §7.4, §8.3 |
| Entradas sin uso | **Deben** ir a VCC o GND (no flotantes) | §5.4 nota (1), §8.4 |
| Encapsulado | 24 pines (DB = SSOP, PW = TSSOP) | §4, §5.6 |

**Advertencia de revisión [D]:** el historial de cambios dice que la revisión K (mayo de 2026) **añadió** la
especificación Ioff, el aislamiento de VCC y la secuencia sin glitches. La revisión J (diciembre de 2022) no las
tenía. Si las piezas de existencias antiguas cumplen esas especificaciones **no consta** [P]. Hay que usar piezas
cubiertas por la revisión K, o confirmarlo con TI.

### 2.1 ¿Sirve para MOTOR 5 V → 3,3 V?

**Sí, funcionalmente** [D][T]:
- VCCA = 5 V, VCCB = 3V3 de la FujiNet, DIR = H (A→B), OE = L.
- A1 = MOTOR. B1 → IO36. MOTOR 4,7 V ≫ VIH 2 V. IO36 recibe ≈ VCCB, por encima de VIH 2,48 V.
- Con VCCB = 0 (FujiNet apagada): B1 en alta impedancia; A1 consume ≤ 2 µA [D].

**Pero no encaja bien en la 1.3** [D][T][I]:
1. **VCCA y el máximo absoluto.** El puerto A admite como máximo VCCA + 0,5 V.
   - Si VCCA sale de `VCC` de la FujiNet (≈ 4,6–4,7 V tras el Schottky [I]): con MOTOR = 5,25 V (peor caso) y
     VCCA = 4,5 V, el límite es 5,0 V y **se supera** [T]. Además, VCCA < 4,5 V queda fuera de lo recomendado.
   - Si VCCA sale de `SIO_5V` (pin 10, el mismo +5 V que alimenta Q7 del Atari), VCCA y MOTOR siguen el mismo riel
     y MOTOR ≤ VCCA en régimen [I]. Es la conexión correcta. Los picos de MOTOR siguen sin medir [P].
2. **Necesita el 3V3 de la propia FujiNet** como VCCB. Si VCCB viniera de un regulador del adaptador, alimentado por
   el pin 10, la salida B **conduciría 3,3 V hacia IO36 con la FujiNet apagada**: alimentación parásita de baja
   impedancia (hasta 24 mA de capacidad), **peor que hoy** [I]. El aislamiento de VCC solo protege si VCCB es el
   riel del ESP32.
3. **Siete canales sobrantes:** hay que fijar A2–A8 a GND o VCCA [D]. 24 pines con paso de 0,65 mm para una sola
   señal lenta.
4. **Dos alimentaciones, DIR y OE** que cablear. Más puntos de error.
5. **Ioff solo en la revisión K** (ver la advertencia).

## 3. Lo que usa realmente la FN32ROV-1.6 [E][B]

Ya verificado en el PR #93 §2.1 (esquema `FN32ROV-1.6_schematic.jpg`, commit `372bf92`, hoja única):

| | 1.6 (74LS07) | 74LVC4245A |
|---|---|---|
| Pieza | U3 SN74LS07DR, **puerta 2 de 6** (U3 ya estaba para COMMAND y PROC) | Chip nuevo de 8 canales |
| Tipo | Buffer TTL, **colector abierto**, no inversor | Transceptor CMOS, push-pull, 3 estados |
| Conexión | 2A = `SIO_MCTL`; 2Y = IO36 | A1 = MOTOR; B1 = IO36 |
| Dirección | Fija (buffer) | Por pin DIR = H |
| Alimentación | Una: `VCC_BUF` (5 V conmutado por Q2/Q3 solo si hay 3V3) | Dos: VCCA (5 V) y VCCB (3V3) |
| Nivel en IO36 | Lo fija R2 (10K a 3V3) | Lo fija VCCB |
| Lado Atari | R24 2K a GND | Necesita un pull-down aparte (la entrada CMOS no puede flotar) [D] |
| FujiNet apagada | `VCC_BUF` = 0. IO36 sin camino. Entrada LS con tensión > VCC, incluso VCC = 0: **tolerada según TI E2E**, respetando el máximo absoluto [R] (§3.1) | VCCB = 0 → alta impedancia; Ioff ≤ 2 µA [D] |

La 1.6 **no usa** un 74LVC4245. Usa una puerta libre de un 74LS07 que la placa ya tenía.

### 3.1 Entrada del SN74LS07 con VCC = 0 [R]

- **Evidencia:** según el revisor del PR #93, TI confirma en su foro técnico E2E que la entrada del SN74LS07 tolera
  una tensión mayor que VCC, **incluso con VCC = 0**, siempre que se respete el máximo absoluto. Fuente:
  https://e2e.ti.com/support/logic-group/logic/f/logic-forum/1637475/
- **Límite de esta evidencia:** el 2026-11-23 la página respondió **HTTP 403** (acceso denegado) a la lectura
  automática y al navegador integrado. Este informe **no leyó el hilo**: no cita texto, fecha ni autor. Queda como
  evidencia del revisor, a confirmar por quien pueda abrir la página [P].
- **Máximo absoluto de la entrada:** 7 V en la hoja de datos de TI (`h4-puerta-sio.md` §3.1) [D]. MOTOR previsto
  ≤ 5,25 V [T] queda por debajo. Los picos están sin medir [P] (M3a).
- **Qué cubre:** solo la **entrada** del 74LS07 sin alimentación. **No valida** el circuito completo de la 1.6 ni la
  modificación E del #93: faltan la fuga real de la entrada sin VCC, el estado de la salida 2Y con `VCC_BUF` = 0 y la
  medición en la placa [P].

## 4. Soluciones de un solo canal

### 4.1 SN74LVC1G17 (buffer Schmitt, push-pull) [D]

TI **SCES351Y**, revisada en octubre de 2025.
- VCC 1,65–5,5 V. **Entradas tolerantes a 5,5 V** con cualquier VCC (máximo absoluto 6,5 V) [D].
- **Ioff ±10 µA máx.** con VCC = 0 y VI o VO = 0–5,5 V [D].
- Con VCC = 3 V: VT+ = 1,48–1,92 V [D]. MOTOR 4,7 V ≫ 1,92 V. Histéresis frente al ruido.
- Salida push-pull a 3V3. La hoja especifica VOH ≥ VCC − 0,1 V **con IOH = −100 µA**, y VOH ≥ 2,4 V con −16 mA
  (VCC = 3 V) [D]. R2 (10 k a GND) pide ≈ 330 µA: **no hay fila para esa corriente**. Valor **esperado**
  ≈ 3,2–3,3 V [I], porque 330 µA está mucho más cerca de 100 µA que de 16 mA. La única cifra garantizada por
  encima de 100 µA es 2,4 V a −16 mA, que queda **por debajo** de VIH 2,48 V [D][T]: la hoja **no garantiza** el
  nivel con R2. Por eso V4 y V6 lo **miden bajo carga** (§8).
- SOT-23-5 o SC70-5. **Una** alimentación: el 3V3 de la FujiNet.
- No inversor. **R2 puede quedar a GND**: la salida la vence con 0,33 mA [T].

### 4.2 SN74LVC1G07 (buffer de drenador abierto) [D]

TI **SCES296AG**, revisada en octubre de 2025.
- Entradas y salida tolerantes a 5,5 V; **Ioff ±10 µA máx.** [D].
- Con VCC = 3–3,6 V: VIH 2 V, VIL 0,8 V [D].
- Drenador abierto: **necesita pull-up a 3V3**. En la 1.3, R2 va a GND: habría que moverla a 3V3, como en la 1.6.
  Es el equivalente CMOS del 74LS07 de la 1.6.

### 4.3 SN74LV1T34 (traductor de una alimentación) [D]

TI **SCLS743E**, revisada en febrero de 2024.
- Entrada 0–5,5 V; con VCC = 3–3,3 V, VIH ≈ 1,37–1,39 V [D].
- **La revisión E no especifica Ioff** [D]. Con la FujiNet apagada, su comportamiento no está garantizado [P].
  **Se descarta** frente al LVC1G17.

### 4.4 Divisor con clamp (pasivo)

Analizado en el PR #93 §4, alternativas A y B. Resumen [T]:
- Rs ≈ 6,8 kΩ 1 % en serie, con R2 de la placa: IO36 = 2,73–3,20 V en el peor caso. Ventana estrecha.
- Con la FujiNet apagada: **≈ 0,6 mA** hacia el 3V3 apagado [T]. No es cero.
- Un clamp zener a GND aporta poco y fuga corriente. Un clamp a 3V3 necesita un cable a la placa.
- No necesita alimentación y **no toca la placa**.

## 5. Matriz comparativa

Valores para MOTOR = 4,7–5,25 V.

| Criterio | 74LVC4245A | 74LVC1G17 | 74LVC1G07 + R2 a 3V3 | 74LS07 puerta 2 (como 1.6) | Divisor Rs 6,8 k (#93 A) |
|---|---|---|---|---|---|
| Canales usados / total | 1 / 8 | 1 / 1 | 1 / 1 | 1 / 6 (ya en la placa) | — |
| Alimentaciones | 2 (VCCA 5 V, VCCB 3V3) | 1 (3V3) | 1 (3V3) | 1 (`VCC_BUF`) | 0 |
| Pines de control | DIR, OE | — | — | — | — |
| Encapsulado | 24 pines, 0,65 mm | SOT-23-5 | SOT-23-5 | Ya montado (SOIC-14) | Resistencia |
| IO36 alto | ≈ VCCB [D] | ≈ 3,2–3,3 V esperado [I]; se mide bajo carga (V4) | 3,3 V (pull-up) [T] | 3,3 V (pull-up) [I] | 2,73–3,20 V [T] |
| Margen frente a VIH 2,48 V | ≈ 0,7 V | ≈ 0,7 V | ≈ 0,8 V | ≈ 0,8 V | **0,25 V** |
| Máx. en la entrada | VCCA + 0,5 V: **riesgo si VCCA < MOTOR** [T] | 6,5 V abs.; 5,5 V operativo [D] | 6,5 V abs.; 5,5 V operativo [D] | 7 V abs. [D] | Lo limita Rs |
| FujiNet apagada: corriente desde MOTOR | ≤ 2 µA (Ioff, rev. K) [D] | ≤ 10 µA [D] | ≤ 10 µA [D] | Entrada tolerada con VCC = 0 [R]; corriente sin cifra [P] | ≈ 0,6 mA [T] |
| FujiNet apagada: ¿algo alimenta IO36? | No, si VCCB = 3V3 de la placa [D] | No [D] | No [D] | No [I] | Sí, ≈ 0,6 mA [T] |
| Carga sobre MOTOR | ≈ 0,49 mA (pull-down 10 k) | ≈ 0,45 mA (§6.1) | ≈ 0,49 mA (pull-down 10 k) | ≈ 2,4 mA (R24 2K) | ≈ 0,29 mA |
| ¿Toca la placa? | Sí (3V3, y cortar pista o adaptador con cable) | Mínimo (un cable a 3V3) | Sí (mover R2) | Sí (cortar pista, levantar pin, mover R2) | No |
| Reversible | Parcial | Casi total (variante adaptador) | Parcial | No | Sí |
| Complejidad | **Alta** | **Baja** | Media | Alta (retrabajo) | Muy baja |
| Coste de piezas | Medio | Muy bajo | Muy bajo | Nulo | Muy bajo |

## 6. Propuesta de implementación

Sin modificar el Atari. Dos variantes, a decidir por el responsable tras M3a.

### 6.1 Recomendada: adaptador SIO con SN74LVC1G17 y un cable a 3V3

**Idea:** el buffer va en un adaptador SIO en línea (Atari → adaptador → J1 de la FujiNet). Solo necesita un cable
al 3V3 de la FujiNet. **No se corta ninguna pista.**

| Elemento | Conexión |
|---|---|
| U_A, SN74LVC1G17 | VCC = 3V3 de la FujiNet (cable a un punto de 3V3; punto exacto por decidir [P]); GND = SIO pin 4/6 |
| Entrada A | Pin 8 del lado Atari, por **Rin = 1 kΩ** en serie (limita la corriente de picos) [I] |
| Pull-down de entrada | **10 kΩ** a GND en el lado Atari (MOTOR flota cuando está apagado) |
| Salida Y | Pin 8 del lado FujiNet → red `SIO_MCTL` → IO36 |
| Desacoplo | 100 nF entre VCC y GND, junto a U_A |
| Resto de pines | 1:1 |

Cálculos [T]:
- MOTOR 4,7 V: entrada ≈ 4,7 · 10 / 11 ≈ 4,27 V ≫ VT+ 1,92 V. Con 5,25 V: 4,77 V ≤ 5,5 V operativo.
- MOTOR apagado: entrada 0 V → salida ≈ 0 V.
- Carga sobre MOTOR: 4,9 / 11 k ≈ **0,45 mA**.
- Salida alta: **esperado** VOH ≈ 3,2–3,3 V con R2 (10 k a GND, ≈ 330 µA) [I]. La hoja no lo garantiza a esa
  corriente (§4.1). Se verifica bajo carga en V4 y V6.
- **FujiNet apagada, Atari encendido:** VCC de U_A = 0 → fuga del integrado ≤ 10 µA en la entrada y en la salida
  (Ioff) [D]. MOTOR sigue cargado por Rin + pull-down: ≈ 0,45 mA [T], que van a GND, no al ESP32. IO36 queda en 0 V
  por R2. **Sin alimentación parásita** por este camino [I].
- **Atari apagado, FujiNet encendida:** el pull-down deja la salida en bajo.
- Picos: Rin limita la corriente; el valor real depende de M3a [P]. Si M3a muestra picos > 5,5 V, añadir un clamp
  en la entrada (por ejemplo, un zener de 5,1 V a GND) [I].

Limitaciones [I]:
- **J2 recibe el MOTOR traducido (3,3 V).** **Con el adaptador puesto, no se conecta en J2 ningún periférico que
  use o cargue MOTOR** (por ejemplo, una XC12, que se alimenta del pin 8 [E-Atari]). La salida de 3,3 V del
  SN74LVC1G17 **no reemplaza** la alimentación de MOTOR original: no está pensada para dar corriente a un periférico
  [I]. RetroBridge emula la casetera, así que no hace falta una casetera detrás.
- El cable a 3V3 es una intervención pequeña y reversible en la placa.
- Sin el cable a 3V3, la salida queda sin alimentar e IO36 en 0 V por R2: falla de forma segura.

### 6.2 Alternativa: modificación interna con SN74LVC1G17

Igual que 6.1, pero dentro de la FujiNet: cortar la pista `SIO_MCTL`→IO36 y montar el 1G17 en el corte, alimentado
por el 3V3 local. J2 conserva el MOTOR de 5 V. La ubicación de la pista y de R2 en el PCB **no se revisó** [P].
Riesgo y reversibilidad peores que 6.1.

### 6.3 Por qué no el 74LVC4245A

Funciona, pero para **una** señal lenta añade: dos alimentaciones; riesgo de superar el máximo absoluto si
VCCA < MOTOR; DIR y OE; siete entradas que fijar; 24 pines de paso fino; e Ioff garantizado solo desde la
revisión K. El 74LVC1G17 da la misma protección con la placa apagada (Ioff), con una alimentación y cinco pines.

## 7. Riesgos

| Id | Riesgo | Afecta | Control |
|---|---|---|---|
| L1 | MOTOR real o sus picos > 5,5 V | 1G17, 1G07, 4245A | M3a; Rin y clamp opcional |
| L2 | VCCA < MOTOR → máximo absoluto superado | 4245A | VCCA desde `SIO_5V`; mejor, no usar 4245A |
| L3 | Buffer alimentado por un regulador propio → alimentación parásita | 4245A, cualquier buffer autoalimentado | El VCC del buffer debe ser el 3V3 del ESP32 |
| L4 | Piezas antiguas sin Ioff garantizado | 4245A | Revisión K o confirmación de TI |
| L5 | Entrada CMOS flotante con MOTOR apagado | 4245A, 1G17, 1G07 | Pull-down de 10 k |
| L6 | Cable a 3V3 mal soldado o en otro riel | 6.1, 6.2 | Etapa V3 (sin energía) |
| L7 | J2 recibe 3,3 V en MOTOR; un periférico que use o cargue MOTOR quedaría mal alimentado y cargaría la salida del 1G17 | 6.1 | **Prohibido** conectar en J2 periféricos que usen o carguen MOTOR mientras el adaptador esté puesto; rotular el adaptador |
| L8 | Errores de montaje (pin 8 cruzado, puentes) | Todas | Etapas V2–V5 |
| L9 | Umbrales del ESP32 sin confirmar en la WROVER | Todas | Revisar la hoja de datos [P] |
| L10 | Tolerancia del 74LS07 sin VCC apoyada en un hilo de foro no leído aquí | 74LS07 (1.6, modificación E) | Confirmar el hilo de TI E2E; medir la fuga si se elige esa vía |

## 8. Validación por etapas

> Cada etapa requiere **autorización explícita del responsable** antes de empezar, registrada con fecha, etapa y
> quién autoriza. Una autorización vale para una sola etapa. Ninguna etapa aprueba M3a, H4 ni C5. Si algo no
> coincide: **parar**. No se flashea, no se abre ningún puerto serie y no se cambia el firmware.

| Etapa | Qué | Condición previa | Criterio para seguir |
|---|---|---|---|
| V0 | Revisión independiente y aprobación del diseño | — | Aprobación escrita, en un PR aparte |
| V1 | M3a en el Atari solo (`ficha-medicion-m3a-motor.md`) | V0 | Nivel y picos registrados; recalcular Rin y clamp |
| V2 | Adaptador **sin energía**: continuidad 1:1, Rin, pull-down, sin cortos | V1 | Valores dentro de tolerancia |
| V3 | Cable a 3V3 **sin energía**: continuidad al riel 3V3; sin continuidad a `VCC`, `SIO_5V` ni GND | V2 | Correcto |
| V4 | Adaptador en banco, sin Atari ni FujiNet: 3,3 V limitados a 20 mA como VCC; entrada con fuente limitada a 1 mA (0 / 4,7 / 5,25 V); 10 kΩ a GND en la salida | V3 | **Prueba bajo carga:** con los 10 kΩ a GND en la salida (≈ 330 µA, como R2), salida alta ≥ 3,1 V y baja ≤ 0,1 V; corriente de VCC < 1 mA |
| V5a | Banco con **VCC = 0 V** (VCC unido a GND) y 5,25 V en la entrada del adaptador (antes de Rin), con el pull-down de 10 kΩ montado | V4 | **Corriente total** ≈ 5,25 / 11 kΩ ≈ **477 µA** (454–502 µA con resistencias al 5 %) [T]. Casi toda va por el pull-down: **no es la fuga del integrado**. Salida con 10 kΩ a GND ≤ 0,1 V (≈ 10 µA o menos por la salida) |
| V5b | **Fuga del integrado:** igual que V5a, pero con el pull-down **desconectado** (antes de montarlo, o con un puente retirable) | V5a | Corriente por Rin ≤ **10 µA** (límite Ioff [D]); equivale a una caída en Rin de 1 kΩ ≤ 10 mV. Según la resolución del instrumento |
| V6 | Atari solo + adaptador alimentado desde el banco a 3,3 V; 10 kΩ en la salida; `POKE 54018,52` / `,60` | V5 | Salida alta 3,1–3,3 V; pico ≤ 3,6 V; baja ≤ 0,3 V |
| V7 | Atari encendido, FujiNet con S5 en OFF, adaptador puesto | V6 + orden de conexión de `procedimiento-medicion-pasiva-sio.md` §2 | IO36 ≈ 0 V; el 3V3 de la FujiNet no sube |
| V8 | Atari y FujiNet encendidos; firmware actual **sin configurar GPIO36** | V7 | IO36 alto 3,1–3,3 V, bajo ≤ 0,3 V, picos ≤ 3,6 V |
| V9 | Lectura de GPIO36 por firmware (T9) | **Fuera de este informe.** M3a y H4 aprobadas, PR aparte | — |

## 9. Fuentes

Hojas de datos de Texas Instruments, consultadas el 2026-11-23 (no se suben al repositorio; SHA-256 del PDF):
- **SN74LVC4245A**, SCAS375K (marzo de 1994, revisada en mayo de 2026) —
  https://www.ti.com/lit/ds/symlink/sn74lvc4245a.pdf — SHA-256 `29a63cd1…f2b0`. Secciones 1, 3, 4, 5.1, 5.2,
  5.4, 5.5, 5.7, 7.4, 7.5, tabla 7-1, 8.3, 8.4 y 10 (historial de revisiones).
- **SN74LVC1G17**, SCES351Y (revisada en octubre de 2025) — https://www.ti.com/lit/ds/symlink/sn74lvc1g17.pdf —
  SHA-256 `ade19853…050f`. Características eléctricas (VT+, Ioff) y condiciones recomendadas.
- **SN74LVC1G07**, SCES296AG (revisada en octubre de 2025) — https://www.ti.com/lit/ds/symlink/sn74lvc1g07.pdf —
  SHA-256 `0471dcdd…e9c4`. VIH, VIL, Ioff.
- **SN74LV1T34**, SCLS743E (revisada en febrero de 2024) — https://www.ti.com/lit/ds/symlink/sn74lv1t34.pdf —
  SHA-256 `04b2e1f1…0d52`. VIH; sin Ioff.

Foro técnico de TI (evidencia del revisor del PR #93, no leída aquí: HTTP 403 el 2026-11-23):
- TI E2E, Logic forum, hilo 1637475 — https://e2e.ti.com/support/logic-group/logic/f/logic-forum/1637475/ —
  entrada del SN74LS07 por encima de VCC, incluso con VCC = 0 (§3.1).

Esquemas:
- FujiNet FN32ROV-1.6 y FN32ROV-1.3-Q24, repositorio oficial `FujiNetWIFI/fujinet-hardware`, commit
  `372bf92236b883c27c0c5928a77adbe5e6e81b66` —
  https://github.com/FujiNetWIFI/fujinet-hardware/tree/372bf92236b883c27c0c5928a77adbe5e6e81b66/ATARI/Old-Versions.
  Lectura detallada y SHA-256 en [`proteccion-motor-gpio36.md`](proteccion-motor-gpio36.md) §2 y §9 (PR #93).
- Esquemas de terceros del 65XE (hojas 5 y 9) y de la XC12 (`docs/fuentes.md`, 2026-10-26 y 2026-11-23).

Documentos del proyecto: `h4-puerta-sio.md` §3.1 y §3.4, `ficha-medicion-m3a-motor.md`,
`procedimiento-medicion-pasiva-sio.md`, `h4-c-casetera.md`, `mitigacion-c-motor-pin8.md`.

No se leyó ni copió código de FujiNet ni de terceros (ADR-012). Los circuitos de §6 son aplicaciones genéricas de
las hojas de datos.
