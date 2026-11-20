---
type: progress
title: Progreso — menu-escritura-disco
description: Avance por tarea del plan menu-escritura-disco, con evidencia.
tags: [sdd, progress, host, sio, netsio, escritura]
timestamp: 2026-11-19T23:59:00Z
topic: sdd
slug: menu-escritura-disco
plan: 02-DOCS/wiki/sdd/plans/menu-escritura-disco.md
---

# Progreso — menu-escritura-disco

## W001 — Medida de la escritura por NetSIO (hecha, 2026-11-19)

Método: sonda Python propia (no versionada, ADR-012) registrada en el hub como dispositivo; atiende solo D2 sobre
una copia de una ATR de 720 x 128. DOS 2.5 en D1 de Altirra 4.31; hub `netsiohub` de laboratorio. Copia de
`D1:TORRES.BAS` a `D2:TORRES.BAS` desde el DOS.

Resultado con `81 <sync> 01 'A' <write_size>` y `write_size` = 129 (`81 00`, LE: datos + checksum):

- 36 de 36 escrituras `W` completas (sectores 4 a 35, 360 y 361). Todas: 129 bytes recibidos, checksum válido.
- Cada escritura llega como `02` (65 bytes de datos) + `02` (63 bytes) + `09 <checksum> <sync>`; los bloques
  pueden partirse de otra forma: se juntan hasta `09`.
- `09` lleva el byte de checksum y un número de sincronización nuevo; se contesta con `81 <sync> 01 'A' 00 00` y
  luego COMPLETE en un `02`.
- La ATR quedó con `TORRES  BAS` en el directorio (sector 361) y la VTOC actualizada.

Conclusión para W003: `write_size` = tamaño del sector + 1. No se probó 128: no hace falta para el producto.

Hallazgos del laboratorio:

- Una unidad de Altirra solo vacía sigue contestando. Debe estar en **Off** para que sus comandos lleguen por
  NetSIO. Con la misma ATR en D2 de Altirra (VRWSafe) y en la sonda, el Atari descartaba tramas (8 lecturas
  seguidas del sector 361, error 143). El README del host dice «Dejar D1 vacía»: corregir en W007 a «D1 en Off».
- La sonda necesita la pausa t5 (≥ 250 µs) entre ACK y COMPLETE, como `SioSession` (300 µs).
- Formatear (`!`, `"`) por la sonda no se completó; está fuera del alcance de la spec.

Evidencia: `D:\rb-e0\m45-w001\plus1.jsonl` (intento 6) y `sonda-d2-red.atr`; intentos 1 a 5 conservados con sus
nombres (`plus1-intento1..5.jsonl`).

## Pendiente

W002 a W007.
