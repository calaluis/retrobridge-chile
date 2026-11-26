---
author: Calaluis
branch: feat/cas-paridad-fsk
status: in-review
---
# Paridad de la carga `.cas` con la beta FSK del responsable

## Intent

Que la lectura y reproducción de `.cas` de RetroBridge se comporte como la beta FSK del responsable
(`v1.6.2-atari-fsk-beta.1`, PRs #1722, #1756, #1773 y #1804), con su código propio adaptado (ADR-012, enmienda 1),
y dejar escrito qué es igual, qué se corrigió y qué no se puede verificar sin el Atari.

## Scope

Dentro:
- Matriz de paridad: `docs/diseno/cassette-paridad-fsk-fujinet.md`.
- D1: un corte de la señal a mitad de tirada repite la tirada (`cassette_player`, `esp_data_in`).
- D2: carga progresiva solo con un primer bloque de 16 384 bytes o más (`fsk_loader`).
- Pruebas del host y filas en `docs/fuentes.md`.

Fuera:
- Flashear, COM3, el Atari, T9b, H4, R2, R7, C-1, `puertas.json` y `kTapeOutput` (sigue en `false`).
- Rebobinar por segundos (PR #1722): decisión diferida en la spec `cassette-cas`.
- Disco, `.ATR`, `.xex` y TNFS.

## Checklist

- [x] Rama nueva desde `main` `7673980` — prueba: `git log` de `feat/cas-paridad-fsk`.
- [x] ADR-012, enmienda 1 y spec `cassette-cas` leídas — prueba: matriz §1, columna «Referencia», solo código propio.
- [x] Matriz de paridad — prueba: `docs/diseno/cassette-paridad-fsk-fujinet.md` §1.
- [x] D1 en rojo y luego en verde — prueba: matriz §3 (tres pruebas del reproductor, Missile Command real y
  `test_portions_after_the_rmt_ran_dry_cut_the_run`).
- [x] D2 en rojo y luego en verde — prueba: `test_small_first_chunk_waits_for_the_whole_run`, borde de 16 384 bytes.
- [x] Cintas reales: International Karate (bytes, niveles y duraciones), Alien Ambush y Missile Command — prueba:
  0 pruebas ignoradas con `RB_CAS_SAMPLES`.
- [x] Regresión: 102 de 102 en `ctest` — prueba: ejecución local, matriz §3.
- [x] Procedencia — prueba: tres filas nuevas en `docs/fuentes.md` (D1, D2, D7).
- [ ] Revisión independiente (ChatGPT) — en la PR.
- [ ] Prueba física autorizada — no forma parte de esta rama.

## Evidence

- Matriz §3: mensajes de fallo en rojo y resultado en verde.
- ESP-IDF v6.1 `rmt_tx.c:835-840` para D7 (sin cambio de código).

## Next

Revisión independiente de la PR. No se afirma que RetroBridge cargue `.cas` en el Atari.
