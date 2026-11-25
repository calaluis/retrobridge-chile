---
author: Calaluis
branch: fix/textos-riesgos-h4
status: in-review
---
# Textos de riesgos de H4 en el código

## Intent

Alinear el aviso de `menu-sio` y los comentarios de código con la decisión del PR #111: R2 y R7 no aceptados, R6
aceptado solo con USB. H4 sigue sin aprobar.

## Scope

Dentro:
- El aviso de consola de `main/menu_sio.cpp` (el prefijo que busca el empaquetador no cambia).
- Comentarios de `main/menu_sio.hpp`, `main/Kconfig.projbuild`, `sdkconfig.menu-sio` y `fujinet_atari_v1x.hpp`.
- Datos de prueba de `tools/tests/test_firmware_package.py`.
- La cita del aviso en `procedimiento-demo-menu-sio.md` §0 y una nota en `riesgos-h4-2026-11-25.md` §4.

Fuera:
- Valores de las puertas (`sioGateApproved`, `motorGateApproved`), `puertas.json`, la CI y el comportamiento.
- Flasheo, hardware y mediciones.

## Checklist

- [x] Ninguna línea vigente dice «R2, R6, R7 not accepted» — prueba: `git grep` sobre el código.
- [x] Las puertas no cambian — prueba: el diff del perfil solo toca un comentario.
- [x] Compilación de host y `ctest` en verde — prueba: `ctest`, 100/100.
- [x] `tools/tests` en verde; `check_notices` y `check_layering` OK — prueba: 295 OK; los dos chequeos OK.
- [ ] CI del PR en verde (el aviso solo se compila en ESP-IDF).

## Evidence

- Rama `fix/textos-riesgos-h4` desde `main` `69204a0`.

## Next

Commit y PR, con la aprobación del responsable.
