---
author: Calaluis
branch: docs/m3a-registro-motor
status: in-review
---
# Registro M3a parcial y decisión D3 (prohibición de conexión)

## Intent

Registrar la primera medición de MOTOR en el 65XE solo (B4 = 4,89 V, resultado «Detener») y dejar escrita la
decisión D3 del responsable: la FN32ROV-1.3-Q24 no se conecta al SIO del Atari hasta tener una protección validada.

## Scope

Dentro:
- `docs/mediciones/m3a-motor-2026-11-25.md`: registro de la medición, con la hoja de la ficha M3a.
- `docs/hardware/enmienda-motor-gpio36.md`: cabecera de enmienda aprobada; §7 con D1, D3 y el orden D2 → D4 → D5.
- `docs/hardware/h4-puerta-sio.md`: §1 y §3.4 con la medición y la decisión; nota de cabecera actualizada.
- `02-DOCS/wiki/ftd/docs-enmienda-motor-gpio36.md`: estado `done`.

Fuera:
- `docs/mediciones/puertas.json` (M3a y H4 siguen en `false`), el perfil de `rb_board`, `kTapeOutput` y el código.
- Nuevas mediciones, flasheos, GPIO y fusiones.

## Checklist

- [x] Registro con las condiciones informadas por el responsable: Atari solo, `POKE 54018,52`, tester — prueba:
      §2 del registro; lo no informado queda como [S] o [P].
- [x] Criterio de la ficha aplicado: 4,89 V > 3,6 V → «Detener» — prueba: §4 del registro.
- [x] D3 = prohibición de conexión, sin aprobar M3a — prueba: enmienda §7 y `h4-puerta-sio.md` §3.4.
- [x] Diff sin puertas, perfil ni código — prueba: `git diff --stat origin/main`.
- [x] Revisión independiente 1 (`db93d20`), H1: 4,89 V es sin carga; la tensión con 10 kΩ o con la FujiNet
      conectada queda como no medida — prueba: cabecera y §4 del registro; `h4-puerta-sio.md` §1 y §3.4; enmienda §7.
- [x] H2: A, D y E quedan pendientes; solo C es opcional, como dice la ficha §5 — prueba: §3 y §5 del registro.
- [x] Evidencia nueva del responsable: M-2 (S5 apagado, 3,16 V) y M-3 (S5 encendido, 4,87 V), con la FujiNet
      conectada, en su conector posterior y sin USB — prueba: §2 y §4 del registro; no se registran como medición C.
- [x] Hallazgo S5 con hipótesis HS1–HS4 no verificadas — prueba: §4 del registro.
- [x] R1 (`h4-puerta-sio.md` §5), §3.4, `proteccion-motor-gpio36.md` §1, P4 y §7, y la enmienda §4 actualizados,
      sin aprobar ninguna solución — prueba: diff del PR.
- [ ] Segunda revisión independiente.

## Evidence

- Rama `docs/m3a-registro-motor` desde `main` `d48b245`.
- Datos de la medición: informados por el responsable en la sesión del 2026-11-25.

## Next

D2: completar M3a con osciloscopio (picos) y diseñar una adaptación de nivel para MOTOR.
