---
type: plan
title: Plan — Pantalla principal del menú Atari (Issue #40)
description: Cómo se construye la pantalla principal del menú: terminal 6502 delgado, lógica y dibujo en el host.
tags: [sdd, plan, host, netsio, menu]
timestamp: 2026-11-17T23:55:00Z
topic: sdd
slug: menu-principal
branch: feat/40-menu-principal
status: approved
---

# Plan — Pantalla principal del menú (Issue #40)

Spec: `specs/menu-principal.md` (aprobada). Modo: autopiloto hasta el PR (commit, push y PR con aprobación).

## Arquitectura

- **Terminal delgado en el Atari.** El programa 6502 (`tools/make_menu.py`) ya no conoce el catálogo. Al
  arrancar pide la pantalla al host; después envía cada tecla. Comando nuevo `$5A 'K'` (AUX1 = código de tecla
  de CH, $FF = solo redibujar). Respuesta de 962 bytes: acción (0 mostrar, 1 arrancar D1 con COLDSV), diseño
  (0 principal, 1 lista de una fuente) y 960 bytes de pantalla en códigos internos.
- **Dos display lists** en la misma página de 256 bytes (solo cambia el byte bajo de SDLSTL):
  - principal, como la maqueta: 8 en blanco, modo 7 título, 8 en blanco, 8 fuentes en modo 2, 8 en blanco,
    modo 6 subtítulo, 11 líneas modo 2 (vacía, D1..D8, C:, mensajes), 2 líneas de pie;
  - lista: 8 en blanco, modo 7 título, 8 en blanco, 20 entradas, mensajes y una línea de pie.
- **Colores** por las sombras del SO: COLOR0..4 ($2C4-$2C8) = $0E, $0E, $A0, $46, $A0; CHACT ($2F3) = $02.
  Juego de caracteres de la ROM. Todo vive desde $0700; la pantalla no cruza 4 KB y las display lists no
  cruzan 1 KB (el generador lo comprueba).
- **Plantillas en un solo lugar.** El generador exporta la plantilla de la pantalla principal (título,
  subtítulo, pie con $42/$56 y teclas en inverso) y la de la lista. El host copia la plantilla y escribe solo
  las líneas dinámicas.
- **Host.** `MenuController::key()` es una máquina de estados: Principal, Lista (fuente 1), Elegir unidad,
  Desmontar. D1 tiene una imagen **asignada**; D1 sirve el menú hasta `A`. D2..D8 se montan y desmontan con
  `MountDiskImage` y `UnmountDiskImage` por el buzón (ADR-014). `DriveBay{8}`; repositorio con 9 archivos.
- **Transporte.** `MenuSioTransport` atiende `'K'` y envía COMPLETE + 962 + checksum en bloques de 512 como
  máximo (contrato de `NetSioSioTransport::send`).
- **Fuentes.** `--sources <archivo>`: líneas no vacías y sin `#`, como máximo 7, 1..32 ASCII imprimibles.
  `--log` no puede ser el archivo de fuentes.

## Tareas

| # | Tarea | Criterio | Riesgo | Comprobación |
|---|-------|----------|--------|--------------|
| T1 | Generador 6502: terminal, 2 display lists, plantillas | AC1 | 1 | `test_menu_program.py`: DL byte a byte, colores, tecla enviada, acción 1 = COLDSV, host caído |
| T2 | `MenuSources` y `--sources` | AC2 | 1 | `test_host_menu`: archivo válido, 8 fuentes, línea larga; `test_log_collision`: `--log` = fuentes |
| T3 | `MenuScreen` + `MenuController::key()` | AC2, AC4, AC5 | 1 | `test_host_menu`: cada tecla del pie, pendientes, D1 asignada, montaje D2, desmontaje |
| T4 | Transporte `'K'` en bloques, `DriveBay{8}`, repositorio | AC3, AC4 | 1 | `test_host_menu` con transporte falso: 964 bytes en 2 bloques |
| T5 | Demo en Altirra | AC1, AC3, AC4 | 0 | capturas y log en `D:\rb-e0\m40` |
| T6 | ADR-017 enmendado, README, progreso | — | 0 | revisión |

## Riesgos

- Altirra con dos bloques de datos seguidos en una respuesta: se comprueba en T5 antes de cerrar.
- 216 líneas de barrido: si Altirra recorta, se anota.
