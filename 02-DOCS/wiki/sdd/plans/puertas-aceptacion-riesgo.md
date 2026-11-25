---
type: plan
title: Plan — M3a por aceptación de riesgo (H4 pendiente)
slug: puertas-aceptacion-riesgo
date: 2026-11-25
---
# Plan — M3a por aceptación de riesgo (H4 pendiente)

Especificación: `02-DOCS/wiki/sdd/specs/puertas-aceptacion-riesgo.md`.

> **Cambio del 2026-11-25 (revisión del PR #110):** la primera versión aprobaba H4 y M3a. El responsable aclaró que
> acepta solo R1: M3a aprobada y H4 pendiente. Además, antes de T9b solo se instala `menu-sio`. Este plan ya lo
> refleja.

## 0. Restricciones globales

- `kTapeOutput` sigue en `false`.
- H4 sigue sin aprobar.
- Sin hardware, sin flasheo, sin mediciones y sin fusión automática.
- ADR-012: sin código de FujiNet.
- Las llamadas a UART y GPIO de cada paquete se cuentan exactamente (`check_sio_calls`).

## 1. Decisión principal

**Cada variante declara el estado exacto de las puertas que exige**, como `(H4, M3a)`: `cassette` exige `(true,
true)` y `menu-sio` exige `(false, true)`. Se retiran `cassette-lectura` y `bench-c8`, que exigían `(false, false)`.
No hay job de CI para `cassette` mientras H4 siga pendiente.

Alternativa descartada: mantener los dos booleanos `gates` y `closed_gates`. No pueden expresar el estado mixto.

## 2. Tareas

| # | Tarea | Criterio | Riesgo | Comprobación |
|---|---|---|---|---|
| T1 | Registro `docs/mediciones/puertas-aceptacion-riesgo-2026-11-25.md` y `puertas.json` (M3a `true`, H4 sin cambios) | CA5 | 1 | `read_gate_record(repo, (False, True))` OK en una prueba de herramientas |
| T2 | Perfil `fujinet_atari_v1x`: M3a en `true`; pruebas de host | CA1 | 2 | `ctest` verde |
| T3b | `tapeGatesOpen` (`main/tape_gates.hpp`): `kTapeOutput` **y** las dos puertas | CA3 | 2 | Prueba de host: con `kTapeOutput=false`, la cinta sigue cerrada |
| T3 | `firmware_package.py`: puertas esperadas por variante; `menu-sio` con `(False, True)`; retirar `cassette-lectura` y `bench-c8` | CA2, CA4 | 2 | `tools/tests` verde; mutantes del chequeo de puertas |
| T4 | CI: quitar los jobs de `cassette-lectura` y `bench-c8`; sin job de `cassette` | CA4 | 2 | CI del PR verde, con el artefacto `menu-sio` |
| T5 | Documentos: bloque «Estado vigente» y marcas de lo histórico en `h4-puerta-sio.md`, la enmienda, el procedimiento, la ficha M3a y los procedimientos que citan paquetes retirados | CA6 | 1 | `grep` de frases que contradigan el estado; revisión |

Orden: T1 → T2 → T3b → T3 → T4 → T5.

## 3. Pruebas

- **Host:** el perfil real concede MOTOR y rechaza SIO y la salida de casete. Una copia con las dos puertas
  aprobadas abre la cinta solo con `kTapeOutput`.
- **Herramientas:** `menu-sio` exige M3a aprobada (con fecha y registro) y H4 en JSON `false`; rechaza H4 aprobada,
  M3a sin aprobar y un registro faltante. `cassette` se rechaza con el perfil real. Las variantes retiradas ya no
  existen.
- **CI:** el paquete `menu-sio` se arma y se verifica.

## 4. Riesgos

| Riesgo | Mitigación |
|---|---|
| `menu-sio` cambia de comportamiento | Lee MOTOR en GPIO36 como entrada. La cinta no sale: `kTapeOutput=false` y H4 sin aprobar. Documentado en el registro |
| `menu-sio` responde por DATA IN con H4 sin aprobar (R2, R6, R7) | Sin cambios respecto de antes. Queda como punto abierto para el responsable en el registro |
| El riesgo eléctrico se lee como validado | El registro y los documentos dicen «aceptación de riesgo, no validación eléctrica» |
| Se amplía por inferencia la aceptación | El registro cita las palabras exactas del responsable |

## 5. Reversión

`git revert` del merge: el perfil y `puertas.json` vuelven a `false`, y la CI vuelve a los paquetes anteriores.
