# SIO +5V con solo USB en la FujiNet 1.3: series de lectura y diagnóstico (2026-11-25)

> Estado: **diagnóstico abierto.** Con solo USB, sin cable SIO y sin Atari, GPIO35 ha leído entre **453 y 688 mV**.
> La lectura cambia en minutos, con reinicio y sin él. El enclavamiento C-1 (menos de 500 mV en el pin) pasa o falla
> según el momento: **en esta placa no sirve como criterio, y su resultado no se usa en ningún sentido.**
>
> - El mecanismo **no está identificado**.
> - No cambia el umbral de C-1, no autoriza repetir C8a y no aprueba H4, R2 ni R7. `puertas.json` no cambia.
> - Hardware original intocable: nada de lo aquí propuesto modifica la placa.

**Convención:**
- **[H]** dato observado en la consola USB el 2026-11-25 (esta sesión);
- **[A]** dato de capturas anteriores en `Respaldos`, con el montaje **no confirmado**;
- **[E]** esquema y lista de componentes oficiales de la FN32ROV-1.3-Q24;
- **[R]** dato comunicado por el responsable o por la revisión independiente;
- **[T]** cálculo propio; **[I]** inferencia o hipótesis; **[P]** pendiente.

## 1. Autorización y montaje

- Autorización del responsable para leer por COM3 solo con `sio status`, sin flashear, sin cambiar configuración y sin
  escribir eFuses («Sí, solo USB»). `espefuse summary` queda para una autorización aparte.
- Montaje: FujiNet FN32ROV-1.3-Q24 solo por USB (COM3), cable SIO desconectado, sin el Atari, firmware menu-sio
  `bea39e1`. El responsable confirma que **no desenchufó el USB** en ningún momento. **[R]**

## 2. Circuito (fuentes oficiales)

Del esquema `FN32ROV-1.3-Q24_schematic.jpg` y la lista de componentes `FN32ROV-1.3-Q24_BOM.ods` del repositorio oficial
`FujiNetWIFI/fujinet-hardware`, carpeta `ATARI/Old-Versions/FN32ROV-1.3-Q24` **[E]**:

- D4 `PMEG2010ER,115`: USB_5V (ánodo) → VCC (cátodo). D5 `PMEG2010ER,115`: SIO_5V (ánodo) → VCC (cátodo).
- R4 de 2 kΩ: SIO_5V → IO35 (pata 7 del módulo). R5 de 3,9 kΩ: IO35 → GND. Encapsulado 0402. **R4 va arriba.**
- En la red SIO_5V solo aparecen J1.10, J2.10, D5 y R4 (lectura de la imagen; la lista de conexiones `.dch` no se
  procesó). Los 74LS07 se alimentan de VCC_BUF, no de SIO_5V.

## 3. Firmware

`main/bench_c8_console.cpp` (C8a) y `main/menu_sio.cpp` (menu-sio) configuran igual el ADC: ADC1, canal 7, 12 dB,
12 bits, calibración por ajuste lineal (`default_vref = 1100`). C8a usa 8 lecturas sueltas; menu-sio, su media. **[H]**

- No apareció el aviso «ADC without calibration»: el objeto de calibración se creó. **No se sabe** si usa datos de eFuse
  del chip o la referencia por defecto de 1100 mV **[P]**.
- menu-sio informa SIO +5V = pin × 5900 / 3900, y 0 mV si el pin está por debajo de 500 mV (`kNoSioPowerMv`).

## 4. Series de esta sesión

### 4.1 Con reinicio por COM3 (`sio status` cada 5 min) **[H]**

| Tiempo desde el reinicio | GPIO35 (ADC) | SIO +5V informado | C-1 (< 500 mV) |
|---|---|---|---|
| 0,6 s (arranque) | 453 mV | 0 mV | pasaría |
| 10 s | 461 mV | 0 mV | pasaría |
| 5 min | 556 mV | 841 mV | rechazaría |
| 10 min | 595 mV | 900 mV | rechazaría |
| 15 min | 615 mV | 930 mV | rechazaría |
| 20 min | 632 mV | 956 mV | rechazaría |
| 25 min | 650 mV | 983 mV | rechazaría |
| 30 min | 645 mV | 975 mV | rechazaría |

Sin panics ni reinicios inesperados. El Wi-Fi se conectó a los 3,7 s y siguió conectado.

### 4.2 Sin reinicio (COM3 abierto con DTR y RTS desactivados), unos minutos después **[H]**

Ninguna línea `rst:` en la captura: la placa no se reinició.

| Tiempo desde que se abrió COM3 | GPIO35 (ADC) | SIO +5V informado |
|---|---|---|
| 2 s | 597 mV | 903 mV |
| 32 s | 617 mV | 933 mV |
| 63 s | 616 mV | 931 mV |
| 93 s | 627 mV | 948 mV |
| 123 s | 639 mV | 966 mV |

### 4.3 Antes, el mismo día **[H]**

- Arranque antes de flashear el banco: 671 mV. `c8a run`: 670–681 mV. Arranque tras restaurar: 688 mV.
- Multímetro en el pin 10: 0,00 V sin USB; 0,86 y 0,88 V con USB **[R]** (`banco-c8a-2026-11-25.md` §6).

## 5. Capturas anteriores (montaje no confirmado) **[A]**

| Situación según el registro | GPIO35 (ADC) |
|---|---|
| 65XE encendido (6 capturas) | **3160 mV siempre** (SIO informado 4780 mV): lectura saturada del ADC |
| «65XE off» con el Atari conectado | SIO informado 0 mV; 142 mV en varias capturas |
| USB, 21–22 nov | 411–435 mV (subiendo en arranques seguidos), 447 y 552 mV |

## 6. Cálculos **[T]**

- Relación del divisor: 3,9 / 5,9 = 0,661. El umbral de C-1 (500 mV en el pin) equivale a 0,756 V en el pin 10.
- Con un ADC perfecto, 0,86–0,88 V en el pin 10 darían 569–582 mV en GPIO35: **por encima de 500 mV**.
- Corriente que el divisor toma de la línea a 0,87 V: ≈147 µA. Un multímetro de 10 MΩ toma ≈0,09 µA.
- Rango sin Atari visto hoy: 453–688 mV. Atari encendido: 3160 mV o más (saturado). Relación de más de 4,5 veces.

## 7. Hipótesis (sin comprobar) **[I]**

- **H-A.** Corriente inversa de D5 desde VCC hacia SIO_5V, cargando el divisor: compatible con el esquema (no hay otra
  fuente en la red). La revisión independiente da para el PMEG2010ER una corriente inversa típica de 60 µA a 5 V y
  25 °C, sin máximo a 5 V **[R]**: plausible, no demostrado frente a los ≈147 µA calculados.
- **H-B.** Dependencia de la temperatura. Pierde fuerza: tras un reinicio la lectura bajó a 453 mV en 0,6 s, demasiado
  rápido para un enfriamiento.
- **H-C.** Influencia del estado de funcionamiento (arranque, puerto USB-serie abierto y enviando texto): compatible
  con 4.1 y 4.2, no comprobada.
- **H-D.** Error del ADC (referencia por defecto, no linealidad a 12 dB): puede explicar parte de la diferencia, no
  toda la variación.

## 8. Consecuencias

- El resultado de C-1 en esta placa depende del momento de la lectura. **Pasar C-1 justo tras un arranque no es una
  validación** y no se usa para ejecutar C8a.
- El criterio actual de C-1 no sirve en este montaje. No está demostrado que no exista una alternativa por software:
  tendría que conservar el aislamiento y demostrar que nunca confunde un Atari encendido con su ausencia.
- `SioPowerWatch` (menu-sio) usa 1000 mV en SIO como umbral de «apagado». Sin Atari se han visto de 0 a 1040 mV: el
  margen cambia de una sesión a otra. No se ha observado un fallo funcional por ello.

## 9. Pendiente **[P]**

- Fuente de la calibración del ADC (`espefuse summary`, autorización aparte).
- Mecanismo de la variación (H-A a H-D).
- Comportamiento del pin 10 al encender un Atari (subida), si se propone redefinir C-1.
- Decisión del responsable sobre C-1, en su propio PR con pruebas. No se amplía por inferencia.
