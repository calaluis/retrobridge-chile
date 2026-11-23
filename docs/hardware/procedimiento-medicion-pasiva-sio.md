# Procedimiento de medición pasiva del SIO (H4): FujiNet FN32ROV-1.3-Q24

> **NO EJECUTAR sin autorización explícita para cada etapa.** Ninguna medición está hecha: todos los resultados
> están **pendientes**. M3a (Atari solo) requiere una autorización aparte. Las etapas que conectan la FujiNet al
> Atari (M3–M6) requieren además que **M3a haya pasado su punto de detención**.
>
> Análisis eléctrico: `h4-puerta-sio.md`. El firmware en la placa (`1b1527d`) no configura ningún GPIO del SIO.
> **No se flashea ni se reconfigura nada para medir.**

## 1. Instrumentos y reglas de conexión

| Instrumento | Uso | Requisito |
|---|---|---|
| Multímetro digital | Continuidad, resistencia (desenergizado) y tensión DC | Categoría y puntas adecuadas; impedancia de entrada ≥ 10 MΩ en tensión |
| Osciloscopio | Formas de onda en el lado de 5 V (J1/J2) y en el de 3,3 V | Sonda **10×**; masa de la sonda en GND de la placa (J1, pin 4 o 6) |
| Analizador lógico | Temporización (velocidad, t0, t1) | **Usarlo en el lado de 5 V solo si su tolerancia a 5 V está confirmada en la hoja de datos del modelo.** Si no, medir solo en el lado de 3,3 V o usar el osciloscopio |
| Amperímetro en serie o cable SIO de prueba | Corriente del pin 10 (M6) | Solo con un adaptador o cable de prueba preparado para medir sin cortar el cable original |
| Pulsera antiestática | Toda manipulación | La 1.3 no tiene TVS en el USB |

**Reglas:**
1. No conectar ninguna señal de 5 V a una entrada de instrumento sin tolerancia confirmada. **No usar sondas de
   3,3 V en J1/J2.**
2. Una sola referencia de masa: la de la placa (J1, pines 4/6). Conectar primero la masa y después la punta.
3. No conectar ni desconectar el cable SIO con el Atari encendido.
4. Nunca inyectar tensión en un pin. Todas las mediciones son de **lectura**.
5. Registrar cada resultado con fecha, instrumento, condiciones y foto de la configuración, en
   `docs/mediciones/` (sin datos privados).

## 2. Masas y alimentación simultánea

- La masa USB del PC y la del Atari quedan unidas por el GND del SIO. Para evitar lazos de masa, conectar el PC y el
  Atari **a la misma toma**, o usar un portátil con batería.
- **Con USB y SIO a la vez,** D4 y D5 hacen un OR: alimenta la fuente de mayor tensión, sin retroalimentación
  esperada. Aun así, en H4 y H5 **la placa se alimenta siempre por USB**, porque el pin 10 del Atari solo garantiza
  50 mA (riesgo R6).
- **Orden recomendado,** aplicable **solo después de aprobar M3a** (§3):
  - al encender: Atari apagado → conectar el SIO → USB a la FujiNet (S5 encendido) → encender el Atari;
  - al apagar: Atari → USB → desconectar el SIO.

> **Punto de detención MOTOR.** MOTOR CONTROL llega **directo a GPIO36**, sin buffer ni divisor (`h4-puerta-sio.md`
> §3.4). Por eso la FujiNet **no se conecta al SIO del Atari** (ni encendida ni apagada) hasta que M3a haya medido,
> **en el Atari solo**, que el nivel alto de MOTOR no supera lo que admite un pin del ESP32. Si la señal puede
> superar ese límite, **no se autoriza ninguna conexión** hasta diseñar y validar una mitigación aprobada.

## 3. Etapas

### M0. Desenergizada: continuidad y resistencias (multímetro)

Sin USB, sin Atari y con S5 apagado.

| Medición | Puntos | Esperado [E] |
|---|---|---|
| Paso de J1 a J2 | Pines 1–13 de J1 ↔ los mismos de J2 | Continuidad en los 13 |
| COMMAND | J1, pin 7 ↔ U3, pin 1 | Continuidad |
| DATA OUT | J1, pin 5 ↔ U4, pin 9 | Continuidad |
| DATA IN | J1, pin 3 ↔ U4, pin 6 | Continuidad |
| CLOCK OUT | J1, pin 2 ↔ U4, pin 3 | Continuidad |
| MOTOR | J1, pin 8 ↔ pad de IO36 (módulo, pin 4) | **Continuidad directa** |
| R2 | IO36 ↔ GND | unos 10 kΩ |
| R11 | U3, pin 2 (IO39) ↔ +3V3 | unos 10 kΩ |
| Divisor | J1, pin 10 ↔ IO35 / IO35 ↔ GND | unos 2 kΩ / unos 3,9 kΩ |
| +12 V | J1, pin 12 ↔ cualquier nodo del circuito | Sin continuidad (solo J1↔J2) |

> Con el circuito desenergizado, las lecturas en paralelo pueden diferir del valor nominal. Registrar el valor
> medido; no forzar una conclusión.

### M1. Solo USB, S5 encendido, sin Atari (multímetro)

| Medición | Esperado | Criterio |
|---|---|---|
| +3V3 | unos 3,3 V | 3,2–3,4 V |
| VCC (cátodo de D4) | Estimación: unos 4,6–4,7 V [I] | Registrar el valor medido; la estimación no lo sustituye |
| VCC_BUF (U3/U4, pin 14) | ≈ VCC | **Comparar con el mínimo de 4,75 V del 74LS07** (riesgo R7, por confirmar) |
| IO39 (U3, pin 2) | unos 3,3 V (R11, U3 con entrada flotante en alto) | Alto |
| IO36 | unos 0 V (R2) | Bajo |
| IO35 | unos 0 V (sin SIO_5V) | Bajo |

### M2. Solo USB, S5 apagado: aislamiento

| Medición | Esperado |
|---|---|
| +3V3 | 0 V |
| VCC | Presente (USB por D4) |
| VCC_BUF | **unos 0 V** (Q2 abierto) |

### M3a. Atari solo, FujiNet desconectada del SIO: nivel de MOTOR y reposo del bus (punto de detención)

Ficha autocontenida para un colaborador, con el modo de encender MOTOR y la hoja de registro:
`ficha-medicion-m3a-motor.md` (no cambia esta etapa ni sus criterios).

Con autorización. **La FujiNet no está conectada al Atari.** Se mide en el conector SIO del propio Atari, o en un
cable SIO de prueba sin ningún periférico, respecto de GND (pin 4 o 6).

| Medición | Instrumento | Esperado | Criterio |
|---|---|---|---|
| Pin 8 (MOTOR) en reposo | Multímetro u osciloscopio con sonda 10× | Bajo con un periférico que lo cargue [M]; **sin periférico puede quedar flotante**: en el 65XE ninguna resistencia lo baja (`h4-puerta-sio.md` §3.4) [E-Atari] | Registrar el valor |
| **Pin 8 (MOTOR) con el motor del casete encendido** (por ejemplo, una operación de casete desde el Atari) | **Osciloscopio con sonda 10×**: ver el nivel estable y cualquier pico | Desconocido: el manual solo dice "alto = encendido" [M] | **Punto de detención (abajo)** |
| Pin 10 (+5V/READY) | Multímetro | unos 5 V | Registrar el valor |
| Pines 7 (COMMAND), 5 (DATA OUT) y 3 (DATA IN) en reposo | Multímetro u osciloscopio | Altos (pull-ups del Atari) | Registrar el valor |

**Punto de detención:**
- **Seguir** solo si el nivel alto de MOTOR medido, **incluidos sus picos**, queda claramente por debajo de lo que
  admite un pin del ESP32, que la hoja de datos fija en VDD + 0,3 V (unos 3,6 V con 3,3 V de alimentación), con
  margen. Además hay que considerar que, con la FujiNet apagada, un nivel alto en IO36 podría entrar por las
  protecciones del pin [I].
- **Detener** si supera ese límite o no se puede determinar con confianza. Entonces no se autoriza ninguna
  conexión de la FujiNet al Atari hasta diseñar, revisar y validar una mitigación, por ejemplo una adaptación de
  nivel en la línea o la desconexión de MOTOR en el cable. Una mitigación implica cambios de hardware o de cableado
  y requiere una aprobación aparte.

### M3. Atari encendido, FujiNet apagada (S5 apagado, sin USB), con autorización y M3a aprobado

| Medición (en J1/J2, respecto de GND) | Esperado |
|---|---|
| Pin 10 (+5V/READY) | unos 5 V |
| Pin 7 (COMMAND) | Alto en reposo (lo pone el Atari) |
| Pin 3 (DATA IN) | Alto (pull-up del Atari); la 1.3 no aporta ninguno |
| Pin 5 (DATA OUT) | Alto (marca) |
| Pin 8 (MOTOR) | Bajo en reposo |
| VCC (por D5 desde SIO_5V) | Presente |
| VCC_BUF | **unos 0 V** (sin +3V3, Q2 abierto): bus aislado |

### M4. Atari encendido y FujiNet por USB con `1b1527d` (sin configuración SIO), con autorización y M3a aprobado

| Medición | Esperado | Criterio |
|---|---|---|
| COMMAND en J1 / en IO39 | Alto / unos 3,3 V | Polaridad conservada |
| DATA IN en J1 | Alto (la placa no lo fuerza) | La placa no bloquea el bus |
| DATA OUT en J1 / en IO33 | Alto / **a medir** | El esquema no tiene pull-up en IO33. Se registra el nivel real; la hipótesis de entrada flotante se confirma o se descarta aquí (riesgo R3) |
| MOTOR en IO36 en reposo | unos 0 V (R2) | Confirmación con los dos equipos conectados; el nivel alto ya se midió en M3a |

### M5. Observación pasiva de COMMAND y DATA OUT (osciloscopio o analizador), con autorización y M3a aprobado

Durante el arranque, el Atari consulta D1 y envía tramas de comando. RetroBridge **no responde**, porque DATA IN
nunca se maneja, así que el Atari reintenta y termina por tiempo de espera. La observación es pasiva.

| Medición | Dónde | Instrumento |
|---|---|---|
| Polaridad y duración de COMMAND | J1, pin 7 (5 V) e IO39 (3,3 V) | Osciloscopio de 2 canales |
| Velocidad del bus (~19 200 ± 5 %) | J1, pin 5 | Osciloscopio o analizador con tolerancia a 5 V |
| t0 (COMMAND↓ → primer bit) y t1 (último bit → COMMAND↑) | J1, pines 7 y 5 | Igual |
| Nivel y flanco de subida en IO33 | IO33 (lado de 3,3 V) | Osciloscopio. Línea base con el firmware actual, que no configura GPIO33. Determina si hace falta un pull-up y de qué tipo; la decisión se toma en H5 |

Registrar en `docs/mediciones/sio-tiempos.md`, según la arquitectura §9.3: fecha, commit, Atari, NTSC/PAL, ROM,
instrumento, frecuencia de muestreo, y mínimo, máximo y P99 sobre al menos 1 000 transacciones cuando aplique.

### M6. Alimentación doble y corriente del pin 10, con autorización, cable de prueba y M3a aprobado

| Medición | Esperado |
|---|---|
| J1, pin 10 con el Atari **apagado** y la FujiNet por USB | unos 0 V: D5 impide la retroalimentación |
| Corriente del pin 10 con USB y SIO a la vez | Registrar (límite del Atari: 50 mA) |
| IO39 con el Atari apagado y la FujiNet por USB | [I] Bajo (entradas del 74LS07 con líneas sin alimentación): confirma el riesgo R12 |

## 4. Criterios para detener

- **MOTOR (M3a):** nivel alto, o picos, por encima de lo que admite un pin del ESP32, o imposible de determinar.
  No se conecta la FujiNet al Atari sin una mitigación aprobada y validada.
- Cualquier tensión mayor que 3,6 V en un GPIO.
- Calor, olor o consumo anómalo.
- VCC_BUF presente con S5 apagado (falla el aislamiento).
- Una sonda o un instrumento sin tolerancia confirmada conectado a una línea de 5 V.
