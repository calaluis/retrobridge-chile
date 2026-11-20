---
type: verification
title: Verificación — menu-formatear-disco — 2026-11-20 (r2)
description: Veredicto final de menu-formatear-disco con la CI del PR #56 ejecutada.
tags: [sdd, verification, menu-formatear-disco]
timestamp: 2026-11-20T06:00:00Z
topic: sdd
slug: menu-formatear-disco
source_state: 8d53eb2 (clean), fusionado en main como a543245
---

# Verificación — menu-formatear-disco — 2026-11-20 (r2)

Completa el registro `menu-formatear-disco-2026-11-20.md`. Ese registro dejó dos capas para la CI. Esta corrida
las cierra; lo demás no cambia.

## CI del PR #56

- Primera corrida (cabeza 553438f): `Host tests (CMake + CTest + Unity, ASan/UBSan)` falló al compilar. El GCC de
  Linux marcó `-Werror=format-truncation` en `host/src/menu_controller.cpp:1029` (`snprintf` de `"%s.ATR"` en un
  búfer de 30). El nombre ya estaba validado a 25 caracteres, pero el compilador no lo sabe. MinGW no da ese aviso.
- Arreglo 8d53eb2: la copia lleva precisión `%.*s` con `kMaxNewAtrName`. 65 de 65 en local.
- Corrida sobre 8d53eb2 (44675481096 push, 44675485268 pull_request): 6 de 6 checks en verde.
  - `Host tests (CMake + CTest + Unity, ASan/UBSan)`: pass. Cierra la capa SUSTITUIDA de los sanitizers.
  - `ESP-IDF build (esp32)`: pass. Cierra la capa HERRAMIENTA-AUSENTE del firmware.
  - `Layering and third-party notices`: pass.

## Criterios de aceptación

Sin cambios respecto del primer registro: AC1 a AC10 con evidencia.

## Lección

- Un aviso de formato del GCC de Linux no aparece con MinGW. Antes de un push conviene acotar con precisión
  (`%.*s`) cada `%s` que se copia en un búfer fijo, aunque el largo ya esté validado.

## Veredicto: PASA — todas las capas corrieron y todos los criterios tienen evidencia.
