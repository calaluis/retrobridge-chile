# DATA-IN para el casete de menu-sio, por aceptación de riesgo; H4 sigue pendiente (2026-11-26)

> **Aceptación de riesgo del responsable. No es una validación eléctrica.**
>
> - **DATA-IN** queda aceptada en `docs/mediciones/puertas.json` y en el perfil `fujinet_atari_v1x`
>   (`dataInRiskAccepted`), para la FujiNet FN32ROV-1.3-Q24 del responsable con su 65XE.
> - Cubre **solo** que el casete de **menu-sio** conduzca DATA IN (GPIO21), compartido con el disco.
>   Solo `validate_for_menu_cassette_output()` la lee, y solo `main/menu_sio.cpp` y `main/tape_gates.hpp` pueden
>   llamarla (lo comprueba el empaquetador).
> - Riesgos aceptados: **R2** (bus bloqueado si DATA IN queda en bajo), **R6** (carga del pin 10) y **R7**, sin
>   medición. R1 ya estaba aceptado (`docs/mediciones/puertas-aceptacion-riesgo-2026-11-25.md`).
> - **H4 sigue sin aprobar.** El checklist de `docs/hardware/h4-puerta-sio.md` §7 **no se cumplió**.
>   `validate_for_sio()` y `validate_for_cassette_output()` siguen cerradas: el paquete `cassette` (C107) no cambia.

## 1. Decisión del responsable

Del 2026-11-26, en la sesión de trabajo con el asistente.

- Pedido: «Necesito que FN32ROV-1.3-Q24 cargue o lea archivos .cas».
- A la pregunta de cómo abrir DATA IN con H4 pendiente, eligió **«Aceptar riesgo DATA IN»**: registrar la
  aceptación de R2, R6 y R7 solo para el casete en menu-sio, como se hizo con MOTOR el 2026-11-25.
- A la pregunta de con qué motor suena la cinta, eligió **«CassettePlayer (actual)»**. El motor FSK de la beta
  (PR #122) sigue fuera de los paquetes.

## 2. Qué cambia y qué no

| Puerta | Estado | Alcance |
|---|---|---|
| H4 (SIO, DATA IN en GPIO21) | **Pendiente** | Sin cambios |
| M3a (MOTOR en GPIO36) | Aprobada por aceptación de R1 | Sin cambios |
| DATA-IN (casete de menu-sio) | **Aceptada por riesgo** | R2, R6, R7; solo menu-sio |

- `kTapeOutput = true` en `main/menu_sio.cpp`: el árbitro presta DATA IN a la cinta (T9b).
- El banco C8a no usa esta concesión; su paquete solo acepta el registro tal como queda.

## 3. Lo que no se sabe

- Nada se midió. Con USB, el +5 V del SIO se midió en unos 0,87 V (`docs/mediciones/sio5v-usb-serie-2026-11-25.md`).
- La carga de un `.cas` en el 65XE no está probada. Se prueba con el paquete menu-sio, con aprobación para
  flashear.
