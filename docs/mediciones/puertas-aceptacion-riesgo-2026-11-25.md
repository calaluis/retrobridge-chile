# Puertas H4 y M3a aprobadas por aceptación de riesgo (2026-11-25)

> **Aceptación de riesgo del responsable. No es una validación eléctrica.**
>
> - H4 y M3a quedan aprobadas en `docs/mediciones/puertas.json` y en el perfil `fujinet_atari_v1x`, para la FujiNet
>   FN32ROV-1.3-Q24 del responsable con su 65XE.
> - El checklist de H4 (`docs/hardware/h4-puerta-sio.md` §7) y la ficha M3a **no se cumplieron**. Los puntos sin
>   medir siguen sin medir.
> - Especificación: `02-DOCS/wiki/sdd/specs/puertas-aceptacion-riesgo.md`.

## 1. Decisión

| Puerta | Decisión del responsable (2026-11-25) | Base |
|---|---|---|
| **M3a** (MOTOR, GPIO36) | Aprobada por aceptación de riesgo (opción C) | La FN32ROV-1.3-Q24 funciona desde que se compró con el pin 8 directo a GPIO36 y el firmware de FujiNet, sin daños físicos conocidos |
| **H4** (SIO, DATA IN en GPIO21) | Aprobada por aceptación de riesgo, con la misma base | El firmware de FujiNet manejó DATA IN en esa placa durante todo ese uso |

El responsable rechazó agregar hardware de protección (adaptador con Rs) y hacer más mediciones. La razón para no
dar más peso a las hipótesis de protección interna es que Espressif no describe cómo está hecha la entrada de GPIO36.

## 2. Lo que se sabe y lo que no

**Medido** (`docs/mediciones/m3a-motor-2026-11-25.md`):
- MOTOR con el Atari solo: 4,89 V, por encima de V_IH máx (≈ 3,6 V). Según la ficha M3a, el resultado es
  «Detener».
- Con la FujiNet conectada, en su conector posterior y sin USB: 4,87 V con S5 encendido y 3,16 V con S5 apagado.

**No medido:**
- Picos de MOTOR, riel 3V3 y corriente en IO36.
- M0–M6 de `procedimiento-medicion-pasiva-sio.md`.

**Evidencia histórica [H]:** G5-2 T5, con MOTOR activo durante 15 min 27 s y la placa funcionando después.

**Riesgos aceptados** (`h4-puerta-sio.md` §5):
- R1: sobretensión en GPIO36 y posible alimentación parásita;
- R2: bus bloqueado si DATA IN queda en bajo;
- R6: carga del pin 10;
- R7: VCC_BUF posiblemente bajo 4,75 V.

## 3. Alcance

- Vale solo para la placa y el Atari del responsable descritos arriba. No es una aprobación general de la
  FN32ROV-1.3.
- Habilita en el perfil la lectura de MOTOR y el manejo de DATA IN por la casetera y el disco.
- `kTapeOutput` sigue en `false` hasta el banco T9b, en un PR aparte.
- Se reemplaza la excepción operativa D3, que estaba acotada a disco, menú y TNFS
  (`docs/hardware/enmienda-motor-gpio36.md` §7).
