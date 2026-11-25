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
- [x] Revisión 1, H1 (punto físico no identificado) — prueba: ficha convertida en guía preparatoria; §3 «NO
  IDENTIFICADO»; §4 condiciones; detención antes del paso 0; 0, A2, B2 y B4 «no habilitado».
- [x] Revisión 1, H2 (exceso de certeza) — prueba: §7 con «compatible con» / «no demuestra»; paso 0 «no
  concluyente» sin inferir resistencias intercambiadas.
- [x] Revisión 1, nota de seguridad — prueba: §5, GND en pin 4/6 (USB solo con continuidad comprobada); tensión
  continua antes del USB; nunca resistencia con la placa alimentada.

## Next step

Revisión del PR. Para medir GPIO35 hay que cumplir antes el §4 de la ficha (punto físico identificado en la
revisión exacta y en la placa real) y tener autorización explícita del responsable.
