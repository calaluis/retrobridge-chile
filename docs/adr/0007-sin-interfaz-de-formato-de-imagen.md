# ADR-007 — `DiskImage` y `AtrLayout` concretos

- Estado: Aprobada (2026-10-23)

## Decisión

No se define una interfaz de formato de imagen mientras ATR sea el único formato. El punto de extensión queda
localizado en `MountDiskImage`.

## Justificación

Una interfaz con un solo implementador es una abstracción especulativa. Se extrae cuando llegue el segundo
formato (por ejemplo, XFD o ATX).
