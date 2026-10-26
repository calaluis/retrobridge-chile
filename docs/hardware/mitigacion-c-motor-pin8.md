# Mitigación C de R1: adaptador SIO sin el pin 8 (MOTOR) para la FujiNet FN32ROV-1.3-Q24

> **PROPUESTA NO APROBADA.** Especificación documental de la alternativa C para el riesgo R1 (`h4-puerta-sio.md`
> §3.4). **No autoriza** a fabricar, modificar ni conectar nada. Mientras no se apruebe formalmente y se actualice
> el procedimiento de medición:
> - **M3a sigue pendiente** y es el punto de detención vigente;
> - **`sioGateApproved = false`** en el perfil de la placa;
> - **H4 no está aprobado.**
>
> Validar esta mitigación resolvería **solo R1**. La aprobación global de H4 es una decisión aparte (§8).

**Etiquetas:**
- **[E]** esquema oficial `FN32ROV-1.3-Q24_schematic.jpg` (`fujinet-hardware`, commit `0e539b8`);
- **[E-Atari]** esquema de terceros del Atari 65XE (Jerzy Sobola, 2003) o de la XC12 (2002);
- **[M]** manual de Atari C016555, §9;
- **[H]** registro histórico del equipo (proyecto WSS);
- **[I]** inferencia;
- **[P]** pendiente de verificación o medición.

## 1. Qué hace y qué no hace

- **Hace:** interrumpe **solo** el pin 8 (MOTOR CONTROL) entre el Atari y J1 de la FujiNet. Así IO36 queda únicamente
  con R2 de 10 k a GND, en bajo, y desaparece la exposición de R1: la tensión prevista de MOTOR (≈ 4,7–4,9 V, no
  medida) y la posible alimentación parásita por ese pin.
- **No hace:** no limita, no adapta y no mide ninguna otra línea. Las otras 12 deben pasar sin cambios.
- **No sustituye** a las demás etapas de H4 (§8).

## 2. Pinout del conector SIO (13 contactos)

Numeración eléctrica según el esquema oficial de la 1.3 (J1 «SIO Thru Plug» y J2 «SIO Thru Receptacle») [E],
contrastada con el conector J1 del esquema del 65XE [E-Atari] y con `h4-puerta-sio.md` §2. Los nombres siguen el
punto de vista del Atari.

| Pin | Señal | En la FN32ROV-1.3 [E] | En el 65XE [E-Atari] | En el adaptador |
|---|---|---|---|---|
| 1 | CLOCK IN (hacia el Atari) | SIO_CKIN | CLK IN | **Continuidad** |
| 2 | CLOCK OUT (del Atari) | SIO_CKOUT | CLK OUT | **Continuidad** |
| 3 | DATA IN (hacia el Atari) | SIO_DATAIN | DATA IN | **Continuidad** |
| 4 | GND | GND | GND | **Continuidad** |
| 5 | DATA OUT (del Atari) | SIO_DATAOUT | DATA OUT | **Continuidad** |
| 6 | GND | GND | GND | **Continuidad** |
| 7 | COMMAND− | SIO_CMD | COMMAND | **Continuidad** |
| 8 | **MOTOR CONTROL** | SIO_MCTL → IO36 (y J2, pin 8) | MTR CTRL (colector de Q7) | **ABIERTO** |
| 9 | PROCEED− | SIO_PROC | PROCEED | **Continuidad** |
| 10 | +5V / READY | SIO_5V → D5 → VCC, y divisor a IO35 | +5 V por L10 (READY) | **Continuidad** |
| 11 | AUDIO IN | SIO_AUDIOIN | SIOAUDIO | **Continuidad** |
| 12 | +12 V | SIO_12V (solo de J1 a J2) | sin conectar en el 65XE | **Continuidad** |
| 13 | INTERRUPT− | SIO_INT | INTRUPT | **Continuidad** |

**Reglas:**
- **No se omite ningún otro contacto.** Las dos masas (4 y 6), el +5 V del pin 10 y el +12 V del pin 12 pasan
  aunque el 65XE no use el pin 12: el adaptador no debe depender del modelo de Atari.
- Si el cable o los conectores tienen malla o carcasa metálica, se conserva su continuidad tal como venga. No se
  añaden uniones nuevas.
- **Vistas del conector:** la disposición física de los 13 contactos (dos filas) se ve **invertida** según se mire el
  conector macho o el hembra, o el frente o la parte posterior. Esta especificación **no fija posiciones físicas**
  [P]: la identificación de cada contacto se hará **eléctricamente** (§5.3), nunca solo por su posición visual. Si
  se documenta un dibujo, debe indicar la vista y el tipo de conector, y contrastarse con la figura de conexiones
  del manual de Atari (C016555 §9) [M].

## 3. Topología obligatoria

```
Atari 65XE ─ [otros equipos con paso, p. ej. 1050] ─ ADAPTADOR (sin pin 8) ─ J1 FujiNet   J2: LIBRE
```

- **La FujiNet va al final de la cadena y J2 queda libre.** J1 pin 8 y J2 pin 8 son la **misma red** `SIO_MCTL`,
  igual que R2 e IO36 [E]. Un adaptador antes de J1 también corta MOTOR hacia J2, así que ningún equipo conectado a
  J2 recibiría MOTOR.
- Los equipos **antes** del adaptador conservan MOTOR [I].
- **La XC12 tiene un solo conector SIO** [E-Atari]: tiene que ir al final de la cadena, así que **no puede convivir**
  con este montaje. Para usarla hay que retirar el adaptador y la FujiNet de la cadena.
- Si la 1010 tiene conector de paso está **sin verificar** [P]; hasta confirmarlo, no se plantea en esta cadena.

## 4. Pérdida temporal de funciones

Mientras el adaptador esté puesto:

| Función | Efecto |
|---|---|
| RetroBridge v1 | **Ninguno.** No usa MOTOR: `SioPins` no lo incluye y H5 no lo configura (§3.4 y diseño de H5 §7) |
| Emulación de casete del firmware FujiNet original (por ejemplo, al restaurar los respaldos D5/D6), incluidas las cargas de CAS de tipo turbo | **Se pierde.** Ese firmware detecta MOTOR en IO36 para empezar la reproducción: en G5-2 T5 apareció «MOTOR ON: activating cassette» al cargar `turbo_software_zorro.cas` [H]. Con el pin 8 abierto, IO36 queda en bajo y el casete no arrancaría [I] |
| Casete real después de la FujiNet (J2) | **Se pierde**: J2 tiene que quedar libre (§3) |
| Disco por SIO, incluida la alta velocidad | Sin efecto previsto: no usa MOTOR [I] |

**La pérdida es reversible:** basta con retirar el adaptador. **Riesgo de uso:** volver al firmware FujiNet sin
retirar el adaptador dejaría sin casete, sin daño. Al revés, retirarlo y conectar la FujiNet de nuevo **reintroduce
R1**. Por eso hace falta un rótulo visible (§5.1).

## 5. Verificación previa del adaptador (con multímetro, sin energía)

Se hace con el adaptador **solo**, sin conectarlo a ningún equipo. Cada resultado se registra en `docs/mediciones/`
con fecha, multímetro (modelo) y fotos, sin datos privados.

### 5.1 Inspección mecánica y rótulo

- Contactos rectos, completos y bien asentados; ninguno doblado, hundido o flojo. El contacto del pin 8 está
  **ausente o aislado** de forma que no pueda tocar ningún otro.
- Retención firme en los dos extremos; alivio de tensión en el cable, si lo hay.
- Rótulo visible en el cuerpo del adaptador, por ejemplo: «SIN PIN 8 (MOTOR) — SOLO FUJINET AL FINAL — J2 LIBRE».
- **Fotos:** los dos extremos de frente, con la vista indicada, y el rótulo.

### 5.2 Mediciones

| Prueba | Criterio de aceptación | Detener si |
|---|---|---|
| Resistencia de las puntas juntas | Registrar (se resta en las demás lecturas) | — |
| **Continuidad de extremo a extremo**, pines 1–7 y 9–13 | Continuidad en los 12. Registrar cada valor; propuesta: ≤ 1 Ω, descontadas las puntas [I] | Alguno abierto o con resistencia anómala |
| **Pin 8 de extremo a extremo** | **Abierto** (sin continuidad, «OL») | Cualquier continuidad |
| **Pin 8 contra cada uno de los otros 12**, en cada extremo | Abierto en todos | Cualquier continuidad |
| **Cortocircuitos**: cada pin contra los demás en un extremo (78 pares) | Sin continuidad entre señales distintas | Cualquier corto |
| Pines 4 y 6 (las dos masas) entre sí, dentro del adaptador | Registrar. Si el cable las une, se documenta y se acepta solo si coincide con un cable SIO estándar [I] | Unión no explicada |

### 5.3 Identificación eléctrica (paso posterior, con autorización aparte)

Para descartar una confusión de vistas, una vez superado §5.2 y **con autorización**, se conectaría el adaptador a la
FujiNet **desenergizada** (sin USB, sin Atari, S5 apagado) y se mediría desde el extremo libre:
- pines 4 y 6 contra la masa de la placa: continuidad;
- **pin 8 contra IO36** (módulo, pin 4): **abierto**;
- pin 7 contra U3 pin 1, pin 5 contra U4 pin 9, pin 3 contra U4 pin 6, pin 2 contra U4 pin 3: continuidad (rutas de
  M0 en `procedimiento-medicion-pasiva-sio.md`).

Este paso se superpone en parte con M0 y puede hacerse junto con esa etapa.

## 6. Efecto sobre la alimentación y el aislamiento de la FujiNet

| Aspecto | Efecto del adaptador |
|---|---|
| Pin 10 (+5 V del SIO) → D5 → VCC; USB → D4 → VCC | **Sin cambios**: el pin 10 pasa. La regla de alimentar la placa siempre por USB en H4/H5 (R6) se mantiene [I] |
| Q2/Q3 y VCC_BUF (aislamiento de los 74LS07 con S5 apagado) | **Sin cambios**: no dependen del pin 8 [E] |
| Líneas que pasan por los 74LS07 (1, 2, 3, 5, 7, 9, 13) | **Sin cambios** |
| IO36 | Queda solo con R2 a GND: **≈ 0 V** con o sin Atari [E][I] |
| Alimentación parásita por el pin 8 | **Eliminada** [I] |
| Otras posibles vías de alimentación parásita (por ejemplo, entradas del 74LS07 sin alimentar, R12) | **Sin cambios**: fuera del alcance de R1 |
| Resistencia de contacto y longitud añadidas | Efecto previsto despreciable a las velocidades del SIO [I]; sin verificar |

## 7. Relación con la alternativa B

La alternativa B (acondicionar MOTOR: resistencia en serie, divisor, o buffer como en la FN32ROV-1.6 según su README)
**se conserva como opción futura**. Es la que permitiría mantener las funciones de casete con la FujiNet. C no la
descarta: es una medida reversible para el uso de RetroBridge v1.

## 8. Validación de R1 frente a la aprobación de H4

**Para dar R1 por mitigado con C (cuando se apruebe):**
1. aprobación formal de esta especificación;
2. actualización de `procedimiento-medicion-pasiva-sio.md`: dividir M3a en (a) **nivel de MOTOR**, que la
   mitigación C validada sustituiría, y (b) **pin 10 y reposo de los pines 3, 5 y 7**, que siguen siendo
   obligatorios; y que M3–M6 exijan «M3a aprobada **o** mitigación C validada, más M3a(b)»;
3. §5.2 superada y registrada; §5.3 y M0 con IO36 a GND por R2;
4. actualización de §3.4, R1 y el checklist de `h4-puerta-sio.md`.

**Estado de M0–M6 con C validada (previsto):**

| Etapa | Estado | Instrumento |
|---|---|---|
| M0 | Pendiente | Multímetro |
| M1, M2 | Pendientes | Multímetro |
| M3a(a) nivel de MOTOR | Sustituida por C, una vez aprobada y validada | Multímetro (§5) |
| M3a(b) pin 10 y reposo de 3, 5 y 7 | Pendiente | Multímetro |
| M3, M4 | Pendientes (con el adaptador puesto; IO36 esperado ≈ 0 V) | Multímetro |
| **M5** | **Pendiente** | **Osciloscopio o analizador con tolerancia a 5 V confirmada** |
| M6 | Pendiente | Amperímetro o cable de prueba |

**H4 seguiría sin poder aprobarse** mientras falte M5, que necesita un instrumento que hoy no hay, y el resto del
checklist. `sioGateApproved` solo cambia con H4 aprobado y en un PR específico.

## 9. Incertidumbres

- El nivel real de MOTOR del 65XE y el circuito del 800XL (sin esquema revisado).
- Si la 1010 tiene conector de paso.
- La disponibilidad y calidad de los conectores o cables SIO para fabricar el adaptador.
- El efecto real de la resistencia de contacto añadida, no medido.
- La disposición física de los contactos: se resuelve eléctricamente (§5.3).

## 10. Fuentes

- `fujinet-hardware`, commit `0e539b8`: esquema de la FN32ROV-1.3-Q24 (J1, J2, R2, U1) y README de la FN32ROV-1.6
  (ver `docs/fuentes.md`, entrada del 2026-10-26).
- Esquemas de terceros del Atari 65XE y de la XC12 (`docs/fuentes.md`, 2026-10-26).
- Atari C016555 §9 (conector y especificaciones eléctricas).
- `h4-puerta-sio.md` §2 y §3.4, `procedimiento-medicion-pasiva-sio.md` y diseño de H5 §1 y §7.
- Registro histórico WSS G5-2 T5 (privado, no versionado).

No se consultó código de FujiNet (ADR-012).
