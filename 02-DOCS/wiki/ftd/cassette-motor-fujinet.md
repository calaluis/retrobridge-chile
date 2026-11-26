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
- Rebobinar por segundos.
- El modo compartido de la UART2 quedaba fuera. La revisión de la PR (H1) mostró que así el motor no se alcanza en
  menu-sio. El responsable eligió «Integrar compartido»: entra, con el mismo protocolo de traspaso.

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
- [x] Revisión independiente, ronda 1 (ChatGPT) — NO FUSIONAR, hallazgos H1, H2 y H3 registrados en la PR.
- [x] H1, motor inalcanzable en menu-sio — prueba: `test_fuji_shared` (2) con `EspDataIn` compartido,
  `DataInHandover`, `TapeProgress` y el código del responsable; diseño §2.1 y D7.
- [x] H2, fin de cinta y `cas status` — prueba: `test_fuji_shared`, `test_fuji_tape_runner`,
  `test_cassette_commands` (`test_status_follows_the_engine_that_plays`).
- [x] H3, esperas sin límite — prueba: `test_fuji_tape_runner` (16): línea que no acepta nada, velocidad
  rechazada, comando en espera; diseño D12 a D16.
- [x] Mutaciones de H1-H3 — prueba: 13 de 13 caen (runner, bucle y comandos).
- [x] Revisión independiente, ronda 2 — H1, H2 y H3 cerrados; hallazgo nuevo R2-H4 (velocidad antigua
  restaurada tras un Take) registrado en la PR.
- [x] R2-H4 — prueba: `test_fuji_tape_runner` (19): cinta nueva sin `baud`, MOTOR apagado y encendido, activación
  rechazada; 2 mutaciones caen; diseño D17.
- [ ] Revisión independiente, ronda 3 — en la PR.
- [ ] Prueba física autorizada — no forma parte de esta rama.

## Evidence

- Matriz idéntico / adaptador / diferente: `docs/diseno/cassette-motor-fujinet.md` §1.
- Diferencias y supuestos: §3 (D1 a D17).
- Límite: `check_fujinet_fsk` compara con el manifiesto de la propia PR, no con el repositorio del responsable.

## Next

Tercera revisión independiente de la PR. No se afirma que `international_karate.cas` cargue en el Atari.
