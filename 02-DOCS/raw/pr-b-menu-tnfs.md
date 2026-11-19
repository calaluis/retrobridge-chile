## Qué cambia

PR B de la spec `menu-tnfs` (sin issue propia; el prefijo M41 de las decisiones es histórico): las fuentes 2 a 8 del menú del Atari abren servidores TNFS de solo lectura.

- T006: el 6502 normaliza teclas (`AND #$3F`) y admite la acción 2 «esperar» con sondeo `$FE`.
- T007: disco `SECTOR-PROBE.ATR` que lee el sector 65 535 de D2.
- T009 a T011: `MemoryImageStore` (9 huecos, `*tnfs*0-8`), `TnfsFolder` y `TnfsLoader` (hilo de carga).
- T012: vistas TNFS del menú (carpetas, «CARGANDO nn%», errores) y devolución de huecos (M41-20).
- T013: `retrobridge_host` conecta fuentes, cargador y puerto UDP; registra cada orden TNFS y el SHA-256.
- T014: ADR-018, enmienda 2 de ADR-017, README y M41-1 a M41-7 en `decisions.md`.
- Merge de main `acb166a` (#43).
- Revisión del PR: UMOUNT de cada sesión al cerrar, sin esperar; nombres UTF-8 sin cambios en el registro JSONL;
  `host/README.md` con las fuentes TNFS; referencias a «Issue #41» corregidas.

## Fuentes consultadas (obligatorio, D-13)

- *The TNFS Protocol* (spectranet, «Updated July 15, 2020»), ya en `docs/fuentes.md` (PR A).
- NIST FIPS PUB 180-4 (SHA-256), ya en `docs/fuentes.md` (PR A).
- Ninguna fuente nueva en este PR.

## Declaración de procedencia

- [ ] No se copió, tradujo ni adaptó código fuente de FujiNet ni de otros emuladores de periféricos SIO.
- [ ] Las dependencias nuevas de terceros, si las hay, están en `THIRD_PARTY_NOTICES.md`.

## Verificación

- [x] Pruebas de host (`ctest`) en verde: 59 de 59, y `retrobridge_host` 1 de 1, con main integrado.
- [x] `check_layering.py` y `check_notices.py` en verde.
- [ ] Si el cambio toca hardware: no aplica (solo host y Altirra).
- Altirra 4.31, T015: sector 65 535 de una imagen local de 65 535 sectores leído por SIO (estado `$01`), igual a
  los 256 bytes esperados; `ARCADE.ATR` (16 MB) cargada desde el servidor y montada en D2 sin host caído.
- Demo T016 contra el servidor de laboratorio: AC1 a AC7, AC9 y AC12 por capturas y registro; solo órdenes de
  lectura y cada OPEN con `flags=0x0001 mode=0x0000`; SHA-256 de `Atari Logo.atr` y `ARCADE.ATR` iguales a las
  copias de referencia. AC14 completo: «CARGANDO 45%» en pantalla y, con `ARCADE.ATR` en D2, la sonda lee el
  sector 65 535 (estado `$01`) igual a la copia descargada. Sin captura: el logo «ATARI» (AC4 probado por el
  registro) y run4 (AC11).

Generated with [Claude Code](https://claude.com/claude-code)
