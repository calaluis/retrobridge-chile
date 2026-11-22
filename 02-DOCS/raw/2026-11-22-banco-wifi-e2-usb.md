# Banco Wi-Fi E2 con alimentación USB — 2026-11-22

Spec: `02-DOCS/wiki/sdd/specs/banco-wifi-e2.md`. Plan: `menu-sio-wifi-tnfs`, T1.

## Montaje

- FujiNet **FN32ROV-1.3-Q24**, la placa identificada por foto en `docs/hardware/fujinet-atari-v1x.md`. La consola
  de este ensayo coincide con ese registro: ESP32-D0WD rev. v1.0, flash GigaDevice de 16 MB y 8 MB de PSRAM.
  Alimentada por USB desde el PC (COM3).
- Observación del banco: el cable SIO estaba físicamente desconectado durante todo el ensayo.
- Paquete `retrobridge-fw-bench-wifi-fujinet-atari-v1x-276ee77` (ejecución de CI 44977410589).
  `firmware_package.py verify`: todas las comprobaciones OK.
- esptool 4.5.1. Grabación en 0x1000, 0x8000 y 0x10000; `verify_flash`: las tres regiones «digest matched».
- Sin instrumento de corriente: este ensayo es funcional, no de consumo.

## NVS (C10)

Leída en memoria, solo se calculó el hash. El contenido no se mostró ni se guardó.

| Momento | SHA-256 de 0x9000+0x6000 |
| --- | --- |
| Antes de grabar el banco | `d8a04e883160efe379c45f9b317884c043eedd81f710366f818bd2b1d7a55c1d` |
| Después del ensayo, antes de restaurar `menu-sio` | `d8a04e883160efe379c45f9b317884c043eedd81f710366f818bd2b1d7a55c1d` |

Resultado: **iguales**.

## Ciclo 1 (consola USB, MAC ocultas)

| Fase | Resultado |
| --- | --- |
| 0 radio apagada | 30 s |
| 1 búsqueda | 12 búsquedas en 30 s |
| 2 conectado | conexión en 1730 ms |
| 3 TNFS | 9 descargas de `NumenPAL.atr` (196 496 bytes), 2,6–5,0 s cada una, SHA-256 OK en todas; 0 fallos |

El ciclo 2 empezó con normalidad. La clave no aparece en la salida; solo su longitud.

## Notas

- La causa de arranque registrada fue `OTHER (7)`: el reinicio por RTS de esta placa pasa por «flash read err» y un
  reinicio del watchdog RTC antes de arrancar. No es un brownout.
- Primer intento: `retrobridge-wifi.txt` tenía la primera línea vacía (`FAIL: network name of 0 bytes`). El banco
  se detuvo sin encender la radio. El responsable corrigió el archivo.

## Restauración

- Paquete `retrobridge-fw-menu-sio-fujinet-atari-v1x-395ea67`: artefacto 14730189831, ejecución 44963760526.
  SHA-256 del ZIP `510fb4d4bf12c34bdb25c59a3a221e48c70d7c50100bf60a853acc6bba26ec25` (igual al indicado por el
  responsable). `SHA256SUMS` y `firmware_package.py verify`: todas las comprobaciones OK.
- Grabado solo con USB. `verify_flash`: las tres regiones «digest matched».
- Lectura del ADC en el arranque: 838 mV en el divisor del pin 10 del SIO (`SIO +5V (pin 10) 838 mV`). Es una
  lectura del ADC, no una medida de tensión calibrada. Que el SIO estaba desconectado físicamente es una
  observación separada del banco (ver «Montaje»).
- Consola: `App version: 395ea67`, `D1: menu. Switch the 65XE on.`, `D1 ready at 144 ms`, 18 imágenes en la
  microSD.
- Los 144 ms se cuentan desde el inicio de la aplicación del ESP32. El tiempo total desde EN es mayor: suma
  la ROM y el cargador de arranque, más el reinicio del watchdog RTC que esta placa hace tras un reinicio por RTS.

## Estado

- Completados: ensayo funcional con USB y restauración de `menu-sio` 395ea67.

## Pendiente

- T1a: medida de corriente por fase. Falta instrumento.
- T1b (con el SIO del 65XE): queda pendiente de T1a, por decisión del responsable.
- La implementación de E2 espera el resultado de T1a y T1b.
