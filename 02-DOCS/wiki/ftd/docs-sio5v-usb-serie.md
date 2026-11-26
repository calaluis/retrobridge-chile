---
author: Calaluis
branch: docs/sio5v-usb-serie
status: in-review
---
# Registro de las series de SIO +5V con solo USB (diagnóstico de C-1)

## Intent

Registrar el diagnóstico de por qué C-1 rechaza C8a con solo USB: fuentes oficiales del circuito, configuración del
ADC, y dos series de lectura por COM3 (con y sin reinicio) autorizadas por el responsable. Base para la revisión
independiente y para cualquier decisión futura sobre C-1.

## Scope

Dentro:
- `docs/mediciones/sio5v-usb-serie-2026-11-25.md` (nuevo).
- Enlace desde `docs/mediciones/banco-c8a-2026-11-25.md` §8.

Fuera:
- Umbral de C-1, código, `puertas.json`, H4, R2, R7, flasheos, `espefuse` y cualquier cambio físico de la placa.

## Checklist

- [x] Rama nueva desde `main` `fe44dff` (elegida por el responsable).
- [x] Series por COM3 solo con `sio status`, sin flashear ni escribir eFuses — prueba: capturas en `Respaldos`
  (`FujiNet-RB-sio5v-serie-2026-11-25/serie.txt` y `sin-reinicio.txt`); la segunda sin ninguna línea `rst:`.
- [x] Registro con verificado, calculado, hipótesis y pendiente separados — prueba: §2–§9 del registro.
- [x] Enlace desde el registro de F4 — prueba: `banco-c8a-2026-11-25.md` §8.
- [x] Sin SSID, IP ni MAC en el repositorio — prueba: búsqueda en el diff.
- [x] `tools/tests` y `check_notices` OK.

## Next step

Revisión independiente del PR. Cualquier cambio de C-1 necesita decisión escrita del responsable, en su propio PR.
