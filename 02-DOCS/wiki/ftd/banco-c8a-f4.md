---
author: Calaluis
branch: feat/banco-c8a-f4
status: done
---
# Banco C8a, fase F4: imagen por ejecución manual y ejecución por USB

## Intent

Ejecutar el banco C8a en la FujiNet real, solo por USB y sin el Atari, con autorización escrita del responsable
(2026-11-25, «Adelante. Procede con el F4»). Paso de la ruta hacia T9b (cargar y ejecutar
`international_karate.cas` en el 65XE real).

## Scope

Dentro:
- `.github/workflows/ci.yml`: el job `firmware-package-bench-c8a` sube el paquete solo en `workflow_dispatch`
  sobre `main`, 7 días (decisión del responsable; `ref` añadido por la revisión 1, H1).
- Pruebas: `test_the_job_uploads_only_in_a_manual_run_on_main`, `test_a_bench_package_is_uploaded_only_by_a_manual_run`
  y `test_the_condition_reader_rejects_what_it_cannot_read`.
- Tras la fusión: ejecución manual, descarga, verificación, flasheo por COM3, `c8a run`, restauración de menu-sio y
  registro en `docs/mediciones/`.

Fuera:
- Conectar el Atari, medir niveles (C8b), H4, R2, R7, `puertas.json`, `kTapeOutput`, NVS.

## Checklist

- [x] Rama nueva desde `main` `32b9e46` (elegida por el responsable).
- [x] RED — prueba: `test_the_job_uploads_only_in_a_manual_run` falló sin la subida.
- [x] GREEN — prueba: `tools/tests` 319 OK; el YAML se lee; subida y digest con `if: workflow_dispatch`.
- [x] Las pruebas pueden fallar — prueba: subida sin `if` y retención de 90 días fallan.
- [x] Revisión 1, H1 (manual en cualquier rama) — prueba: subida y digest exigen `refs/heads/main`; la condición se
  evalúa para PR, push, manual en `main` y manual en otra rama; 4 mutantes detectados; `tools/tests` 320 OK.
- [x] Ejecución manual en `main` y paquete descargado y verificado — prueba: ejecución `45709127519`, digest del
  zip coincidente, `verify` OK.
- [x] Flasheo por USB (montaje reconfirmado), `c8a run` y captura — prueba: `verify_flash` OK; `c8a run` →
  `C8A REFUSED` (SIO +5V ≈1 V con solo USB). **C8a no pasa.**
- [x] menu-sio restaurado y verificado; registro en `docs/mediciones/` — prueba: `bea39e1` con `verify_flash` OK y
  arranque correcto; `docs/mediciones/banco-c8a-2026-11-25.md`.
- [x] Revisión 1, H1 (medición posterior del pin 10) — prueba: registro §6 separa ADC de GPIO35, valor calculado y
  multímetro del pin 10 (dato del responsable); §8 deja GPIO35, la diferencia y el umbral como pendientes.

## Next step

F4 ejecutada y rechazada por C-1. Pin 10 ya medido con multímetro (0,86–0,88 V con solo USB). Siguiente: medir
GPIO35, explicar la diferencia con el valor calculado y decidir el umbral de C-1 en su propio PR. Repetir `c8a run`
solo con nueva autorización escrita.
