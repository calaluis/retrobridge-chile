# Protección de GPIO36 frente a MOTOR (pin 8 del SIO) en la FujiNet FN32ROV-1.3-Q24

> **PROPUESTA NO APROBADA. Solo documento.** No se fabricó, no se montó ni se midió nada.
>
> - Este documento **no aprueba M3a ni H4**, ni la condición C5 de H4-C. No cambia `docs/mediciones/puertas.json`.
> - T9 de `menu-cinta` sigue **bloqueado**. `kTapeOutput` sigue deshabilitado.
> - El firmware no lee GPIO36 ni conduce DATA IN por este documento.
> - La compilación y las pruebas de host **no son validación eléctrica**.
> - Cada medición de §8 necesita **autorización explícita del responsable**, antes de empezarla.
>
> Documentos relacionados: `ficha-medicion-m3a-motor.md` (medición de MOTOR en el Atari solo),
> `procedimiento-medicion-pasiva-sio.md` (etapas M0–M6), `h4-puerta-sio.md` §3.4 (recorrido de MOTOR en la 1.3),
> `h4-c-casetera.md` (C5) y `mitigacion-c-motor-pin8.md` (adaptador sin pin 8).

## 0. Etiquetas

| Etiqueta | Significado |
|---|---|
| **[E]** | Leído en el esquema oficial de FujiNet (§9), en esta revisión |
| **[B]** | Leído en la BOM oficial de la misma carpeta |
| **[E-Atari]** | Esquema de terceros del 65XE o de la XC12 (no versionado) |
| **[D]** | Hoja de datos del fabricante |
| **[T]** | Cálculo propio a partir de valores [E], [B] o [D] |
| **[I]** | Inferencia: razonable, pero no leída ni medida |
| **[P]** | Pendiente: no verificado. No se deduce |

## 1. Problema

- En la FN32ROV-1.3-Q24, MOTOR (SIO pin 8, red `SIO_MCTL`) llega **directo** a U1 pin 4 (GPIO36). Solo hay R2,
  10 kΩ, a GND [E] (`h4-puerta-sio.md` §3.4; confirmado otra vez en §2.2).
- GPIO36 es solo entrada. Máximo **VDD + 0,3 V ≈ 3,6 V** [D].
- El 65XE da MOTOR con Q7 (PNP, emisor en +5 V), **sin resistencia en serie** [E-Atari]. Previsión: **4,7–4,9 V**
  con el motor encendido [T]. **Medido el 2026-11-25: 4,89 V** en el Atari solo, sin carga (B4 de M3a, «Detener»;
  `docs/mediciones/m3a-motor-2026-11-25.md`). Picos sin medir [P].
- Con la FujiNet conectada, en su conector posterior y sin USB: **4,87 V con S5 encendido** y **3,16 V con S5
  apagado** [H]. La caída con S5 apagado no tiene causa verificada (hipótesis HS1–HS4 del registro) [P].
- Para T9 de `menu-cinta` hay que **leer** MOTOR. Quitar el pin 8 (mitigación C) protege, pero impide T9.

## 2. Comparación de esquemas: FN32ROV-1.3-Q24 frente a FN32ROV-1.6

### 2.1 FN32ROV-1.6 [E][B]

Hoja única del esquema (§9). El dibujo **no tiene cuadro de revisión**; la revisión es la carpeta `FN32ROV-1.6` en el
commit citado.

| Pieza | Valor | Conexiones leídas |
|---|---|---|
| **U3** | SN74LS07DR [E][B] | Puerta 2: **pin 3 (2A) = `SIO_MCTL`**; **pin 4 (2Y) = `IO36/MCTRL`**. Pin 14 (VCC) = `VCC_BUF`; pin 7 = GND |
| **R24** | 2K [E][B] (RES_0402) | Pin 1 = `SIO_MCTL`; pin 2 = GND. Pull-down del lado Atari |
| **R2** | 10K [E][B] (RES_0402) | Pin 2 = 3V3; pin 1 = `IO36/MCTRL`. **Pull-up** del lado ESP32 |
| U1 pin 4 | SENSOR_VP/IO36/ADC1_CH0 | Red `IO36/MCTRL` [E] |
| J1 pin 8 «MCTL» y J3 pin 8 | — | Red `SIO_MCTL` [E] |
| **Q2** | DMP2035U-7 (P-MOSFET) [E][B] | Fuente en `VCC`; drenador = `VCC_BUF` [E] |
| **Q3** | DMG2302U (N-MOSFET) [E][B] | Puerta desde +3V3 por **R21 1k**; drenador a la puerta de Q2; **R20 10K** entre puerta de Q2 y `VCC` [E][B] |
| C6 | 0.1uF 6.3V | `VCC_BUF` a GND, junto a U3 [E] |

El README de la 1.6 dice lo mismo: «Run Motor Control signal through buffer», 10K pull-up del lado ESP y 2K
pull-down del lado Atari [E]. Ahora el esquema lo confirma.

Lectura funcional [I] (deducida de las conexiones [E] y del 74LS07 [D]):
- El 74LS07 es **no inversor** y de **colector abierto**. MOTOR alto → 2Y abierto → R2 lleva IO36 a **3,3 V**.
  MOTOR bajo → 2Y conduce → IO36 ≈ VOL. **La polaridad para el firmware no cambia** respecto de la 1.3.
- Ningún nivel de 5 V llega a IO36. El nivel alto de IO36 lo fija el propio 3V3 de la placa.
- `VCC_BUF` solo existe con +3V3 presente (Q3 → Q2). Con la placa apagada, U3 queda sin alimentación y R2 va a un
  3V3 en 0 V: IO36 queda en bajo y **aislado de MOTOR** por U3.
- R24 fija el reposo de MOTOR en bajo, aunque el Atari deje el pin flotando.

Pendiente en la 1.6 [P]: tolerancia de R2 y R24 (la BOM no la dice); comportamiento de la entrada del 74LS07 con
VCC = 0 y 4,8 V en la entrada (la hoja de datos no lo especifica en lo revisado).

### 2.2 FN32ROV-1.3-Q24 [E][B]

Mismo archivo que en la auditoría del 2026-10-26: el SHA-256 coincide (§9).

| Pieza | Valor | Conexiones leídas |
|---|---|---|
| R2 | 10k [E][B] | Pin 1 = `SIO_MCTL` / U1 pin 4; pin 2 = **GND**. Pull-down |
| U1 pin 4 | IO36 | **Directo** a `SIO_MCTL` [E] |
| **U3 puerta 2** | SN74LS07DR | **Libre:** pin 3 (2A) a **GND**; pin 4 (2Y) **sin conexión** [E] |
| Q2 / Q3 / R14 / R16 | DMP2035U-7 / DMG2302U / 10K / 1k | Mismo conmutador de `VCC_BUF` que en la 1.6 [E] |

### 2.3 Diferencias de MOTOR

| Punto | 1.3 | 1.6 |
|---|---|---|
| Camino a IO36 | Directo | Por U3, puerta 2 (74LS07) |
| Resistencia del lado ESP32 | R2 10k a **GND** | R2 10K a **3V3** |
| Resistencia del lado Atari | Ninguna aparte de R2 | R24 2K a GND |
| Nivel máximo en IO36 | El de MOTOR (≈ 4,7–4,9 V previstos) [T] | 3,3 V (pull-up) [I] |
| FujiNet apagada, MOTOR alto | Corriente por la protección de IO36 hacia un 3V3 apagado [I] | U3 sin alimentar; IO36 sin camino desde MOTOR [I] |
| Carga sobre MOTOR del Atari | 10 kΩ → ≈ 0,48 mA a 4,8 V [T] | 2 kΩ + entrada LS → ≈ 2,4 mA a 4,8 V [T] |

**Hallazgo útil:** la 1.3 ya tiene la puerta que usa la 1.6 (U3, puerta 2), libre [E]. Por eso la modificación de
placa (alternativa E) es posible sin añadir un chip. Dónde va la pista `SIO_MCTL`→IO36 en el PCB **no se revisó**
(no se abrieron el `.dip` ni los Gerbers) [P].

## 3. Datos de partida

| Dato | Valor | Origen |
|---|---|---|
| Límite de entrada de GPIO36 | VDD + 0,3 V ≈ 3,6 V | [D] ESP32 |
| VIH mín. / VIL máx. del ESP32 | 0,75·VDD ≈ 2,48 V / 0,25·VDD ≈ 0,83 V | [D] ESP32, tabla de características DC. **Confirmar en la hoja de datos de la WROVER** [P] |
| Corriente admisible por la protección de un pin | **No especificada** en lo revisado | [P]. No se supone ningún valor |
| 74LS07: VIH / VIL / IIH / IIL / VOL | 2,0 V / 0,8 V / 20 µA / −0,2 mA / 0,4 V a 16 mA | [D] (`h4-puerta-sio.md` §3.1) |
| 74LS07: VCC recomendado | 4,75–5,25 V; `VCC_BUF` puede quedar por debajo (riesgo R7) | [D] [P] |
| MOTOR encendido | ≈ 4,7–4,9 V; peor caso de cálculo 5,25 V (+5 V del Atari al +5 %) | [T] [P] |
| MOTOR apagado | Q7 abierto: flotante sin carga; lo fija la carga del periférico | [E-Atari] |
| Excitación de Q7 | CA2 bajo → R91 2k7 → base. Ib ≈ (5 − 0,7 − 0,4) / 2,7 k ≈ 1,4 mA | [E-Atari] [T]. Modelo y β de Q7 no legibles [P] |
| Picos al encender o apagar | Desconocidos | [P] M3a (B2, B5) |

Sobre la carga [I]: con Ib ≈ 1,4 mA, cualquier β ≥ 10 saturaría Q7 con cargas de unos 10 mA. Las cargas de §4 van
de 0,3 a 3,6 mA. No se espera que ninguna baje MOTOR de forma apreciable, pero el modelo de Q7 no se conoce [P].

## 4. Alternativas

Todas se evalúan con MOTOR = 4,7 V (bajo), 4,9 V (típico previsto) y 5,25 V (peor caso alto). Los valores se
recalculan con el resultado real de M3a.

### A. Resistencia en serie externa (divisor con la R2 de la placa)

**Circuito:** un adaptador SIO en línea (Atari → adaptador → J1 de la FujiNet) con **Rs = 6,8 kΩ, 1 %** en el pin 8.
Los otros 12 contactos pasan directos. Con R2 (10 k a GND) forma un divisor. **No se toca la placa.** Es el mismo
adaptador de `mitigacion-c-motor-pin8.md`, con una resistencia en lugar del pin abierto.

Tensión en IO36, V = MOTOR · R2 / (R2 + Rs) [T]:

| MOTOR | R2 = 10 k, Rs = 6,8 k | Peor caso bajo (R2 9,5 k, Rs +1 %) | Peor caso alto (R2 10,5 k, Rs −1 %) |
|---|---|---|---|
| 4,7 V | 2,80 V | **2,73 V** | — |
| 4,9 V | 2,92 V | 2,84 V | 2,99 V |
| 5,25 V | 3,13 V | — | **3,20 V** |

- Margen alto: 2,73 V frente a VIH 2,48 V → **0,25 V** [T]. Justo, pero suficiente para una señal lenta.
- Margen al límite: 3,20 V frente a 3,6 V → 0,4 V. Además queda bajo VDD: la protección del pin **no conduce** con
  la placa encendida [T].
- R2 se supuso ±5 %: la BOM no da la tolerancia [P]. M0 mide R2.
- **Ventana estrecha:** Rs = 4,7 k da 3,64 V en el peor caso alto (excede). Rs = 10 k da 2,35 V con 4,7 V (bajo
  VIH). Por eso Rs depende del valor real de M3a y de R2 medida.
- Motor apagado: R2 lleva IO36 a 0 V. Bien.
- **Carga sobre MOTOR:** 4,9 V / 16,8 k ≈ **0,29 mA**. Menor que hoy (0,48 mA) y que la 1.6 (2,4 mA) [T].
- **Picos:** un pico de 10 V daría 5,95 V sin la protección; la protección del pin conduciría
  (10 − 3,6) / 6,8 k ≈ **0,94 mA** como máximo, limitado por Rs [T]. Opcional: 1 nF a GND en el lado FujiNet
  (τ ≈ 4 µs) para filtrar picos rápidos. Se decide con los picos de M3a [P].
- **FujiNet apagada (S5 en OFF) y Atari encendido:** el LDO queda deshabilitado y el 3V3 en ≈ 0 V, pero `VCC` sigue
  vivo por D5/SIO_5V [E]. La protección de IO36 conduce hacia el 3V3 apagado [I]:
  I ≈ (4,9 − 0,5) / 6,8 k − 0,5 / 10 k ≈ **0,6 mA** [T] (Vf de 0,5 V supuesto [I]). Hoy, sin Rs, esa corriente
  solo la limita Q7 [I]. Si 0,6 mA levanta el riel de 3V3 lo suficiente para alimentar a medias el ESP32 **no se
  sabe** [P]: se mide en la etapa V6. Se espera poco, porque el riel tiene carga [I].
- **Paso hacia J2:** en la 1.3, J1 pin 8 y J2 pin 8 son la misma red [E]. Un equipo conectado en J2 vería ≈ 2,9 V
  en MOTOR. Una XC12 real en J2 se alimenta del pin 8 [E-Atari] y probablemente no funcionaría [I]. RetroBridge
  emula la casetera, así que no hace falta una XC12 detrás; queda como limitación documentada.
- **Riesgos:** ventana de diseño estrecha; depende de R2; la corriente con la FujiNet apagada no es cero; contacto
  añadido en el pin 8; el adaptador puede quedar mal montado o al revés.
- **Coste:** una resistencia 1 % + un adaptador SIO macho-hembra (el mismo que C). Bajo. **Reversible.**

### B. Serie + clamp externo

**Circuito:** Rs en serie (como A) y un limitador en el lado FujiNet del adaptador.
- **B1, zener a GND** (por ejemplo, 3,0–3,3 V): el adaptador no tiene 3V3, así que el clamp va a GND. Un zener de
  baja tensión tiene un codo blando: a 2,8–3,2 V ya fuga corriente apreciable y **baja el nivel alto**, justo donde
  el margen frente a VIH es menor [I]. Solo aporta frente a picos que A ya limita a ≈ 1 mA [T].
- **B2, Schottky a 3V3** (por ejemplo, doble diodo a GND y a 3V3): fija el máximo en 3V3 + 0,3 V. **Necesita un
  cable al 3V3 de la placa:** ya es una modificación de placa. Con la placa apagada, el diodo a 3V3 lleva la misma
  corriente que en A [I].
- **Carga sobre MOTOR:** como A, más la fuga del clamp.
- **Coste:** A + 1 componente. **Ventaja sobre A: poca**, salvo que M3a muestre picos grandes. Riesgo: fuga del
  zener no caracterizada [P].

### C. Buffer o comparador externo

- **C1, buffer de colector o drenador abierto** (como la 1.6, o un 74LVC1G07): la salida necesita pull-up a 3V3. En
  la 1.3, R2 va a **GND**. Hace falta mover R2 a 3V3 o añadir un cable a 3V3: **modificación de placa**.
- **C2, buffer alimentado desde el pin 10 del SIO** con su propio regulador de 3,3 V: con la FujiNet apagada y el
  Atari encendido, el buffer **alimenta IO36 a 3,3 V con baja impedancia** → peor alimentación parásita que A [I].
  Se descarta salvo con resistencia en serie, y entonces equivale a A con más piezas.
- **C3, comparador o transistor inversor:** cambia la polaridad (cambio de firmware) y también necesita pull-up a
  3V3.
- **Carga sobre MOTOR:** IIH del buffer (µA) más el pull-down que se ponga en el lado Atari (como R24, 2,4 mA).
- **Coste:** medio. Necesita alimentación. **En la 1.3, cualquier buffer correcto exige tocar la placa.**

### D. Optoacoplador

- **Entrada:** MOTOR → 1 kΩ → LED → GND. I ≈ (4,8 − 1,2) / 1 k ≈ **3,6 mA** de carga sobre MOTOR [T] (la mayor
  de todas; Vf de 1,2 V supuesto [I]).
- **Salida:** fototransistor en seguidor de emisor, colector a 3V3 y **R2 (10 k a GND) como carga de emisor**.
  Necesita ≈ 0,33 mA; con una CTR ≥ 50 % habría unos 1,8 mA disponibles [T] (CTR depende del modelo [P]).
  No inversor. IO36 ≤ 3V3.
- **FujiNet apagada:** el colector está en 0 V → sin camino hacia IO36 [I]. Es la mejor en este punto.
- **Contras:** necesita un cable al 3V3 de la placa (**modificación**); la CTR envejece; el aislamiento galvánico no
  aporta, porque GND ya es común por el SIO.
- **Coste:** medio. Velocidad irrelevante para MOTOR.

### E. Modificar la placa 1.3 como la 1.6

Usa la puerta 2 libre de U3 [E].
1. Cortar la pista entre `SIO_MCTL` e IO36 (ubicación en el PCB **no revisada** [P]).
2. Levantar U3 pin 3 (2A) de GND y unirlo a `SIO_MCTL`.
3. Unir U3 pin 4 (2Y) a IO36.
4. Pasar R2 de GND a 3V3 (pull-up).
5. Añadir 2 kΩ de `SIO_MCTL` a GND (como R24).

- **Niveles:** los de la 1.6 [I]. MOTOR bajo con 2 k: IIL × 2 k = 0,2 mA × 2 k = **0,4 V** < VIL 0,8 V [T].
  MOTOR alto 4,7 V ≫ VIH 2,0 V. IO36 bajo ≈ VOL; R2 pide 0,33 mA [T].
- **FujiNet apagada:** U3 sin alimentar; IO36 aislado [I]. Entrada del LS07 sin VCC con 4,8 V: [P].
- **Carga sobre MOTOR:** ≈ 2,4 mA [T]. J2 sigue viendo MOTOR completo (la red `SIO_MCTL` no cambia) [I].
- **Riesgos:** retrabajo en SOIC y 0402; cortar una pista sin ver el PCB; `VCC_BUF` posiblemente bajo 4,75 V (R7);
  es **difícil de revertir**; daño a la única placa.
- **Coste:** piezas casi nulas; habilidad y riesgo altos.

### F. Otras

- **Placa FN32ROV-1.6 o posterior:** ya trae el buffer [E]. Coste: otra placa. La 1.7.1 existe en el repositorio,
  **no se revisó** [P].
- **Mitigación C (sin pin 8):** máxima seguridad, pero **impide T9** (`h4-puerta-sio.md` §3.4).

### Resumen

| Alt. | Toca la placa | IO36 máx. (5,25 V) | Carga MOTOR | FujiNet apagada | J2 ve MOTOR | Reversible | Coste |
|---|---|---|---|---|---|---|---|
| A | No | 3,20 V [T] | 0,29 mA | ≈ 0,6 mA a 3V3 [T][P] | ≈ 2,9 V | Sí | Bajo |
| B1 | No | < 3,20 V | 0,3 mA + fuga | Similar a A | Reducido | Sí | Bajo |
| B2 / C1 / D | Sí (cable a 3V3) | ≤ 3,6 V / 3,3 V | Variable / 3,6 mA (D) | B2 ≈ A; C1, D ≈ 0 [I] | Completo (C1, D) | Parcial | Medio |
| E | Sí (retrabajo) | 3,3 V [I] | 2,4 mA | ≈ 0 [I] | Completo | No | Bajo en piezas, alto en riesgo |
| F (1.6) | No (otra placa) | 3,3 V [I] | 2,4 mA | ≈ 0 [I] | Completo | — | Otra placa |

## 5. Propuesta

1. **Primero M3a** (ficha existente). Sin el nivel y los picos reales no se fija Rs.
2. **Diseño propuesto: alternativa A** (adaptador con Rs ≈ 6,8 kΩ 1 % en el pin 8, sin tocar la placa). Razones:
   reversible, barata, menor carga sobre MOTOR, limita la corriente de picos y la de la FujiNet apagada.
   Rs se recalcula con M3a y con R2 medida en M0.
3. **Alternativa robusta, a decidir aparte: E o F** (equivalentes a la 1.6). Eliminan la corriente con la placa
   apagada. E es irreversible; F cuesta una placa.
4. Si M3a muestra picos grandes: añadir 1 nF en el lado FujiNet o pasar a E/F.

**La elección es del responsable.** Ninguna alternativa queda aprobada por este documento.

## 6. Riesgos

| Id | Riesgo | Alternativas | Control |
|---|---|---|---|
| P1 | MOTOR real fuera de 4,7–5,25 V, o con picos | Todas | M3a antes de fijar Rs |
| P2 | R2 fuera de ±5 % | A, B | M0: medir R2 |
| P3 | Margen VIH de solo 0,25 V | A | Recalcular con M3a; V4 y V7 |
| P4 | Corriente por la protección con la FujiNet apagada; alimentación parásita. **Indicio del 2026-11-25:** con S5 apagado MOTOR cae a 3,16 V; causa sin verificar (HS1–HS4) | A, B | Etapa V6; la protección debe limitar esa corriente con la placa apagada, sea cual sea la causa |
| P5 | XC12 en J2 sin MOTOR completo | A, B | Limitación documentada; no usar XC12 detrás |
| P6 | Adaptador mal montado (Rs en otro pin, puentes) | A, B | V2 con multímetro, sin energía |
| P7 | Daño a la placa en el retrabajo | E | Decisión aparte; quien tenga práctica en SOIC/0402 |
| P8 | `VCC_BUF` < 4,75 V (R7) | E, F | M1 |
| P9 | Entrada del 74LS07 con VCC = 0 y 4,8 V en la entrada | E, F | Revisar la hoja de datos [P] |

## 7. Verificaciones pendientes

- [ ] M3a: nivel estable y picos de MOTOR en el Atari solo, con y sin 10 kΩ (ficha existente). B4 = 4,89 V ya
      medido; picos, A, C, D y E pendientes.
- [ ] Causa de la caída de MOTOR a 3,16 V con S5 apagado (HS1–HS4 del registro del 2026-11-25). Solo con un
      procedimiento seguro aprobado si exige conectar MOTOR a la placa.
- [ ] M0: R2 real; continuidad `SIO_MCTL`–IO36.
- [ ] VIH/VIL de GPIO36 en la hoja de datos de la ESP32-WROVER del equipo.
- [ ] Corriente admisible por la protección de los pines del ESP32 (si Espressif la da).
- [ ] Ubicación de la pista `SIO_MCTL`→IO36 en el PCB de la 1.3 (solo si se elige E).
- [ ] Comportamiento del 74LS07 sin alimentación (solo E/F).
- [ ] Tolerancias de R2 y R24 (BOM sin dato).
- [ ] Esquema de la 1.7.1 (solo si se considera F).

## 8. Validación por etapas (alternativa A)

> **Regla para cada etapa:** antes de empezarla, el responsable da **autorización explícita** para esa etapa.
> Se registra: fecha, etapa, quién autoriza. Una autorización vale para **una** etapa. Una etapa autorizada **no
> aprueba** M3a, H4 ni C5. Si algo no coincide con lo esperado: **parar** y no seguir a la siguiente.
> En ninguna etapa se flashea, se abre un puerto serie ni se cambia el firmware.

| Etapa | Qué | Condición previa | Equipo | Criterio para seguir |
|---|---|---|---|---|
| V0 | Revisión y aprobación del diseño (este documento) | — | — | Aprobación escrita del responsable, en un PR aparte |
| V1 | M3a en el Atari solo (ficha existente) | V0; autorización | Osciloscopio 10×, multímetro | Datos completos. Recalcular Rs con ellos |
| V2 | Adaptador **sin energía** | V1; autorización | Multímetro | Rs dentro de ±1 % en el pin 8; 12 contactos con continuidad 1:1; sin cortos entre pines vecinos |
| V3 | Adaptador en banco, **sin Atari ni FujiNet**: fuente limitada a 1 mA en la entrada del pin 8 (0 V, 4,7 V, 5,25 V) y 10 kΩ a GND en la salida | V2; autorización | Fuente con límite, multímetro | Salida dentro de la tabla de §4.A (±2 %) |
| V4 | Adaptador en el **Atari solo**, con 10 kΩ a GND en la salida (simula R2). `POKE 54018,52` / `,60` | V3; autorización | Osciloscopio 10× | Nivel alto ≥ 2,6 V y **pico ≤ 3,3 V**; bajo ≤ 0,3 V |
| V5 | FujiNet **desenergizada** con el adaptador: M0 de MOTOR | V4; autorización | Multímetro | IO36–GND ≈ R2; J1 pin 8–IO36 ≈ Rs |
| V6 | Atari encendido, **FujiNet con S5 en OFF**, adaptador puesto | V5; autorización; orden de conexión de `procedimiento-medicion-pasiva-sio.md` §2 | Multímetro | Registrar IO36 y riel 3V3 con motor apagado y encendido. **Parar** si el 3V3 sube de forma apreciable (umbral lo fija el responsable) |
| V7 | Atari y FujiNet encendidos, firmware actual **sin configurar GPIO36**, adaptador puesto | V6; autorización | Multímetro, osciloscopio 10× en IO36 | Alto 2,6–3,3 V; bajo ≤ 0,3 V; picos ≤ 3,3 V |
| V8 | Lectura de GPIO36 por firmware (T9) | **Fuera de este documento.** Requiere M3a y H4 aprobadas, en PR aparte | — | — |

La evidencia de V4 puede servir al responsable para decidir C5 («adaptador validado», `h4-c-casetera.md`). La
decisión y el registro son suyos.

## 9. Fuentes

- **FujiNet FN32ROV-1.6** (repositorio oficial `FujiNetWIFI/fujinet-hardware`, CERN OHL 2.0), commit
  `372bf92236b883c27c0c5928a77adbe5e6e81b66` (2026-11-06; último cambio de la carpeta: `c5af53d254`,
  2024-03-29). Hoja única del esquema, sin cuadro de revisión:
  - `ATARI/Old-Versions/FN32ROV-1.6/FN32ROV-1.6_schematic.jpg`, SHA-256 `69b9bca9…fc653` —
    https://github.com/FujiNetWIFI/fujinet-hardware/blob/372bf92236b883c27c0c5928a77adbe5e6e81b66/ATARI/Old-Versions/FN32ROV-1.6/FN32ROV-1.6_schematic.jpg
  - `FN32ROV-1.6_BOM.ods`, SHA-256 `462dde61…68fd4dd` (R2, R20, R21, R24, U3, Q2, Q3).
  - `README.md`, SHA-256 `7743f378…3ae192a` (igual al ya citado el 2026-10-26).
- **FujiNet FN32ROV-1.3-Q24**, mismo commit: `ATARI/Old-Versions/FN32ROV-1.3-Q24/FN32ROV-1.3-Q24_schematic.jpg`,
  SHA-256 `4a7bb138…1e281`, idéntico al de la auditoría del 2026-10-26 (commit `0e539b8`) —
  https://github.com/FujiNetWIFI/fujinet-hardware/blob/372bf92236b883c27c0c5928a77adbe5e6e81b66/ATARI/Old-Versions/FN32ROV-1.3-Q24/FN32ROV-1.3-Q24_schematic.jpg
- Hojas de datos: TI SN74LS07 (enlace de la BOM: https://www.ti.com/lit/ds/sdls021d/sdls021d.pdf); Espressif
  ESP32 / ESP32-WROVER (características DC y valores máximos).
- Esquemas de terceros del Atari 65XE (hojas 5 y 9) y de la XC12 (`docs/fuentes.md`, 2026-10-26 y 2026-11-23).
- `h4-puerta-sio.md` §3.1 y §3.4, `ficha-medicion-m3a-motor.md`, `procedimiento-medicion-pasiva-sio.md`,
  `h4-c-casetera.md`, `mitigacion-c-motor-pin8.md`.

Los esquemas de FujiNet **no se copiaron** al repositorio: solo se citan. No se leyó código de FujiNet ni de terceros
(ADR-012). Los diseños de §4 son propios y genéricos (divisor, clamp, buffer, optoacoplador).
