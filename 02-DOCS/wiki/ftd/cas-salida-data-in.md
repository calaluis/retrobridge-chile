---
author: Calaluis
branch: feat/cas-salida-data-in
status: in-review
---
# La cinta sale por DATA IN en menu-sio (T9b), por aceptación de riesgo

## Intent

Que la FN32ROV-1.3-Q24 cargue `.cas` en el 65XE: el casete de menu-sio conduce DATA IN (GPIO21), compartido con el
disco. El responsable aceptó R2, R6 y R7 sin medición. H4 sigue sin aprobar.

## Scope

- Perfil: campo `dataInRiskAccepted` (en `true` para la FN32ROV) y `validate_for_menu_cassette_output()`.
- `main/menu_sio.cpp`: `kTapeOutput = true`; `main/tape_gates.hpp` usa la concesión nueva.
- Empaquetador: puerta `DATA-IN` en el perfil y en `puertas.json`; la concesión solo en `main/menu_sio.cpp` y
  `main/tape_gates.hpp`.
- Registro: `docs/mediciones/aceptacion-data-in-casete-2026-11-26.md`.

Fuera:
- H4, `validate_for_sio()`, `validate_for_cassette_output()` y el paquete `cassette` (C107): sin cambios.
- El motor FSK de la beta (PR #122): sigue fuera de los paquetes. Suena `CassettePlayer`.
- Flashear, COM3 y la prueba en el 65XE: después, con aprobación.

## Checklist

- [x] Decisiones del responsable: «Aceptar riesgo DATA IN» y «CassettePlayer (actual)» — prueba: registro §1.
- [x] Rama `feat/cas-salida-data-in` desde `main` `7e1d321` — prueba: `git log`.
- [x] RED → GREEN en el host — prueba: `test_menu_cassette_output` (7, nueva), `test_tape_gates` (5);
  `test_cassette_output_assignment` (9) sin cambios.
- [x] Empaquetador — prueba: `test_firmware_package` (166), con 5 pruebas nuevas de `DATA-IN` y de la concesión.
- [x] Mutaciones — prueba: 3 de 3 caen (concesión fuera de menu-sio, aceptación ignorada, M3a omitida).
- [ ] Verificación completa y CI de la PR.
- [ ] Revisión independiente.
- [ ] Prueba física autorizada: flashear menu-sio y cargar un `.cas` en el 65XE.

## Next

Revisión independiente de la PR. No se afirma que un `.cas` cargue en el 65XE sin prueba física.
