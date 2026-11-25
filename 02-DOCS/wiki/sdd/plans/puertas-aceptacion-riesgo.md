---
type: plan
title: Plan — puertas H4 y M3a por aceptación de riesgo
slug: puertas-aceptacion-riesgo
date: 2026-11-25
---
# Plan — puertas H4 y M3a por aceptación de riesgo

Especificación: `02-DOCS/wiki/sdd/specs/puertas-aceptacion-riesgo.md`.

## 0. Restricciones globales

- `kTapeOutput` sigue en `false`.
- Sin hardware, sin flasheo, sin mediciones y sin fusión automática.
- ADR-012: sin código de FujiNet.
- Las llamadas a UART y GPIO de cada paquete se cuentan exactamente (`check_sio_calls`).

## 1. Decisión principal

**Un solo estado vigente: puertas aprobadas.** `menu-sio` pasa de `closed_gates=True` a `gates=True`. Se retiran
`cassette-lectura` y `bench-c8`, que solo tienen sentido con las puertas cerradas. Se agrega el job de CI de
`cassette`.

Alternativa descartada: que `menu-sio` acepte los dos estados. Mantiene un caso que ya no existe y complica la
verificación del paquete.

## 2. Tareas

| # | Tarea | Criterio | Riesgo | Comprobación |
|---|---|---|---|---|
| T1 | Registro `docs/mediciones/puertas-aceptacion-riesgo-2026-11-25.md` y `puertas.json` (H4 y M3a `true`, fecha y registro) | CA5 | 1 | `check_gate_record(record, src)` OK en una prueba de herramientas |
| T2 | Perfil `fujinet_atari_v1x`: las dos puertas en `true`; actualizar las pruebas de host que esperan `false` | CA1 | 2 | `ctest` verde; RED antes del cambio |
| T3 | `firmware_package.py`: `menu-sio` con `gates=True` y texto nuevo; retirar `cassette-lectura` y `bench-c8`; actualizar `tools/tests` | CA2, CA3, CA4 | 2 | `tools/tests` verde; mutantes del chequeo de puertas |
| T3b | `EspTapeDeck` (`main/esp_tape_deck.cpp`): `gatesOpen` = `kTapeOutput` **y** las puertas. Hoy solo mira las puertas: con H4 y M3a aprobadas y `kTapeOutput=false`, el menú diría «ENCIENDE CON START» y la cinta no saldría | CA3 | 2 | Prueba de host del deck o del menú: con `kTapeOutput=false`, «AUN NO PUEDE CARGARSE» |
| T4 | CI: job `firmware-package-cassette`; quitar los jobs de `cassette-lectura` y `bench-c8` | CA2, CA4 | 2 | CI del PR verde, con los artefactos `cassette` y `menu-sio` |
| T5 | Documentos: D3 ampliada (enmienda §7), `h4-puerta-sio.md` (estado, R1, §7), procedimiento y ficha M3a, FTD | CA6 | 1 | `grep` de frases que contradigan la aprobación; revisión |

Orden: T1 → T2 → T3b → T3 → T4 → T5. T3 depende de T1 y T2, porque la CI lee el perfil y el registro del árbol.
T3b va antes que T3 para que ningún paquete `menu-sio` con las puertas aprobadas ofrezca una cinta que no sale.

## 3. Pruebas

- **Host:** el perfil aprobado autoriza SIO, MOTOR y salida de casete. Una copia de prueba con una puerta en `false`
  sigue fallando.
- **Herramientas:** `cassette` y `menu-sio` exigen el registro aprobado; un registro con una puerta en `false`, sin
  fecha o sin archivo se rechaza; las variantes retiradas ya no existen.
- **CI:** los dos paquetes se arman y se verifican.

## 4. Riesgos

| Riesgo | Mitigación |
|---|---|
| `menu-sio` cambia de comportamiento con las puertas aprobadas | **Confirmado:** `EspTapeDeck` publica `gatesOpen` solo con las puertas, y el menú (`menu_controller.cpp` 749 y 1050) ofrecería cargar una cinta que no sale. Se corrige en T3b. `shareDataIn` ya está detrás de `kTapeOutput` (`menu_sio.cpp` 782) |
| Un documento queda contradiciendo la aprobación | T5 con `grep` y la revisión |
| El riesgo eléctrico se lee como validado | El registro y los documentos dicen «aceptación de riesgo, no validación eléctrica» |

## 5. Reversión

`git revert` del merge: el perfil y `puertas.json` vuelven a `false`, y la CI vuelve a los paquetes anteriores.
