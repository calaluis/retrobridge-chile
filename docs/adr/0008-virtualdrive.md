# ADR-008 — La entidad raíz se llama `VirtualDrive`

- Estado: Aprobada (2026-10-23); D-1

## Decisión

`VirtualDrive` (unidad D1…D8) en lugar de `VirtualDisk`.

## Justificación

En el lenguaje Atari, el computador se comunica con una **unidad**, y el **disco** es lo que se inserta
(`DiskImage`). Con "VirtualDisk", ambos conceptos se confundirían.
