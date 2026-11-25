---
type: spec
title: Puertas H4 y M3a por aceptación de riesgo
status: draft
date: 2026-11-25
slug: puertas-aceptacion-riesgo
---
# Puertas H4 y M3a por aceptación de riesgo

## Problema y por qué

El objetivo principal es cargar `.cas` (normal y turbo FSK) en el 65XE real con la FujiNet FN32ROV-1.3-Q24. El paquete
`cassette` exige H4 y M3a aprobadas, en el perfil de la placa y en `docs/mediciones/puertas.json`. Hoy las dos están
en `false`, y la CI no arma el paquete `cassette`.

El responsable decidió el 2026-11-25:
- **M3a:** aceptar el riesgo de MOTOR (opción C). Base: la 1.3 funciona desde que se compró, con MOTOR directo a
  GPIO36 y el firmware de FujiNet. Mediciones: `docs/mediciones/m3a-motor-2026-11-25.md`.
- **H4:** aceptar el riesgo con la misma base. El firmware de FujiNet siempre manejó DATA IN en esa placa.
- No agregar hardware (rechazó el adaptador con Rs) ni hacer más mediciones.

## Coste de no hacerlo

La casetera no se puede usar en el equipo real. El objetivo principal del producto queda sin cumplir.

## La alternativa más barata

Cambiar solo los dos booleanos del perfil y `puertas.json`. **No basta:** la CI quedaría en rojo, porque
`menu-sio`, `cassette-lectura` y `bench-c8` exigen las puertas cerradas. Y el paquete `cassette` seguiría sin
armarse, porque no tiene job.

## Objetivos

- H4 y M3a aprobadas **por aceptación de riesgo del responsable**, no por medición, en el perfil y en el registro.
- La CI arma el paquete `cassette` y sigue armando `menu-sio`.
- Los documentos dicen con claridad que es una aceptación de riesgo, no una validación eléctrica.

## Fuera de alcance

- `kTapeOutput=true` y el banco T9b en el 65XE: PR aparte.
- Cualquier hardware nuevo o medición.
- Borrar el código de `bench_c8` o de la casetera: solo se retiran sus paquetes.
- ADR-012.

## Usuarios y contexto

El responsable es el único usuario. Revisa el firmware viéndolo en la FujiNet 1.3 con el 65XE.

## Qué podrá hacer el usuario

- Descargar de la CI el paquete `cassette`, con DATA IN y MOTOR habilitados por el perfil.
- Seguir descargando `menu-sio`.

## Qué resultado obtiene

- `puertas.json`: `H4` y `M3a` con `aprobada: true`, fecha `2026-11-25` y registro en `docs/mediciones/`.
- Perfil `fujinet_atari_v1x`: `sioGateApproved = true` y `motorGateApproved = true`.
- La CI en verde: los jobs `firmware-package-cassette` y `firmware-package-menu-sio`; sin los jobs de
  `cassette-lectura` ni de `bench-c8`.

## Qué debe respetar

- La regla de `puertas.json`: solo el responsable lo cambia, en un PR aparte, junto con el perfil y el registro.
  Este PR lo prepara el asistente; el responsable lo revisa y lo fusiona.
- ADR-012: sin código de FujiNet.
- Las capas (`check_layering`) y la lista exacta de llamadas a UART y GPIO de cada paquete.
- `kTapeOutput` sigue en `false`.
- La decisión D3 y los documentos de hardware quedan coherentes entre sí (lección del PR #109, H6).

## Cómo lo demuestra

- Pruebas de host (`ctest`) y de herramientas (`tools/tests`) en verde, con pruebas nuevas o cambiadas para el perfil
  aprobado y los paquetes.
- CI del PR en verde, incluidos los jobs de `cassette` y `menu-sio`.
- `firmware_package.py verify` sobre el paquete `cassette` del PR.

## Criterios de aceptación

1. Con el perfil aprobado, `validate_for_sio`, `validate_for_motor` y `validate_for_cassette_output` aceptan la
   FujiNet 1.3 (prueba de host).
2. `firmware_package.py` arma `cassette` con el perfil y el registro aprobados, y lo rechaza si alguno no lo está.
3. `menu-sio` se arma con las puertas aprobadas, y su texto de perfil ya no dice «sin aprobar» ni pide el adaptador
   sin pin 8.
4. Las variantes `cassette-lectura` y `bench-c8` se retiran del empaquetador y de la CI.
5. El registro de puertas menciona H4 y M3a, dice «aceptación de riesgo» y cita sus bases.
6. `h4-puerta-sio.md`, la enmienda, el procedimiento y la ficha M3a dicen lo mismo.

## Puntos a aclarar

- **suposición tomada:** `menu-sio` pasa a exigir puertas aprobadas, en vez de aceptar los dos estados. *Base:*
  menos casos y un solo estado vigente.
- **suposición tomada:** se retiran `cassette-lectura` y `bench-c8`. *Base:* con H4 y M3a aprobadas, el código de
  la casetera maneja DATA IN y MOTOR, así que «solo lectura» ya no es posible con el mismo perfil. C8 era una prueba
  previa a H4.
- **suposición tomada:** el registro de puertas es un archivo nuevo,
  `docs/mediciones/puertas-aceptacion-riesgo-2026-11-25.md`, y no `m3a-motor-2026-11-25.md`. *Base:* el registro M3a
  es de medición; mezclar las dos cosas confunde.
