# Ficha de medición: GPIO35 (divisor de SIO +5V) en la FujiNet FN32ROV-1.3, solo por USB

> **NO MEDIR SIN AUTORIZACIÓN EXPLÍCITA DEL RESPONSABLE.** La autorización para medir **no** cambia el umbral de C-1,
> no autoriza repetir el banco C8a y no aprueba H4, R2 ni R7. Cada una de esas decisiones va aparte, con su registro.
>
> **Ficha para un colaborador.** Se entiende sola. Solo se mide la **FujiNet 1.3 sola**, alimentada por USB, con el
> cable SIO **desconectado** y **sin el Atari**. No se flashea nada, no se inyecta tensión en ningún pin y no se
> suelda nada. Leer la consola por USB (opcional, §5 paso C) necesita su propia autorización.

**Convención:** **[E]** esquema oficial de FujiNet; **[D]** hoja de datos del fabricante; **[H]** dato ya registrado
en `docs/mediciones/banco-c8a-2026-11-25.md`; **[T]** cálculo propio; **[I]** inferencia, no medida; **[P]** pendiente.

## 1. Para qué sirve

El banco C8a solo corre si la línea SIO +5V (pin 10 del conector SIO) está «ausente». Lo comprueba leyendo GPIO35 con
el ADC del ESP32: menos de 500 mV en el pin cuenta como ausente (enclavamiento C-1).

El 2026-11-25, con solo USB y sin cable SIO, el banco se negó a correr. Hay tres datos que no encajan del todo **[H]**:

| Técnica | Valor |
|---|---|
| GPIO35 leído por el ADC del firmware | 670–688 mV |
| SIO +5V calculado con el divisor a partir del ADC | ≈1,02 V |
| Pin 10 medido con multímetro (responsable) | 0,86 V y 0,88 V (0,00 V sin USB) |

Con el divisor del esquema, 0,87 V en el pin 10 darían ≈0,575 V en GPIO35 **[T]**, no ≈0,675 V. Falta un dato: la
**tensión real en GPIO35 medida con multímetro**. Esta ficha pide medirla, junto con el pin 10 en la misma sesión.

**Esta ficha no decide nada.** Da datos para que el responsable decida sobre C-1 en un PR aparte.

## 2. El circuito

Del esquema oficial de la FN32ROV-1.3 **[E]** (`h4-puerta-sio.md` §2, tabla del pin 10):

```text
pin 10 (SIO_5V) ──┬── D5 (Schottky PMEG2010) ──► VCC ◄── D4 ── USB_5V
                  │
                  R4  2 kΩ
                  │
                  ├──────────► IO35 (GPIO35, ADC1_CH7, solo entrada)
                  │
                  R5  3,9 kΩ
                  │
                 GND
```

- Relación del divisor: V(IO35) = V(pin 10) × 3,9 / 5,9 ≈ 0,661 × V(pin 10) **[T]**.
- Que R4 (2 k) esté arriba y R5 (3,9 k) abajo sale del esquema y de la cuenta del firmware (1015 mV informados para
  671 mV leídos: × 1,513 = 5,9 / 3,9) **[E][H]**. No se ha comprobado en la placa **[P]**.
- La resistencia vista desde IO35 es ≈1,3 kΩ **[T]**. Un multímetro de 10 MΩ no la altera de forma apreciable **[T]**.

## 3. Qué se necesita

- Multímetro digital en tensión continua, escala de 2 V (o automática), con una resolución de 1 mV o mejor.
- Puntas finas o de aguja. **Sin pinzas de cocodrilo en el lado del ESP32.**
- La FujiNet FN32ROV-1.3-Q24 con el firmware que tiene ahora (menu-sio `bea39e1`), y su cable USB.
- Lupa o buena luz para leer la serigrafía.

## 4. Reglas de seguridad

1. El cable SIO **desconectado** de la FujiNet durante toda la medición. El Atari, apagado y lejos.
2. Antes de medir con USB, localizar los puntos **sin alimentación** (§5 paso 0).
3. La punta negra, fija en GND. La roja toca **un solo punto** cada vez. No deslizar la punta entre patas.
4. Si no se encuentra R5 o el nodo del divisor con seguridad, **parar** y anotarlo. No medir en el módulo del ESP32
   por tanteo: un corto entre patas vecinas puede reiniciar la placa o dañarla.
5. No tocar el pin 10 y otro pin a la vez con la misma punta.

## 5. Mediciones

**GND de referencia:** el pin 4 o el 6 del conector SIO de la FujiNet **[E]**, o la carcasa del conector USB.

**Dónde medir GPIO35 (en orden de preferencia):**
1. El extremo de **R5 que no va a GND** (es el nodo del divisor, igual a IO35) **[E]**.
2. El extremo de **R4 que no va al pin 10** (el mismo nodo).

La posición física de R4 y R5 en la placa **no está documentada** en el proyecto **[P]**: buscarlas por la serigrafía.
Para confirmar cuál es cuál, sin alimentación, la continuidad (paso 0) ayuda: un extremo de R4 va al pin 10; un
extremo de R5 va a GND.

### Paso 0 — sin alimentación (USB desconectado)

| # | Qué | Esperado |
|---|---|---|
| 0.1 | Continuidad o resistencia del pin 10 a un extremo de R4 | ≈0 Ω |
| 0.2 | Resistencia entre el pin 10 y el nodo del divisor | ≈2 kΩ (puede leer algo menos por el resto del circuito) |
| 0.3 | Resistencia entre el nodo del divisor y GND | ≈3,9 kΩ (ídem) |

Si 0.2 y 0.3 salen al revés (≈3,9 k y ≈2 k), anotarlo: el divisor estaría invertido respecto al esquema.

### Paso A — sin USB

| # | Qué | Esperado |
|---|---|---|
| A1 | Pin 10 a GND | 0,00 V |
| A2 | Nodo GPIO35 a GND | 0,00 V |

### Paso B — solo USB (cable SIO desconectado, sin Atari)

Conectar el USB y esperar unos 10 s a que arranque.

| # | Qué | Anotar |
|---|---|---|
| B1 | Pin 10 a GND | V |
| B2 | Nodo GPIO35 a GND | V |
| B3 | Repetir B1 | V |
| B4 | Repetir B2 | V |

B1 y B2 deben medirse **en la misma sesión**, sin desconectar el USB entre ellas.

### Paso C — opcional: lectura del firmware a la vez

Solo con autorización para abrir el puerto USB (COM3). Con el USB todavía conectado, reiniciar la placa y anotar la
línea de arranque de menu-sio: `boot: SIO +5V (pin 10) … mV (GPIO35 … mV)`. Así quedan juntas las tres técnicas.

## 6. Cómo leer el resultado (lo decide el responsable)

Con R = B2 / B1 (relación medida) y la lectura del ADC del firmware (§1 o paso C):

| Si… | Lo que indica | Lo que **no** indica |
|---|---|---|
| R ≈ 0,66 y B2 ≈ 0,57 V | El divisor es el del esquema; el ADC lee unos 100 mV de más | Que el umbral deba cambiar: es una decisión aparte |
| B2 ≈ lectura del ADC (≈0,67 V) | El ADC lee bien; la relación del divisor o el pin 10 no son los del esquema | La causa exacta |
| Paso 0 con el divisor invertido | El esquema no describe esta placa en ese punto | Nada sobre otras placas |
| Ninguno de los anteriores | Discrepancia sin explicar | — |

El origen de la tensión en el pin 10 con solo USB (por ejemplo, corriente inversa de D5 desde VCC) es **una hipótesis
sin verificar [I]**. Esta ficha no la confirma ni la descarta.

## 7. Qué entregar

- La hoja del §8 rellena, con fecha, multímetro (marca y modelo) y quién midió.
- Si se puede, una foto de la zona de R4/R5 con la serigrafía visible. Sin datos de red ni de cuentas en la foto.
- Cualquier desviación de la ficha (punto de medida distinto, valores inestables).

## 8. Hoja de registro

```text
Fecha:                  Medido por:
Placa: FN32ROV-1.3-Q24  Firmware: menu-sio bea39e1
Multímetro (marca/modelo):
Cable SIO desconectado: sí / no    Atari: ausente / presente
Punto usado para GPIO35: R5 / R4 / otro: ____

0.1 pin 10 – R4:        ____ Ω
0.2 pin 10 – nodo:      ____ Ω
0.3 nodo – GND:         ____ Ω
A1 pin 10 (sin USB):    ____ V
A2 GPIO35 (sin USB):    ____ V
B1 pin 10 (USB):        ____ V
B2 GPIO35 (USB):        ____ V
B3 pin 10 (USB, rep.):  ____ V
B4 GPIO35 (USB, rep.):  ____ V
C  firmware (opcional): SIO +5V ____ mV   GPIO35 ____ mV

Observaciones:
```

## 9. Fuentes

- Esquema oficial de la FujiNet FN32ROV-1.3, resumido en `h4-puerta-sio.md` §2 (pin 10, D5, R4, R5, IO35) **[E]**.
- `docs/mediciones/banco-c8a-2026-11-25.md` §6: lecturas del ADC y medición del pin 10 **[H]**.
- `docs/hardware/banco-c8a-aislado.md` §6: enclavamiento C-1 y su umbral.
