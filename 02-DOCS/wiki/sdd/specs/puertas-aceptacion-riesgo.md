---
type: spec
title: M3a por aceptación de riesgo (H4 pendiente)
status: draft
date: 2026-11-25
slug: puertas-aceptacion-riesgo
---
# M3a por aceptación de riesgo (H4 pendiente)

## Problema y por qué

El objetivo principal es cargar `.cas` (normal y turbo FSK) en el 65XE real con la FujiNet FN32ROV-1.3-Q24. Para eso
el firmware tiene que leer MOTOR (GPIO36, puerta M3a) y transmitir por DATA IN (GPIO21, puerta H4). Hoy las dos
puertas están en `false`.

El responsable decidió el 2026-11-25 (palabras exactas en `docs/mediciones/puertas-aceptacion-riesgo-2026-11-25.md`):
- **M3a:** aceptar R1, el riesgo de MOTOR (opción C). Base: la 1.3 funciona desde que se compró, con MOTOR directo a
  GPIO36 y el firmware de FujiNet. Mediciones: `docs/mediciones/m3a-motor-2026-11-25.md`.
- **H4:** sigue pendiente. R2, R6 y R7 no se aceptan y se evaluarán por separado.
- Antes del banco T9b solo se instala `menu-sio`.
- No agregar hardware (rechazó el adaptador con Rs) ni hacer más mediciones.

## Coste de no hacerlo

MOTOR no se puede leer en el equipo real, y T9b no puede empezar por el lado de MOTOR.

## La alternativa más barata

Cambiar solo el booleano de M3a en el perfil y en `puertas.json`. **No basta:** `menu-sio`, `cassette-lectura` y
`bench-c8` exigen las dos puertas cerradas, así que la CI quedaría en rojo.

## Objetivos

- M3a aprobada **por aceptación de riesgo del responsable**, no por medición, en el perfil y en el registro.
- H4 sin cambios: sin aprobar.
- La CI sigue armando `menu-sio`, ahora con M3a aprobada y H4 sin aprobar.
- Los documentos distinguen el estado vigente del histórico y dicen que la validación eléctrica sigue pendiente.

## Fuera de alcance

- H4, R2, R6 y R7: decisión aparte del responsable.
- `kTapeOutput=true` y el banco T9b en el 65XE: PR aparte.
- El paquete `cassette`: no se arma mientras H4 siga pendiente.
- Cualquier hardware nuevo o medición.
- Borrar el código de `bench_c8` o de la casetera: solo se retiran sus paquetes.

## Usuarios y contexto

El responsable es el único usuario. Revisa el firmware viéndolo en la FujiNet 1.3 con el 65XE.

## Qué podrá hacer el usuario

- Descargar de la CI el paquete `menu-sio`, que lee MOTOR en GPIO36 como entrada.

## Qué resultado obtiene

- `puertas.json`: `M3a` con `aprobada: true`, fecha `2026-11-25` y registro en `docs/mediciones/`; `H4` sin cambios.
- Perfil `fujinet_atari_v1x`: `motorGateApproved = true`; `sioGateApproved = false`.
- La CI en verde, con el job `firmware-package-menu-sio`; sin los jobs de `cassette-lectura` ni de `bench-c8`.

## Qué debe respetar

- La regla de `puertas.json`: solo el responsable lo cambia, en un PR aparte, junto con el perfil y el registro.
  Este PR lo prepara el asistente; el responsable lo revisa y lo fusiona.
- No ampliar por inferencia el alcance de la aceptación de riesgo (revisión del PR #110, H1).
- ADR-012: sin código de FujiNet.
- Las capas (`check_layering`) y la lista exacta de llamadas a UART y GPIO de cada paquete.
- `kTapeOutput` sigue en `false`.

## Cómo lo demuestra

- Pruebas de host (`ctest`) y de herramientas (`tools/tests`) en verde, con pruebas para el perfil y los paquetes.
- CI del PR en verde, incluido el job de `menu-sio`.

## Criterios de aceptación

1. Con el perfil real, `validate_for_motor` acepta GPIO36, y `validate_for_sio` y `validate_for_cassette_output`
   siguen rechazando la FujiNet 1.3 (prueba de host).
2. `firmware_package.py` arma `menu-sio` solo con M3a aprobada y H4 sin aprobar, en el perfil y en el registro, y
   rechaza `cassette` con el perfil real.
3. Con `kTapeOutput=false`, el menú dice «AUN NO PUEDE CARGARSE» aunque las dos puertas estén aprobadas.
4. Las variantes `cassette-lectura` y `bench-c8` se retiran del empaquetador y de la CI.
5. El registro cita las decisiones del responsable al pie de la letra y dice «aceptación de riesgo».
6. `h4-puerta-sio.md`, la enmienda, el procedimiento y la ficha M3a dicen lo mismo, y separan lo vigente de lo
   histórico.

## Puntos a aclarar

- **suposición tomada:** `menu-sio` exige exactamente M3a aprobada y H4 sin aprobar. *Base:* es el único estado
  vigente.
- **suposición tomada:** se retiran `cassette-lectura` y `bench-c8`. *Base:* exigían las dos puertas sin aprobar, y
  el responsable decidió instalar solo `menu-sio` antes de T9b.
- **suposición tomada:** el registro de puertas es un archivo nuevo,
  `docs/mediciones/puertas-aceptacion-riesgo-2026-11-25.md`. *Base:* `m3a-motor-2026-11-25.md` es de medición.
- **pregunta abierta (del responsable):** `menu-sio` responde por DATA IN con H4 sin aprobar, lo que expone R2, R6 y
  R7. Qué cubre ese uso conectado lo decide el responsable aparte.

- **decisión diferida:** recuperar el banco C8 (prueba de DATA IN solo por USB) cuando el responsable decida validar
  H4. Cómo: `docs/hardware/procedimiento-prueba-banco-c8.md`.

## Clarifications

- 2026-11-25 · Revisión del PR #110, H1: ¿la aprobación cubre H4 y R2, R6 y R7? → No. Solo M3a y R1; H4 sigue
  pendiente → palabras del responsable en el registro.
- 2026-11-25 · Revisión del PR #110, H2: ¿qué firmware se instala antes de T9b? → Solo `menu-sio` → el paquete
  `cassette` puede transmitir sin depender de `kTapeOutput`.
- 2026-11-25 · Después del PR #110: R2 y R7 no aceptados; R6 aceptado solo con USB; H4 sigue pendiente → `docs/mediciones/riesgos-h4-2026-11-25.md`.
