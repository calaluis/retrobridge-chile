# M3a aprobada por aceptación de riesgo; H4 sigue pendiente (2026-11-25)

> **Aceptación de riesgo del responsable. No es una validación eléctrica.**
>
> - **M3a** queda aprobada en `docs/mediciones/puertas.json` y en el perfil `fujinet_atari_v1x`, para la FujiNet
>   FN32ROV-1.3-Q24 del responsable con su 65XE. Cubre **solo R1**.
> - **H4 sigue sin aprobar.** R2, R6 y R7 **no** están aceptados.
>   **Actualización del mismo día (`docs/mediciones/riesgos-h4-2026-11-25.md`):** R2 y R7 siguen sin aceptar; R6 queda aceptado solo con la FujiNet
>   por USB. H4 sigue sin aprobar.
> - La ficha M3a y el checklist de H4 (`docs/hardware/h4-puerta-sio.md` §7) **no se cumplieron**. Los puntos sin
>   medir siguen sin medir.
> - Especificación: `02-DOCS/wiki/sdd/specs/puertas-aceptacion-riesgo.md`.

## 1. Decisiones del responsable, citadas al pie de la letra

Todas del 2026-11-25, en la sesión de trabajo con el asistente.

1. Al elegir la opción C para MOTOR:
   > «Desde que se compró el FujiNet FN32ROV-1.3, ha funcionado tal cual con el pin 8 MOTOR al GPIO36 con el
   > firmware de fujinet. Por eso se acepta.»
2. Al responder la primera revisión independiente del PR #110 (hallazgo H1), sobre el alcance:
   > «Acepto R1 como riesgo eléctrico preexistente de mi FujiNet original. No autorizo automáticamente R2, R6 ni
   > R7. Mantener H4 pendiente hasta evaluar esos riesgos por separado. La aceptación de R1 no constituye validación
   > eléctrica.»
3. Sobre qué firmware se instala antes del banco T9b (hallazgo H2), el responsable eligió: **«Solo menu-sio»**.

El responsable también rechazó agregar hardware de protección (camino A, adaptador con Rs) y hacer más mediciones.

## 2. Qué queda aprobado y qué no

| Puerta | Estado | Riesgos | Base |
|---|---|---|---|
| **M3a** (MOTOR, GPIO36) | **Aprobada por aceptación de riesgo** | R1 aceptado | Decisiones 1 y 2 |
| **H4** (SIO, DATA IN en GPIO21) | **Pendiente** | R2, R6 y R7 **no** aceptados | Decisión 2 |

## 3. Qué firmware se puede instalar antes de T9b

Decisión 3: **solo `menu-sio`**.

| Firmware | Qué hace con estas puertas | ¿Se instala antes de T9b? |
|---|---|---|
| `menu-sio` | Lee MOTOR en GPIO36 como entrada (M3a). La cinta no sale por el SIO: `kTapeOutput` es `false`, y además `validate_for_cassette_output()` exige H4 | **Sí** |
| `cassette` (C107) | Con las dos puertas, configuraría DATA IN y MOTOR y transmitiría la cinta, sin depender de `kTapeOutput`. Con H4 pendiente no configura DATA IN, y el empaquetador se niega a generarlo | **No**: no hay job de CI que lo genere |
| `cassette-lectura`, `bench-c8` | Exigían las dos puertas sin aprobar | **No**: retirados de la CI |

**Punto abierto para el responsable.** `menu-sio` responde como D1 por DATA IN (GPIO21), con H4 sin aprobar, igual
que antes de este cambio. Eso expone R2, R6 y R7, que la decisión 2 **no** acepta:
- la decisión 3 elige qué firmware se instala; **no** es una aceptación de R2, R6 ni R7;
- D3 (`docs/hardware/enmienda-motor-gpio36.md` §7) solo trata MOTOR y deja DATA IN «regido por sus propias puertas
  y paquetes»;
- la aceptación por sesión de `docs/mediciones/menu-sio-fisico-registro.md` §1 sigue **sin firmar**.

Este registro no amplía el uso de DATA IN. Qué cubre el uso conectado de `menu-sio` lo decide el responsable aparte.

## 4. Lo que se sabe y lo que no

**Medido** (`docs/mediciones/m3a-motor-2026-11-25.md`):
- MOTOR con el Atari solo: 4,89 V, por encima de V_IH máx (≈ 3,6 V). Según la ficha M3a, el resultado es
  «Detener».
- Con la FujiNet conectada, en su conector posterior y sin USB: 4,87 V con S5 encendido y 3,16 V con S5 apagado.

**No medido:**
- Picos de MOTOR, riel 3V3 y corriente en IO36.
- M0–M6 de `procedimiento-medicion-pasiva-sio.md`.

**Evidencia histórica [H]:** G5-2 T5, con MOTOR activo durante 15 min 27 s y la placa funcionando después.

**Riesgo aceptado** (`h4-puerta-sio.md` §5): **R1**, sobretensión en GPIO36 y posible alimentación parásita.

**Riesgos no aceptados** (H4 pendiente): R2 (bus bloqueado si DATA IN queda en bajo), R6 (carga del pin 10) y R7
(VCC_BUF posiblemente bajo 4,75 V).

## 5. Alcance

- Vale solo para la placa y el Atari del responsable descritos arriba. No es una aprobación general de la
  FN32ROV-1.3.
- Habilita en el perfil solo la lectura de MOTOR (`validate_for_motor()`). DATA IN sigue sin concederse.
- `kTapeOutput` sigue en `false` hasta el banco T9b, en un PR aparte.
- Para MOTOR, reemplaza la excepción operativa D3, que estaba acotada a disco, menú y TNFS. Para DATA IN no cambia
  nada.
