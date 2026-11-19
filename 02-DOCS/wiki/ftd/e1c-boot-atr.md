---
author: Calaluis
branch: feat/e1c-boot-atr
status: in-progress
---
# E1c: ATR de arranque propia y READ SECTOR verificado (Issue #32)

## Intent

Cerrar el criterio mínimo de la modalidad emulada (Issue #32, diseño E1, D4, P3). Altirra arranca desde
`retrobridge_host` una ATR propia cuyo programa 6502 muestra «RETROBRIDGE D1 OK». El CRC32 de los sectores
enviados coincide con `manifest.csv`. El resultado se repite tras 2 reinicios en frío.

## Scope

Dentro:
- `tools/make_atr.py`: imagen `rb-boot` (720 x 128). Registro de arranque de 3 sectores en $0700, con un
  programa 6502 propio: XOR de los sectores 2 y 3 contra el valor fijado por el generador, display list
  propia en modo 2 con «RETROBRIDGE D1 OK» (o «RETROBRIDGE D1 ERR») y un bucle.
- `tools/compare_crc.py`: lee también el JSONL de `retrobridge_host` (READ `52` + respuesta `43` + datos +
  checksum) y compara el CRC32 de los datos con el manifiesto.
- Pruebas en `tools/tests` (las corre la CI).
- Instrucciones de uso en `host/README.md`.

Fuera:
- CI (`.github/workflows`), firmware, hardware, GPIO, flasheo, H4/H4-L/H4-T.
- Código de FujiNet o de emuladores (ADR-012). Ninguna ROM se versiona.
- Escritura en la ATR (la unidad sigue siendo de solo lectura).

## Checklist

- [x] Imagen `rb-boot` con programa 6502 propio — prueba: tests de estructura (registro, XOR, mensaje) en verde.
- [x] `compare_crc.py` acepta JSONL de `retrobridge_host` — prueba: tests con un JSONL sintético en verde,
      incluido un caso con un sector alterado que debe fallar.
- [x] Instrucciones de uso en `host/README.md` — prueba: los comandos del README son los que se usan en la corrida.
- [x] Corrida en Altirra — prueba: captura con «RETROBRIDGE D1 OK» tras el arranque y tras 2 reinicios en frío.
- [x] CRC32 de la corrida — prueba: `compare_crc.py manifest.csv e1c.jsonl` sale con 0.
- [x] Controles: `tools/tests`, `check_layering`, `check_notices`, `rsc risk` y un revisor si el nivel es >= 1.

## Evidence

- Boot sector ensamblado (2026-11-17): `BNE` a $0709, `BEQ show` a $071D, init $0730 = `RTS`, display list $0731
  con `JVB $0731`, LMS $0741 (OK) y $0755 (ERR); XOR esperado `A3`.
- `python -I -m unittest discover -s tools/tests`: Ran 81 tests, OK (71 previas y 10 nuevas). Las nuevas ejecutan
  el programa con un intérprete de sus 13 opcodes: OK con datos intactos, ERR con un bit cambiado en el sector 2
  o en el 3.
- `compare_crc.py` contra el log real de run4 de E1b (manifiesto de `boot-min-sd.atr`): OK, 3 lecturas del sector 1.
- Corrida en Altirra 4.31 + hub v0.16 + `retrobridge_host --peer netsiohub` (`D:\rb-e0\e1c\run1`, 2026-11-17,
  21:10 a 21:12 UTC, hora del PC), `rb-boot.atr` sha256 9bd67a98...:
  - arranques: encendido (FF 00/01), reinicio 1 (FF 11) y reinicio 2 (FF 21); en cada uno, STATUS D1 y READ de
    los sectores 1, 2 y 3;
  - pantalla tras cada uno: «RETROBRIDGE D1 OK» (las capturas son idénticas; el reinicio lo prueban los FF y las
    lecturas nuevas del log);
  - resumen del host: 12 tramas, 12 respuestas 81, 0 descartadas, 0 malformadas, 3 peticiones de crédito;
  - `compare_crc.py build/test-images/manifest.csv e1b.jsonl`: OK, 9 lecturas de los sectores 1, 2 y 3; exit 0;
  - registros: `e1b.jsonl` sha256 e6b9087e..., `hub-e1b.log` sha256 2b94f59f....
- `check_layering` OK, `check_notices` OK. `rsc risk`: tier 1, un revisor, sin aprobación humana obligatoria.
- No probado en Altirra: la ruta ERR (solo en la prueba con intérprete).
- Revisor de corrección (contexto nuevo): sin hallazgos que bloqueen. Decodificó a mano opcodes, saltos, registro
  de arranque y display list; las cinco imágenes previas salen idénticas byte a byte. Notas corregidas:
  `compare_crc` reportaba con traceback las líneas JSON que no son objeto o con hex inválido (ahora son problemas),
  y usaba solo la imagen del último `start` (ahora cada lectura usa la imagen de su corrida). Nota no corregida:
  `docs/mediciones/h4-regresion-h3c-2026-10-25.md` dice «cinco imágenes»; es un registro fechado y era cierto.
- Tras las correcciones: 83 tests OK; `compare_crc` sobre `run1/e1b.jsonl` sigue OK (9 lecturas).

- PR #37 (commit c27a280): la CI falló en «Host tests» por `test_meta_a` (H3), que exige exactamente las 5
  imágenes y 4640 sectores de la Meta A («Expected 5 Was 6», «Expected 4640 Was 5360»). Ni el revisor ni yo
  corrimos `ctest`; fallo mío. Arreglo: `rb-boot` sale de `IMAGES` y se genera aparte con `make_atr.py --boot`, en
  su propio directorio y manifiesto. Prueba nueva que fija el juego de la Meta A.
- Tras el arreglo: 84 tests de `tools/` OK; `ctest` de `test/host` (build nuevo) 47/47 con `test_meta_a` Passed;
  `rb-boot.atr` con `--boot` mantiene sha256 9bd67a98... (la misma de la corrida); `compare_crc` con el manifiesto
  de `build/boot-image` sobre `run1/e1b.jsonl`: OK, 9 lecturas; `check_layering` y `check_notices` OK.

## Next

Commit y push del arreglo en `feat/e1c-boot-atr` (PR #37), con aprobación; luego actualizar la descripción.
