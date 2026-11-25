---
author: Calaluis
branch: docs/enmienda-motor-gpio36
status: in-review
---
# Enmienda de MOTOR y GPIO36 en la FN32ROV-1.3-Q24 (propuesta)

## Intent

Responder por escrito a la contradicción aparente: el firmware original de FujiNet usó MOTOR en la 1.3 sin medir,
y RetroBridge exige M3a y H4. La propuesta separa el riesgo eléctrico de la placa del comportamiento del firmware,
marca como hipótesis lo que no está medido y deja las puertas como están.

## Scope

Dentro:
- `docs/hardware/enmienda-motor-gpio36.md`: propuesta de enmienda, con evidencias, criterios de aceptación y
  decisiones pendientes del responsable.
- `docs/hardware/h4-puerta-sio.md`: una nota en la cabecera que apunta a la propuesta.

Fuera:
- `docs/mediciones/puertas.json`, el perfil de `rb_board`, `kTapeOutput` y cualquier código.
- Pruebas físicas, mediciones, flasheos, GPIO y fusiones.
- Código de FujiNet como fuente (ADR-012).

## Checklist

- [x] E1: MOTOR nominal de unos 5 V y conexión directa a GPIO36, con fuentes — prueba: §2 de la propuesta.
- [x] E2: placa frente a firmware original y RetroBridge — prueba: tabla de §3; `cassette_service.cpp`
      `EspMotorIn::configure()` sale sin `gpio_config` si M3a no está firmada.
- [x] E3: diodos ESD, 3,9 V y absorción por la ESP32 como hipótesis [P] — prueba: tabla de §4.
- [x] E4 y E5: M3a, H4 y DATA IN pendientes; DATA IN no se aprueba por diseño — prueba: §5 y §6.
- [x] Sin la sugerencia de tocar la ESP32 — prueba: §4 lo prohíbe; `grep` sin otras apariciones en `docs/` y
      `02-DOCS/wiki/`.
- [x] Decisiones pendientes D1–D5 — prueba: §7.
- [x] Diff sin archivos de puertas, perfil ni código — prueba: `git diff --stat origin/main` del PR.
- [ ] Revisión independiente del PR.

## Evidence

- Rama `docs/enmienda-motor-gpio36` desde `main` `da3aded`.
- El texto se apoya en documentos ya versionados (`h4-puerta-sio.md` §3.4, `proteccion-motor-gpio36.md` §2,
  `h4-puertas-escucha-transmision.md`) y en fuentes de `docs/fuentes.md`. No hay fuentes nuevas.

## Next

Revisión independiente y decisión del responsable (D1–D5). Ninguna puerta cambia con este PR.
