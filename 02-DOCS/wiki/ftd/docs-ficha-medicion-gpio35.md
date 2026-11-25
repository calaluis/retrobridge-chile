---
author: Calaluis
branch: docs/ficha-medicion-gpio35
status: in-review
---
# Ficha de medición de GPIO35 (divisor de SIO +5V) en la FujiNet 1.3

## Intent

Dar al responsable una ficha para medir con multímetro GPIO35 y el pin 10 en la misma sesión, solo por USB. Es el
dato que falta para explicar la diferencia entre el ADC (≈675 mV), el valor calculado (≈1,02 V) y el pin 10 medido
(0,86–0,88 V), antes de decidir sobre el umbral de C-1 (`docs/mediciones/banco-c8a-2026-11-25.md` §8).

## Scope

Dentro:
- `docs/hardware/ficha-medicion-gpio35.md` (nueva).
- Enlace desde `docs/hardware/banco-c8a-aislado.md` y desde el registro de F4, §8.

Fuera:
- El umbral de C-1, el código, `puertas.json`, H4, R2, R7, flasheos y la propia medición.

## Checklist

- [x] Rama nueva desde `main` `095a9f1` (elegida por el responsable).
- [x] Ficha escrita: circuito del esquema, puntos de medida, seguridad, pasos 0/A/B/C, lectura del resultado y hoja
  — prueba: `docs/hardware/ficha-medicion-gpio35.md` §2–§8.
- [x] Enlaces desde el documento del banco y el registro de F4 — prueba: `banco-c8a-aislado.md` (Estado de F4) y
  registro §8.
- [x] `tools/tests` y `check_notices` OK — prueba: 320 OK; notices OK.

## Next step

Revisión del PR. La medición en la placa necesita autorización explícita del responsable.
