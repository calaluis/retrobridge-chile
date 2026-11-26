---
author: Calaluis
branch: feat/cas-motor-fujinet
status: in-review
---
# Carga `.cas` con el motor FSK de la beta del responsable

## Intent

Que RetroBridge reproduzca `.cas` por el mismo camino que la beta FujiNet (`v1.6.2-atari-fsk-beta.1`, commit
`cf3382f`), con el código original del responsable y adaptadores mínimos. Sin portar el sistema de discos de
FujiNet y sin inventar máquinas de estado nuevas.

## Scope

Dentro:
- Componente `components/rb_fujinet_fsk/`: archivos del responsable tal cual (un cambio de dos líneas) y
  adaptadores escritos para RetroBridge.
- Variante experimental `CONFIG_RB_CAS_ENGINE_FUJINET` (`sdkconfig.cas-fujinet`), solo compilación en la CI.
- ADR-012 enmienda 2, `FUENTE.md`, `docs/diseno/cassette-motor-fujinet.md`, filas en `docs/fuentes.md`.
- Pruebas del host con cintas reales y puerta del empaquetador (`FUJINET_FSK_CHECK`).

Fuera:
- Fusionar, flashear, COM3, el 65XE, GPIO, H4, C8a, R2, R7, `puertas.json` y `kTapeOutput` (sigue en `false`).
- Código de terceros: `cassette.cpp` solo se leyó; `sio.cpp` no se leyó.
- Modo compartido de la UART2 y rebobinar por segundos.

## Checklist

- [x] Rama `feat/cas-motor-fujinet` desde `main` `8bb25d0` — prueba: `git log`.
- [x] Decisiones del responsable: «Solo mis archivos» y «Sí, solo leer» — prueba: ADR-012 enmienda 2.
- [x] Archivos del responsable idénticos al commit fijado, salvo `idf61-rmt-flags` — prueba: `check_fujinet_fsk: OK`
  y sus 9 autopruebas.
- [x] RED → GREEN con cintas reales — prueba: `test_fuji_tape_loop` (10), `test_fuji_tape_runner` (5),
  `test_esp_fsk_engine` (7); 13 mutaciones caen.
- [x] Regresión del host — prueba: `ctest` 106 de 106 con `RB_CAS_SAMPLES`.
- [x] Puerta del empaquetador registrada — prueba: `test_firmware_package` (333 pruebas de `tools/tests` en
  verde) y 4 mutaciones de la puerta nueva que caen; diseño §3 D11.
- [x] Capas y avisos — prueba: `check_layering: OK`, `check_notices: OK`.
- [ ] Compilación ESP-IDF v6.1 de las cuatro variantes y sanitizers — en la CI de la PR.
- [ ] Revisión independiente (ChatGPT) — en la PR.
- [ ] Prueba física autorizada — no forma parte de esta rama.

## Evidence

- Matriz idéntico / adaptador / diferente: `docs/diseno/cassette-motor-fujinet.md` §1.
- Diferencias y supuestos: §3 (D1 a D11).

## Next

Revisión independiente de la PR. No se afirma que `international_karate.cas` cargue en el Atari.
