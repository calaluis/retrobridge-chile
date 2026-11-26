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

- `validate_for_menu_cassette_output(profile, riskAccepted)`: la aceptación la pasa la compilación, no el perfil.
- Opción `CONFIG_RB_MENU_SIO_TAPE_OUTPUT` (apagada por defecto) y `sdkconfig.menu-sio-cinta`; `kTapeOutput` la sigue.
- `tapeGatesOpen()` exige además MOTOR y DATA IN configurados de verdad (fail closed).
- Empaquetador: puerta `DATA-IN` en `puertas.json`; variante `menu-sio-cinta`, la única con la opción; la
  concesión solo en `main/menu_sio.cpp` y `main/tape_gates.hpp`. CI: job `firmware-package-menu-sio-cinta`.
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
- [x] Revisión independiente, ronda 1 — NO FUSIONAR: H1 (aceptación en el perfil genérico) y H2 (el menú
  ofrecía la cinta sin MOTOR o DATA IN configurados).
- [x] H1 — prueba: `test_menu_cassette_output` (7) y `test_tape_gates` sin la aceptación de la compilación;
  empaquetador: menu-sio rechaza la opción, solo menu-sio-cinta la lleva (168 pruebas).
- [x] H2 — prueba: `test_tape_gates` (5): MOTOR sin configurar o DATA IN sin compartir dejan la cinta cerrada.
- [x] Mutaciones de la ronda 1 — prueba: 4 de 4 caen.
- [ ] Verificación completa y CI de la PR.
- [ ] Revisión independiente, ronda 2.
- [ ] Prueba física autorizada: flashear menu-sio-cinta y cargar un `.cas` en el 65XE.

## Next

Segunda revisión independiente de la PR. No se afirma que un `.cas` cargue en el 65XE sin prueba física.
