---
type: progress
title: Progreso — menu-formatear-disco
description: Avance por tarea del plan menu-formatear-disco, con evidencia.
tags: [sdd, progress, host, sio, menu, formateo]
timestamp: 2026-11-20T03:00:00Z
topic: sdd
slug: menu-formatear-disco
plan: 02-DOCS/wiki/sdd/plans/menu-formatear-disco.md
---

# Progreso — menu-formatear-disco

## F001 — Dominio y app (hecha, 2026-11-20)

- `ErrorCode::DensityMismatch` (al final del enum; nombre en `diagnostic_console`).
- `DiskImage::format(Density)`: fuente nula, después protección y después densidad; ninguna de las tres escribe.
  Solo `Single` (720 x 128) y `Enhanced` (1040 x 128). Escribe `$00` sector a sector con un búfer fijo de 128 bytes
  (`static constexpr`, sin reservas). El primer error de E/S lo detiene; los sectores anteriores ya quedaron en
  cero.
- `VirtualDrive::format` (`NoDiskMounted`, registra el fallo) y `rb::app::FormatDisk`.
- Pruebas: 6 casos nuevos en `test_disk_write` (simple, media, otra densidad incluida la doble, protegida, error
  de E/S a mitad, unidad vacía). El fake `MemoryByteSource` gana `fail_writes_after(n)`. Primero en rojo (no
  compilaba), después en verde: 19 de 19 en `test_disk_write`, 64 de 64 en total.
- Desvío del plan: `FormatDisk` no tiene prueba propia, como `WriteSector`; lo prueban el despachador y la sesión
  en F002.
- Riesgo `rsc risk`: tier 1. Un revisor (correctitud): sin hallazgos. Un detalle aceptado: no hay prueba de
  formatear una ATR con cabecera inconsistente o bytes de sobra (el revisor confirmó que `sectorCount` sale del
  tamaño real y la cabecera y los bytes de sobra no se tocan).
- Controles (verify por tarea, corrida nueva sobre b872251 + cambios):
  - pruebas: PASS, 64 de 64 (`ctest --test-dir build/host`).
  - compilación: PASS, MinGW ucrt64, `-Werror`, 0 avisos.
  - seguridad: PASS por lectura dirigida. Solo escribe por `ImageByteSource::writeAt`, dentro de los rangos de
    `AtrLayout::locate`, y nunca en una fuente protegida.
  - trazabilidad: PASS. Formatear simple y media = M46-1 (AC1, AC2); otra densidad = AC3; protegida = AC4.
  - **SUSTITUIDA** — sanitizadores: el CI compila con `-DRB_SANITIZE=ON`; aquí no.
  - **HERRAMIENTA-AUSENTE** — firmware ESP-IDF: sin `idf.py` en este PC; lo compila el CI.

## Pendiente

F002 (SIO), F003 (crear ATR), F004 (menú), F005 (demo), F006 (docs).
