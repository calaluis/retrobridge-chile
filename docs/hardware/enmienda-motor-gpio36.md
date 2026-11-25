# Enmienda: MOTOR (pin 8 del SIO) y GPIO36 en la FN32ROV-1.3-Q24

> **PROPUESTA DE ENMIENDA NO APROBADA. Solo documento.**
>
> - No aprueba M3a, H4, H4-L, H4-T ni DATA IN.
> - No cambia `docs/mediciones/puertas.json`, el perfil de la placa (`rb_board`) ni `kTapeOutput`, que sigue en `false`.
> - No hubo pruebas físicas, mediciones, flasheos ni cambios de GPIO.
> - Solo el responsable la aprueba, la rechaza o la cambia (§7).
>
> Amplía, sin reemplazarlos: `h4-puerta-sio.md` §3.4 y §5 (R1), `proteccion-motor-gpio36.md`,
> `ficha-medicion-m3a-motor.md` y `mitigacion-c-motor-pin8.md`.

## 0. Etiquetas

Son las mismas de `proteccion-motor-gpio36.md` §0:

| Etiqueta | Significado |
|---|---|
| **[E]** | Esquema o BOM oficial de FujiNet (`fujinet-hardware`, commit `0e539b8`) |
| **[E-Atari]** | Esquema de terceros del 65XE o de la XC12 (Jerzy Sobola), no versionado |
| **[D]** | Hoja de datos del fabricante |
| **[M]** | Manual de Atari C016555, §9 |
| **[H]** | Registro histórico del equipo (proyecto WSS, G5-2) |
| **[C]** | Comprobado en este repositorio (código o pruebas) |
| **[T]** | Cálculo propio a partir de las fuentes anteriores |
| **[I]** | Inferencia: razonable, pero no leída ni medida |
| **[P]** | Pendiente: hipótesis no verificada. No se usa para decidir |

## 1. Motivo

El responsable planteó una contradicción aparente:
- La FN32ROV-1.3-Q24 se usó con el firmware original de FujiNet y con MOTOR activo durante 15 min 27 s, y la placa
  siguió funcionando [H].
- Ese uso no tuvo ninguna medición eléctrica [H].
- RetroBridge, en cambio, exige M3a y H4 antes de leer MOTOR o de manejar DATA IN.

Esta enmienda explica por qué las dos cosas son compatibles. El riesgo está en la placa, no en el firmware. Que la
placa funcione demuestra que sobrevivió en esas condiciones. No demuestra que el pin trabaje dentro de lo que admite
la ESP32.

## 2. E1: MOTOR es una señal nominal de unos 5 V y la 1.3 la lleva directo a GPIO36

| Hecho | Fuente |
|---|---|
| En el 65XE, MOTOR CONTROL sale del colector de Q7, un PNP con el emisor en +5A, por L11 (ferrita) al pin 8. No hay resistencia en serie | [E-Atari], hoja 5 |
| Con el motor encendido, el nivel previsto es de **≈ 4,7–4,9 V**: 5 V menos la saturación de Q7 | [T] |
| El manual de Atari dice «alto = encendido» y **no especifica la tensión** | [M] |
| En la XC12, el pin 8 alimenta la electrónica de la casetera y la base de Q2 por R3 (27 k). Es una salida pensada para dar corriente, no solo un nivel lógico | [E-Atari] |
| En la FN32ROV-1.3-Q24, `SIO_MCTL` llega **directo** a U1 pin 4 (SENSOR_VP/IO36). Solo tiene R2, 10 kΩ, **a GND**: está en paralelo con la entrada, no en serie | [E] |
| R2 fija el reposo cuando Q7 está abierto y el pin 8 flota. Con Q7 saturado, R2 no baja el nivel del pin 8 | [E-Atari][T] |
| GPIO36 es solo de entrada. Máximo absoluto de una entrada: **VDD + 0,3 V ≈ 3,6 V** | [D] |
| La FN32ROV-1.6 pasa MOTOR por U3 (74LS07, puerta 2), con R24 2K a GND del lado Atari y R2 10K a 3V3 del lado ESP32 | [E], `proteccion-motor-gpio36.md` §2.1 |

**Conclusión de E1:** con el motor encendido, la 1.3 entrega a GPIO36 un nivel previsto mayor que su máximo absoluto.
Es una previsión [T], no una medición. **El nivel real y sus picos siguen sin medir [P].** Los mide M3a.

## 3. E2: riesgo de la placa frente al comportamiento del firmware

| Aspecto | Placa FN32ROV-1.3-Q24 | Firmware original de FujiNet | RetroBridge |
|---|---|---|---|
| ¿Crea la exposición de IO36? | **Sí.** Existe siempre que la placa esté conectada al SIO y el Atari encienda MOTOR [E][T] | No [I] | No [I] |
| ¿Puede reducirla? | Solo con un cambio de hardware o un adaptador (§5 de `proteccion-motor-gpio36.md`) | No [I] | No [I] |
| Qué sabemos de su uso | — | Funcionó con MOTOR activo 15 min 27 s (G5-2 T5), sin medición [H] | No configura GPIO36 mientras M3a no esté firmada: `EspMotorIn::configure()` sale antes de `gpio_config` [C] |
| Qué no sabemos | El nivel, los picos y la corriente reales [P] | Su código no se consultó como fuente (ADR-012, consecuencia de la enmienda 1) | — |

**Razones de la columna «firmware»:**
- GPIO36 es solo de entrada [D]. Un firmware puede leerlo o no leerlo. No puede hacer que el pin reciba menos tensión.
- El recorrido de MOTOR no pasa por ningún componente que el firmware controle [E].
- Por eso el riesgo es el mismo con el firmware original, con RetroBridge o con la ESP32 sin firmware.
- Con la placa apagada y MOTOR en alto, la entrada podría alimentar parcialmente la ESP32 [I]
  (`h4-puerta-sio.md` §3.4). Tampoco depende del firmware.

**Consecuencia:** M3a no es una puerta del firmware. Es una puerta de **conexión**. Mientras no se cierre, la regla
de `h4-puerta-sio.md` §3.4 sigue: no se conecta la placa al Atari para leer MOTOR. El código de RetroBridge que no
toca GPIO36 cumple la política del proyecto, pero no reduce la exposición eléctrica.

**Hallazgo para el responsable (§7, D3):** el uso actual de la 1.3 conectada al 65XE (menú SIO, disco) no lee
MOTOR. Pero la exposición existe igual si el Atari enciende MOTOR, por ejemplo al intentar una carga de casete
(START, o START + OPTION) [I]. Hoy solo la evita que nadie haga esa operación. No está escrito como regla.

## 4. E3: hipótesis no verificadas

Las siguientes explicaciones de por qué la placa sobrevive son **hipótesis [P]**. No se usan para aprobar nada.

| # | Hipótesis | Por qué no está verificada | Qué haría falta |
|---|---|---|---|
| P1 | IO36 tiene diodos de protección (ESD) hacia el riel de 3,3 V, y conducen cuando MOTOR está en alto | La hoja de datos [D] da el máximo absoluto y la tolerancia ESD, pero **no describe la estructura interna del pin** ni la corriente que admite | Una fuente del fabricante que la describa, o medir la corriente en IO36 con una protección externa ya montada |
| P2 | Esos diodos limitan IO36 a **≈ 3,9 V** (3,3 V + caída de un diodo) | Depende de P1, de la caída real y de la corriente | Medir IO36 con la placa conectada. **Solo después de M3a** y con una protección aprobada |
| P3 | La ESP32 absorbe la corriente que entra al riel de 3,3 V con su propio consumo, así que el riel no sube | Depende de P1, del consumo en cada momento y del regulador | Medir el riel de 3V3 con MOTOR encendido. Mismas condiciones que P2 |

**Reglas para estas hipótesis:**
- No cambian el estado de M3a, de H4 ni de R1.
- Aunque las tres fueran ciertas, IO36 seguiría fuera de su máximo absoluto [D]. Trabajar fuera de ese máximo puede
  degradar el pin sin aviso [I].
- **No se usa el tacto ni la temperatura de la ESP32 como indicador de sobretensión.** No detecta la sobretensión ni
  la degradación, y tocar la placa con el Atari encendido agrega riesgo de descarga electrostática (R10). Solo
  vale una medición con un instrumento adecuado (`ficha-medicion-m3a-motor.md`).

## 5. E4: M3a sigue pendiente

M3a se cierra **solo** por una de estas dos vías:

1. **Medición:** `ficha-medicion-m3a-motor.md`, en el Atari solo, con la FujiNet desconectada, con los picos
   incluidos, y el registro en `docs/mediciones/`.
2. **Protección validada:** una alternativa de `proteccion-motor-gpio36.md` §4 (la propuesta es la A) aprobada por el
   responsable y validada por etapas (§8 de ese documento). Su resultado también queda registrado en
   `docs/mediciones/`.

Ninguna de las dos se cumple hoy. Ni la evidencia histórica [H] ni las hipótesis de §4 sustituyen a ninguna.

## 6. E5: H4 y DATA IN siguen pendientes

- DATA IN va de GPIO21 a U4 (74LS07, colector abierto) y al pin 3 [E]. Con ese diseño, la placa solo puede liberar
  la línea o llevarla a bajo [D].
- Eso es un **análisis de diseño**. **No aprueba DATA IN.** Para aprobarla faltan las mediciones y comprobaciones del
  checklist de H4 (`h4-puerta-sio.md` §7) o de H4-T (`h4-puertas-escucha-transmision.md`), entre ellas:
  - niveles en reposo con el Atari encendido (M3, M4);
  - VCC_BUF con carga, porque el 74LS07 puede quedar bajo 4,75 V (R7; M1);
  - el análisis de bloqueo del bus (R2): DATA IN liberada en reposo y tras cualquier error, comprobado en el equipo.
- H4 sigue **no aprobada**. La matriz de riesgos (`h4-puerta-sio.md` §5) no cambia.

## 7. Decisiones pendientes del responsable

| # | Decisión | Opciones | Efecto |
|---|---|---|---|
| D1 | Aprobar esta enmienda | Aprobar, rechazar o pedir cambios | Si se aprueba, §2–§6 se incorporan como texto vigente. Ninguna puerta cambia |
| D2 | Vía para cerrar M3a | Medición (conseguir un instrumento) o protección (alternativa A u otra de `proteccion-motor-gpio36.md`) | Define el siguiente trabajo de hardware. Cada medición necesita su autorización |
| D3 | Uso actual de la 1.3 conectada al 65XE, mientras M3a esté pendiente | (a) seguir sin cambios; (b) regla escrita: no iniciar cargas de casete con la FujiNet conectada; (c) usar el adaptador sin pin 8 (`mitigacion-c-motor-pin8.md`) | (b) y (c) convierten en regla lo que hoy es costumbre (§3) |
| D4 | Plan de validación eléctrica de DATA IN | Dentro de H4 completo, o dentro de H4-T si se aprueba la separación | Ordena las mediciones M1, M3 y M4 |
| D5 | Investigar P1–P3 | Sí, después de D2; o no | Solo aporta conocimiento. No cambia ninguna puerta |

## 8. Criterios de aceptación de esta enmienda

Cada criterio se comprueba leyendo el PR:

- [ ] **CA1.** §2 afirma que MOTOR es una señal nominal de unos 5 V y que la 1.3 la conecta directo a GPIO36. Cada
      fila tiene su fuente, y el nivel aparece como **no medido**.
- [ ] **CA2.** §3 separa en columnas la placa, el firmware original y RetroBridge, y dice qué no se sabe de cada uno.
- [ ] **CA3.** §4 marca los diodos ESD, el límite de 3,9 V y la absorción de corriente por la ESP32 como hipótesis
      **[P]**, con lo que haría falta para verificarlas.
- [ ] **CA4.** M3a, H4 y DATA IN siguen sin aprobar. El diff del PR **no toca** `docs/mediciones/puertas.json`, el
      perfil de `rb_board`, `kTapeOutput` ni ningún archivo de código.
- [ ] **CA5.** Ningún documento propone tocar la ESP32 ni usar su temperatura como prueba. §4 lo prohíbe de forma
      explícita.
- [ ] **CA6.** No se cita código de FujiNet como fuente (ADR-012). Las fuentes son las de `docs/fuentes.md`.
- [ ] **CA7.** §7 lista las decisiones pendientes del responsable, con sus opciones.

## 9. Fuentes

Todas ya están registradas en `docs/fuentes.md`. No hay fuentes nuevas.
- `fujinet-hardware`, commit `0e539b8`: esquema y BOM de la FN32ROV-1.3-Q24; esquema y README de la FN32ROV-1.6.
- Texas Instruments, *SN74LS07 Hex Buffers and Drivers With Open-Collector High-Voltage Outputs*.
- Espressif, *ESP32 Series Datasheet* v5.3: máximos absolutos y características DC.
- Atari, *Home Computer Technical Reference Notes* (C016555), §9.
- Esquemas de terceros del 65XE (hojas 5 y 9) y de la XC12 (Jerzy Sobola). No se versionan.
- Proyecto WSS, informe G5-2 §4 (MOTOR activo 15 min 27 s).

No se consultó código de FujiNet ni de emuladores para esta enmienda (ADR-012).
