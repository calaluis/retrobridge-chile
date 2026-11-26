# 2026-11-26 — cassette-motor-fujinet: el motor FSK de la beta en RetroBridge

Rama `feat/cas-motor-fujinet` desde `main` `8bb25d0`. Documento FTD: `02-DOCS/wiki/ftd/cassette-motor-fujinet.md`.

## Qué se hizo

- Se copiaron tal cual los archivos del responsable del commit `cf3382f` a `components/rb_fujinet_fsk/lib/`.
- ESP-IDF v6.1 obligó a un cambio de dos líneas en `cassetteFSK.cpp` y a dos cabeceras de compatibilidad.
- Se escribieron los adaptadores: cabeceras de FujiNet, `FujiTapeLoop`, `FujiTapeRunner` y `EspFskEngine`.
- `CassetteService` usa el motor nuevo solo con `CONFIG_RB_CAS_ENGINE_FUJINET=y` y DATA IN exclusivo.
- La CI compila la variante y comprueba que el motor solo está en su ELF.
- El empaquetador ya no cuenta las llamadas de `rb_fujinet_fsk`; a cambio exige el motor ausente del paquete.

## Decisiones del responsable

- «Solo mis archivos»: entran los archivos propios; `cassette.cpp` no se copia. La licencia no cambia.
- «Sí, solo leer»: `cassette.cpp` en `cf3382f`, solo para describir su comportamiento.

## Revisión, ronda 1

- Veredicto de la revisión independiente: NO FUSIONAR. Tres hallazgos.
- H1: en menu-sio DATA IN es compartido y el motor exigía exclusivo. Nunca se usaba.
- H2: el fin de cinta no llegaba a `TapeProgress` y `cas status` leía `CassettePlayer`.
- H3: esperas sin límite bloqueaban la tarea de comandos.
- Decisión del responsable: «Integrar compartido». El protocolo de traspaso no cambia; las puertas tampoco.
- Arreglo: `FujiTapeRunner` atiende Take y Release como `CassetteTick`, publica el fin, expone `TapePosition`,
  y sus esperas tienen límite (`kStallMs`). Pruebas RED → GREEN; 13 mutaciones caen.
- Límite anotado: durante la precarga del responsable no se consulta MOTOR (diseño D14).

## Pendiente

- CI de la PR tras el arreglo.
- Segunda revisión independiente. Ninguna prueba en el Atari.
