# ADR-009 — Casos de uso como clases concretas

- Estado: Aprobada (2026-10-23)

## Decisión

`MountDiskImage`, `GetDriveStatus` y `ReadSector` son clases concretas, sin interfaces de entrada. Los puertos
existen solo hacia el mundo externo.

## Justificación

No hay un segundo implementador. Las pruebas usan los casos de uso reales con puertos falsos.
