---
author: Calaluis
branch: analisis/h4-r2-r7
status: in-review
---
# Análisis de R2 y R7 (H4) y banco C8a aislado

## Intent

Avanzar técnicamente en R2 y R7 sin hardware: diagnóstico independiente, matriz de evidencia, pruebas de host
donde haya brechas reales, y la especificación del banco C8a compatible con `(H4 = false, M3a = true)`. R2, R7 y
H4 siguen abiertos.

## Scope

Dentro:
- `docs/hardware/analisis-h4-r2-r7.md`: diagnóstico de R2 y R7, matriz, propuesta R7-a/b/c, riesgos residuales.
- `docs/hardware/banco-c8a-aislado.md`: especificación y plan por fases del banco (propuesta no aprobada).
- `EspDataIn::release()` (modo propio): no informa «liberado» si `gpio_reset_pin()` falla; prueba de host nueva.

Fuera:
- `puertas.json`, aprobaciones del perfil, `kTapeOutput`, el alcance de R6 y la NVS.
- Variante, imagen o job de banco; flasheo, conexiones y mediciones.

## Checklist

- [x] `main` contiene los PR #109–#112 — prueba: `gh pr view` (MERGED) y `git merge-base --is-ancestor`.
- [x] Sin trabajo equivalente en `main` — prueba: no existía análisis de R2/R7 ni especificación de banco con
  `gates=(False, True)`; `procedimiento-prueba-banco-c8.md` solo describe la recuperación.
- [x] Brecha de R2 con prueba RED→GREEN — prueba: `test_release_with_a_failed_reset_is_not_released`, RED en la
  línea 274 antes del cambio, GREEN después.
- [x] Host completo en verde — prueba: `ctest` 100/100.
- [x] `tools/tests`, `check_notices` y `check_layering` en verde — prueba: salida en el PR.
- [x] Primera revisión (`6063860986`): H1, recuperación sin UART2 tras un `release()` incompleto (estado
  `uartDeleted_`, 4 pruebas RED→GREEN); H2, la matriz distingue el estado del driver del nivel eléctrico — prueba:
  `ctest` 100/100 y `analisis-h4-r2-r7.md` §1.2 y §3.
- [ ] CI del PR en verde.
- [ ] Revisión independiente final.

## Evidence

- Rama `analisis/h4-r2-r7` desde `main` `36c4d43`.

## Next

Decisiones del responsable: fase F1 del banco (enclavamiento SIO_5V y arranque manual); si la aceptación de R1
basta como «C5 resuelto» para C8b; y si autoriza R7-a (M1).
