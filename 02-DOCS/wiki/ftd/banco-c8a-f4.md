---
author: Calaluis
branch: feat/banco-c8a-f4
status: in-review
---
# Banco C8a, fase F4: imagen por ejecución manual y ejecución por USB

## Intent

Ejecutar el banco C8a en la FujiNet real, solo por USB y sin el Atari, con autorización escrita del responsable
(2026-11-25, «Adelante. Procede con el F4»). Paso de la ruta hacia T9b (cargar y ejecutar
`international_karate.cas` en el 65XE real).

## Scope

Dentro:
- `.github/workflows/ci.yml`: el job `firmware-package-bench-c8a` sube el paquete solo en `workflow_dispatch`, 7
  días (decisión del responsable).
- Pruebas: `test_the_job_uploads_only_in_a_manual_run` y `test_a_bench_package_is_uploaded_only_by_a_manual_run`.
- Tras la fusión: ejecución manual, descarga, verificación, flasheo por COM3, `c8a run`, restauración de menu-sio y
  registro en `docs/mediciones/`.

Fuera:
- Conectar el Atari, medir niveles (C8b), H4, R2, R7, `puertas.json`, `kTapeOutput`, NVS.

## Checklist

- [x] Rama nueva desde `main` `32b9e46` (elegida por el responsable).
- [x] RED — prueba: `test_the_job_uploads_only_in_a_manual_run` falló sin la subida.
- [x] GREEN — prueba: `tools/tests` 319 OK; el YAML se lee; subida y digest con `if: workflow_dispatch`.
- [x] Las pruebas pueden fallar — prueba: subida sin `if` y retención de 90 días fallan.
- [ ] Ejecución manual en `main` y paquete descargado y verificado.
- [ ] Flasheo por USB (montaje reconfirmado), `c8a run` y captura.
- [ ] menu-sio restaurado y verificado; registro en `docs/mediciones/`.

## Next step

Revisión y fusión del PR. Después, la ejecución manual y la descarga.
