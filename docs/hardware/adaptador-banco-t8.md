# Adaptador SIO reversible para el banco T8 de menu-cinta: pin 8 abierto y pin 10 con puente

> **DESCARTADA por el responsable el 2026-11-23:** la FujiNet comercial se usa tal como viene, sin adaptador
> («No hay nada que construir físicamente…»). Se conserva como referencia (por ejemplo, para T1b). Ver la aceptación
> del montaje en `02-DOCS/raw/2026-11-23-banco-menu-cinta-t8.md` §2.
>
> **PROPUESTA NO APROBADA.** Especificación documental para revisión del responsable. **No autoriza** a fabricar,
> modificar cables, conectar, energizar, flashear ni abrir COM3. Mientras no se apruebe:
> - **M3a sigue pendiente** y la mitigación C (`mitigacion-c-motor-pin8.md`) **no está aprobada**;
> - **H4 no está aprobado**; `sioGateApproved = false`;
> - T8 de `menu-cinta` sigue **parcial**, AC5 **bloqueado** por T1a/T1b y T9 **separado** hasta M3a/H4.

**Etiquetas:** [E] esquema oficial FN32ROV-1.3-Q24 (vía `fujinet-atari-v1x.md` y `mitigacion-c-motor-pin8.md`);
[C] consola USB; [P] pendiente de verificación; [I] inferencia.

## 1. Para qué

El banco T8 necesita la consola USB (evidencia [C]) con el 65XE conectado por SIO. Hoy la FujiNet va unida al 65XE
con los 13 pines. Eso deja dos exposiciones sin resolver:

| Riesgo | Hoy | Qué hace el adaptador |
|---|---|---|
| **Pin 10 (+5 V):** USB y Atari alimentan la placa a la vez | Las dos fuentes llegan a VCC por diodos Schottky (USB → D4, SIO_5V → D5) [E]. No están unidas directamente. El reparto de corriente entre ellas **no está medido** [P]; el pin 10 admite 50 mA como máximo | Un puente extraíble en el pin 10: abierto, la placa se alimenta solo por USB; cerrado, como hoy |
| **Pin 8 (MOTOR → IO36, R1):** IO36 recibe MOTOR del Atari (≈ 4,7–4,9 V, sin medir) directamente, con R2 de 10 k a GND [E] | Expuesto cada vez que el Atari activa MOTOR (por ejemplo, un arranque con START) | Pin 8 abierto: IO36 queda solo con R2, en ≈ 0 V |

## 2. Evidencia de partida

- [E] USB_5V y SIO_5V llegan a VCC mediante diodos Schottky PMEG2010 (D4 y D5). El divisor de IO35 mide SIO_5V
  **antes** de D5 (`fujinet-atari-v1x.md` §4 y §5; `mitigacion-c-motor-pin8.md` §2 y §6).
- [C] Banco T8, 2026-11-23, con USB conectado y el 65XE apagado: `SIO +5V (pin 10) 0 mV`. D5 no devuelve tensión
  del USB hacia el pin 10. Con el 65XE encendido: `4780 mV`, la tensión del Atari en su lado de D5.
- [C] `brownout=0` y `panic=0` en todos los arranques registrados. **No prueba** que el montaje sea seguro: no mide
  corriente.
- [I] Con dos fuentes en paralelo a través de diodos, aporta más la de mayor tensión. Si el USB baja (cable largo,
  puerto débil) por debajo de la tensión del Atari, el pin 10 puede aportar corriente. Sin medir.
- [C] El firmware `menu-sio` no configura IO36 sin M3a (`main/app_main.cpp`: `g_motor.configure` solo lo toma con la
  puerta). El riesgo R1 es eléctrico, no de firmware.

## 3. El adaptador

Un cable o adaptador SIO **de paso** (macho a un lado, hembra al otro) que se coloca entre el cable del 65XE y J1 de
la FujiNet. No se modifica la FujiNet ni el cable original: el adaptador se retira y todo queda como hoy.

| Pin | Señal | En el adaptador |
|---|---|---|
| 1 | CLOCK IN | Continuidad |
| 2 | CLOCK OUT | Continuidad |
| 3 | DATA IN | Continuidad |
| 4 | GND | Continuidad |
| 5 | DATA OUT | Continuidad |
| 6 | GND | Continuidad |
| 7 | COMMAND− | Continuidad |
| 8 | **MOTOR CONTROL** | **ABIERTO**, siempre (mitigación C) |
| 9 | PROCEED− | Continuidad |
| 10 | **+5 V / READY** | **Por un puente extraíble** (dos espadines con un jumper, o un interruptor rotulado) |
| 11 | AUDIO IN | Continuidad |
| 12 | +12 V | Continuidad |
| 13 | INTERRUPT− | Continuidad |

```text
65XE ── cable SIO ── [ADAPTADOR: pin 8 abierto · pin 10 por puente J10] ── J1 FujiNet     J2: LIBRE
                                                  │
                                    J10 abierto = solo USB (banco T8 con consola)
                                    J10 cerrado = como hoy (pin 10 pasa)
                                    J10 con amperímetro en serie = medición T1b
```

- **Un solo adaptador para tres usos:** banco con USB (J10 abierto), uso normal o AC5 tras T1a/T1b (J10 cerrado), y
  el punto de medida de corriente de T1b (el instrumento en los dos espadines de J10).
- **J10 solo se cambia con los dos equipos sin energía:** 65XE apagado, USB desconectado. Nunca se abre, se cierra ni
  se sustituye por un instrumento con algo encendido.
- **Un amperímetro convencional no basta para aprobar T1b.** El plan `menu-sio-wifi-tnfs` (§6, T1a/T1b) pide la
  corriente media **y los picos** de cada fase; los picos necesitan osciloscopio con una resistencia de medida en
  serie. Con solo un amperímetro, el resultado de T1b es **preliminar**.
- **Rótulo visible:** «SIN PIN 8 · J10 = PIN 10 · J2 LIBRE». Y junto a J10: «ABIERTO = SOLO USB».
- **Topología:** la de `mitigacion-c-motor-pin8.md` §3. La FujiNet va al final y J2 queda libre.

## 4. Consecuencias de usarlo

| Con J10 abierto (banco T8 con USB) | Efecto |
|---|---|
| Alimentación | Solo USB. El Atari no aporta corriente a la FujiNet |
| `SioPowerWatch` (lee el pin 10 por IO35) | **Queda ciego**: lee 0 mV siempre y cree que el 65XE está apagado. Apagar y encender el 65XE **no** devuelve D1 al menú ni registra `65XE on` |
| Volver al menú tras arrancar un juego (AC7b–d) | Con el botón A de la FujiNet o la orden de consola `menu` (ambos existen hoy) |
| AC5-USB | Sin cambios: el reinicio es con el botón de reset de la FujiNet |
| Árbitro de DATA IN | Ve «Atari apagado» siempre; con las puertas cerradas no cambia nada (AC8) |
| La comprobación «~0 mV con el 65XE encendido» del guion | Pasa a ser la prueba de que J10 está abierto |

| Pin 8 abierto (siempre) | Efecto |
|---|---|
| IO36 | ≈ 0 V por R2; desaparece R1 mientras el adaptador está puesto |
| RetroBridge `menu-sio` | Ninguno: sin M3a no lee MOTOR |
| T9 (cargar cintas) | **Incompatible**: T9 necesita MOTOR. El adaptador se retira, o se sustituye por la alternativa B, antes de T9 |
| Firmware FujiNet original (respaldos) | Pierde la casetera mientras el adaptador esté puesto; sin daño |

## 5. Comprobaciones con multímetro, sin energía

Se hacen con el adaptador **solo**, sin conectar a ningún equipo. Se registran en `docs/mediciones/` con fecha,
modelo del multímetro y fotos.

| Prueba | Criterio | Detener si |
|---|---|---|
| Puntas juntas | Registrar el valor (se resta) | — |
| Pines 1–7, 9, 11–13 de extremo a extremo | Continuidad en los 11 | Alguno abierto |
| Pin 8 de extremo a extremo | Abierto («OL») | Cualquier continuidad |
| Pin 8 contra cada uno de los otros 12, en cada extremo | Abierto | Cualquier continuidad |
| Pin 10 de extremo a extremo, **J10 abierto** | Abierto | Continuidad |
| Pin 10 de extremo a extremo, **J10 cerrado** | Continuidad | Abierto |
| Pin 10 contra los otros 12, en cada extremo, con J10 en las dos posiciones | Abierto | Cualquier continuidad |
| Cortocircuitos en un extremo: cada pin contra los demás (78 pares), **salvo el par 4–6** | Abierto en los 77 pares | Cualquier corto |
| Pines 4 y 6 (las dos masas) entre sí, en cada extremo | Registrar. Si el cable las une, se documenta y se acepta solo si coincide con un cable SIO estándar (`mitigacion-c-motor-pin8.md` §5.2) | Unión no explicada |

**Identificación eléctrica** (paso aparte, con autorización): adaptador conectado a la FujiNet **sin energía** (sin
USB, sin Atari, S5 apagado). Desde el extremo libre: pines 4 y 6 con continuidad a la masa de la placa; pin 8 abierto
contra IO36; con J10 abierto, pin 10 abierto contra el ánodo de D5 / la red SIO_5V; con J10 cerrado, continuidad.
Así se descarta la confusión de vistas del conector (espejo), que es como el pin 4 (GND) puede pasar por el 10.

## 6. Comprobación con energía, al empezar cada sesión del banco

1. J10 abierto. USB conectado; el 65XE apagado; abrir COM3 (con aprobación). La consola marca 0 mV en el pin 10.
2. Encender el 65XE sin pulsar teclas. **La consola debe seguir marcando ~0 mV.** Si marca ~4800 mV, el pin 10 está
   pasando: apagar el 65XE, desconectar el USB y revisar J10 y §5.
3. Solo entonces, las pruebas de T8.

## 7. Qué cambia en el guion T8 si se aprueba

- §2 paso 5 (pin 10 sin tensión) pasa a ser §5 de este documento, con J10 abierto.
- AC7b–d: volver al menú con el botón A de la FujiNet, no apagando el 65XE.
- AC5 sigue bloqueado: con J10 cerrado y sin USB es el montaje solo SIO, que espera a T1a/T1b.

## 8. Alternativas consideradas

| Opción | Por qué no es la recomendada |
|---|---|
| Seguir sin adaptador, apoyados en D4/D5 | Los diodos evitan la unión directa, pero el reparto de corriente del pin 10 no está medido, y R1 (pin 8) sigue expuesto |
| Adaptador solo sin pin 10 | Resuelve la alimentación pero no R1; hace falta otro adaptador para el pin 8 |
| Adaptador solo sin pin 8 (mitigación C tal cual) | Resuelve R1 pero no la alimentación doble del banco con USB |
| Cortar o modificar el cable original o la FujiNet | No es reversible |

## 9. Incertidumbres

- El reparto real de corriente entre USB y pin 10 con las dos fuentes conectadas [P]: J10 es el punto de medida de
  T1b; medias y picos según el plan (amperímetro y osciloscopio con resistencia de medida).
- El nivel real de MOTOR del 65XE [P].
- Disponibilidad de conectores SIO macho y hembra para fabricarlo [P].
- La disposición física de los contactos: se resuelve eléctricamente (§5), nunca solo a la vista.

No se consultó código de FujiNet (ADR-012).
