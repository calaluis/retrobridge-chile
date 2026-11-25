---
author: Calaluis
branch: docs/riesgos-h4-r2-r6-r7
status: in-review
---
# Riesgos de H4: R2, R6 y R7

## Intent

Registrar la decisión del responsable sobre los riesgos de H4: R2 y R7 no aceptados, y R6 aceptado solo con la
FujiNet alimentada por USB. R1 sigue aceptado (PR #110). H4 sigue sin aprobar.

## Scope

Dentro:
- `docs/mediciones/riesgos-h4-2026-11-25.md`: registro nuevo, con las palabras y las condiciones del responsable.
- `docs/hardware/h4-puerta-sio.md`: bloque «Estado vigente» y filas R2, R6 y R7 de §5.
- Bloques «Estado vigente» de la enmienda, `h4-c-casetera.md`, el procedimiento pasivo, la demo `menu-sio` y el banco C8.
- `docs/mediciones/puertas-aceptacion-riesgo-2026-11-25.md`: nota de actualización.
- `02-DOCS/wiki/sdd/specs/puertas-aceptacion-riesgo.md`: una línea en Clarifications.

Fuera:
- `docs/mediciones/puertas.json`, los perfiles de hardware, el firmware y la CI (condición 2).
- Conexiones físicas, pruebas, mediciones y fusiones.

## Checklist

- [x] Registro con la decisión al pie de la letra y las seis condiciones — prueba: `riesgos-h4-2026-11-25.md` §1.
- [x] R6 limitado a USB; la alimentación solo por SIO queda pendiente — prueba: registro §2 y fila R6 de `h4-puerta-sio.md` §5.
- [x] Las mediciones futuras no quedan descartadas — prueba: registro §3.
- [x] Trazabilidad de los PR #109 y #110 — prueba: registro §5.
- [x] Sin cambios en `puertas.json`, perfiles, firmware ni CI — prueba: `git diff --stat origin/main` solo muestra `.md`.
- [x] Los textos de código que siguen diciendo «R6 not accepted» quedan listados para un PR de código aparte — prueba: registro §4.
- [x] Primera revisión (`6063317612`): H1, M5 no cubre DATA IN (se remite a C8, C8a y C8b); H2, M1 es solo un
  diagnóstico sin carga (se remite a C9) — prueba: registro §2 y §3; filas R2 y R7 de `h4-puerta-sio.md` §5.
- [ ] Revisión independiente final.

## Evidence

- Rama `docs/riesgos-h4-r2-r6-r7` desde `main` `f6d3056`.
- Decisión: respuestas del responsable en la sesión del 2026-11-25 y su autorización escrita del PR.

## Next

PR de código aparte para corregir el aviso de `menu-sio` y los comentarios que dicen «R2, R6, R7 not accepted».
