# Ficha de medición M3a: tensión de MOTOR (pin 8 del SIO) en un Atari 65XE

> **NO EJECUTAR M3a SIN AUTORIZACIÓN EXPLÍCITA DEL RESPONSABLE.** La autorización para medir **no** es una
> aprobación de M3a ni de H4: esas aprobaciones son decisiones aparte, con su propio registro.
>
> **Ficha para un colaborador.** Se entiende sola: no hace falta conocer el proyecto. Desarrolla la etapa M3a de
> `procedimiento-medicion-pasiva-sio.md` (§3) y no la cambia. **Esta ficha no aprueba M3a:** la decisión, el
> registro y el cambio de `docs/mediciones/puertas.json` son del responsable, en un PR aparte.
>
> Solo se mide el **Atari solo**. **No se conecta la FujiNet ni ningún otro periférico.** No se flashea nada, no se
> abre ningún puerto serie y no se inyecta tensión en ningún pin.

## 1. Para qué sirve

**Alcance:** MOTOR (pin 8), la alimentación del pin 10 y el reposo de COMMAND, DATA OUT y DATA IN (pines 7, 5 y
3), que la etapa M3a del procedimiento también pide. Nada más de H4.

RetroBridge quiere cargar programas de casete (`.cas`) en un Atari 65XE desde una placa FujiNet con un ESP32. Para
saber cuándo «arranca la casetera», el ESP32 debe leer la línea **MOTOR CONTROL** (pin 8 del conector SIO).

En la FujiNet FN32ROV-1.3, ese pin llega **directo** a una entrada del ESP32 (GPIO36), sin resistencia en serie ni
adaptador de nivel; solo hay una resistencia de 10 kΩ a masa. Esa entrada admite como máximo **unos 3,6 V**.

Según el esquema del 65XE (de terceros), con el motor encendido el pin 8 da **unos 4,7–4,9 V**. Eso supera el
límite, pero es una previsión, **no una medición**. Esta ficha pide medir el valor real, **con sus picos**, antes de
conectar nada.

**Resultado esperado de antemano:** por el esquema, lo más probable es que el nivel supere 3,6 V. Aun así la medición
es necesaria: confirma el riesgo con datos del equipo real y da las cifras (nivel estable, picos, flancos) para
diseñar una protección. Un resultado «alto» no es un fracaso de la medición.

## 2. Qué se necesita

| Elemento | Requisito |
|---|---|
| Atari 65XE (el del responsable) | Con su fuente original. **Sin ningún periférico SIO conectado.** Televisor o monitor |
| Osciloscopio | Imprescindible para los picos. Sonda **10×**, compensada. Base de tiempos ajustable y disparo por flanco |
| Multímetro digital | Tensión DC, continuidad. Impedancia ≥ 10 MΩ |
| Acceso a los pines del SIO | **Recomendado:** un cable SIO con un extremo libre, o una placa de prueba (*breakout*) SIO, para no meter puntas en el conector del Atari. Si no hay, puntas finas con mucho cuidado (§4, regla 3) |
| Opcional | Una resistencia de **10 kΩ** (¼ W) con dos pinzas, para la medición C (§5) |
| Pulsera antiestática | Recomendable |

## 3. Conector SIO: pines que se usan

Solo se tocan estos pines. **Ningún otro.**

| Pin | Señal | Uso en esta ficha |
|---|---|---|
| 4 y 6 | GND (masa) | Referencia de todas las medidas |
| 8 | MOTOR CONTROL | **La medición** |
| 10 | +5 V / READY | Confirmar la orientación del conector (unos 5 V con el Atari encendido) y medición D |
| 7, 5 y 3 | COMMAND, DATA OUT, DATA IN | Solo reposo, con multímetro (medición E) |

**Confirmar la orientación antes de medir** (los dibujos del conector cambian según se miren desde el Atari o desde el
cable):
1. Con el Atari **apagado**: continuidad entre el pin que crees 4 y el que crees 6 (deben estar unidos).
2. Con el Atari **encendido**: unos 5 V entre el pin que crees 10 y la masa.
3. Si alguna de las dos no se cumple, la numeración está invertida o mal identificada: **parar** y revisarla.

## 4. Reglas de seguridad

1. Todas las mediciones son de **lectura**. Nunca se inyecta tensión ni corriente en un pin.
2. **Primero la masa, después la punta.** Pinza de masa de la sonda en el pin 4 o 6. Una sola referencia de masa.
3. No puentear pines vecinos con la punta. Si se mide directo en el conector, hacerlo con el Atari apagado al colocar
   la punta, y encenderlo después.
4. No conectar ni desconectar cables SIO con el Atari encendido.
5. Osciloscopio y Atari en la **misma toma de corriente** (evita lazos de masa).
6. Si algo huele, se calienta o se comporta raro: apagar el Atari y parar.

## 5. Mediciones

Para encender y apagar el motor **sin casetera y sin límite de tiempo** se usa Atari BASIC: la línea del motor se
controla con el registro PACTL del PIA (dirección 54018, `$D302`).

- Motor **encendido**: `POKE 54018,52`
- Motor **apagado**: `POKE 54018,60`

Encender el Atari con BASIC (sin cartucho, o con el cartucho BASIC, según el equipo). Escribir cada orden y pulsar
RETURN. Si el comportamiento no coincide con lo esperado (por ejemplo, el nivel no cambia), anotarlo: el valor de
PACTL tiene que confirmarse en el equipo real.

### Medición A — reposo (motor apagado)

| Paso | Qué hacer | Anotar |
|---|---|---|
| A1 | Atari encendido en BASIC; `POKE 54018,60` | — |
| A2 | Multímetro: pin 8 respecto de masa | Tensión |
| A3 | Osciloscopio en el pin 8, 1 V/div, 1 ms/div | Nivel y si hay ruido |

Nota: según el esquema del 65XE, con el motor apagado el pin 8 **queda flotante**: ninguna resistencia del Atari lo
baja; lo fija la carga del periférico (la FujiNet pone 10 kΩ a masa). **Sin periférico, el reposo puede no ser
«bajo»** y el valor puede ser inestable. Anótalo tal cual; la medición C muestra el reposo con carga.

### Medición B — motor encendido, sin carga (punto de detención)

| Paso | Qué hacer | Anotar |
|---|---|---|
| B1 | Osciloscopio en el pin 8: 1 V/div, disparo por **flanco de subida** a unos 2 V, modo **disparo único** | — |
| B2 | `POKE 54018,52` | **Valor máximo del pico** al encender, y su duración |
| B3 | Con el motor encendido, nivel estable (medida «media» o cursores) durante al menos 10 s | Nivel estable; mínimo y máximo |
| B4 | Multímetro en el pin 8 | Tensión DC |
| B5 | Disparo por **flanco de bajada**, modo único; `POKE 54018,60` | Pico o rebote al apagar (pueden aparecer picos **negativos**: anotarlos también) |
| B6 | Repetir B1–B5 **tres veces** | Los tres valores de cada dato |

### Medición C — motor encendido, con 10 kΩ a masa (opcional, recomendada)

Simula la carga que pone la FujiNet (su resistencia de 10 kΩ a masa). Solo si se dispone de la resistencia y de un
acceso seguro al pin (cable o *breakout*).

| Paso | Qué hacer | Anotar |
|---|---|---|
| C1 | Atari **apagado**. Resistencia de 10 kΩ entre pin 8 y pin 4 (masa) | — |
| C2 | Encender, BASIC, repetir B1–B5 una vez | Mismos datos que en B |
| C3 | Apagar el Atari y quitar la resistencia | — |

### Medición D — pin 10 (comprobación)

| Paso | Qué hacer | Anotar |
|---|---|---|
| D1 | Multímetro: pin 10 respecto de masa, con el motor apagado y encendido | Las dos tensiones |

### Medición E — reposo de COMMAND, DATA OUT y DATA IN

Solo lectura, con multímetro, en BASIC y sin ninguna operación de disco ni de casete en curso.

| Paso | Qué hacer | Anotar |
|---|---|---|
| E1 | Pin 7 (COMMAND) respecto de masa | Tensión (se espera alta: resistencia a +5 V del Atari) |
| E2 | Pin 5 (DATA OUT) respecto de masa | Tensión (se espera alta) |
| E3 | Pin 3 (DATA IN) respecto de masa | Tensión (se espera alta) |

## 6. Criterio (lo decide el responsable)

El límite de la entrada del ESP32 es **VDD + 0,3 V, unos 3,6 V**.

- **Seguir:** el nivel estable **y todos los picos** de B (y de C, si se hizo) quedan **claramente por debajo** de
  3,6 V, con margen. Además se tendrá en cuenta que, con la FujiNet apagada, cualquier nivel alto podría entrar por
  las protecciones del pin.
- **Detener:** algún valor supera ese límite, o no se puede determinar con confianza. Entonces **no se conecta la
  FujiNet al Atari** hasta diseñar, revisar y validar una protección (adaptación de nivel o no conectar MOTOR en el
  cable). Esa protección es un cambio de hardware con aprobación aparte.

El colaborador **no decide**: entrega los datos. El responsable aplica el criterio.

**Dos criterios distintos; ninguno se deduce del otro:**

| Puerta | Criterio | Documento |
|---|---|---|
| M3a | Nivel estable y picos claramente por debajo de VDD + 0,3 V (unos 3,6 V), con margen | `procedimiento-medicion-pasiva-sio.md`, etapa M3a |
| H4-C, condición C5 | Pico de MOTOR ≤ 3,3 V medido en el Atari solo, **o** un adaptador validado | `h4-c-casetera.md` |

Pasar M3a no aprueba C5 ni H4, y una aprobación de H4 no aprueba M3a. Cada una se decide y se registra aparte.

## 7. Qué entregar

1. La tabla de la §8 rellenada.
2. **Fotos o capturas del osciloscopio** de B2, B3 y B5 (y de C, si se hizo), con la escala visible.
3. Una foto de la configuración (dónde están la masa y la punta). Sin datos personales ni números de serie.

El responsable lo registra en `docs/mediciones/m3a-motor-<fecha>.md`.

## 8. Hoja de registro

| Dato | Valor |
|---|---|
| Fecha | |
| Quién midió (nombre o alias; opcional) | |
| Atari: modelo, NTSC/PAL, modificaciones conocidas | 65XE NTSC, 128 KB (confirmar) |
| Fuente de alimentación del Atari | |
| Osciloscopio: modelo; sonda (10×, compensada sí/no) | |
| Multímetro: modelo | |
| Acceso a los pines (cable, *breakout*, puntas directas) | |
| Orientación confirmada (§3): pines 4–6 unidos; pin 10 ≈ 5 V | sí / no |
| A2 / A3: reposo | |
| B2: pico al encender (máx., duración), 3 veces | |
| B3: nivel estable (mín. / máx.), 3 veces | |
| B4: multímetro con el motor encendido | |
| B5: pico o rebote al apagar (positivo y negativo), 3 veces | |
| C: con 10 kΩ (B2, B3, B5), si se hizo | |
| D1: pin 10 con motor apagado / encendido | |
| E1 / E2 / E3: pines 7, 5 y 3 en reposo | |
| ¿`POKE 54018,52` y `,60` encendieron y apagaron el nivel? | sí / no |
| Observaciones (olor, calor, comportamiento raro) | |

## 9. Fuentes

- `procedimiento-medicion-pasiva-sio.md` §1–§4 (reglas, etapa M3a y criterios para detener).
- `h4-puerta-sio.md` §3.4 (recorrido de MOTOR en la FN32ROV-1.3, lado del 65XE y riesgos) y §3.5.
- `h4-c-casetera.md` §4 (C5: «Medición M3a por un tercero»).
- Registro PACTL (`$D302`) y su bit de control del motor: documentación pública del sistema Atari 8 bits (mapa de
  memoria del PIA). **[P]** Confirmar en el equipo real (§8).
