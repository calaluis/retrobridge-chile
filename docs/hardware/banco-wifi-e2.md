# Banco Wi-Fi E2 (menu-sio-wifi-tnfs, T1a y T1b)

Firmware de prueba `bench-wifi`. **No se publica.** Mide cuánta corriente pide la FujiNet al usar el Wi-Fi.
No usa ningún pin del SIO, no lee ni escribe la NVS y nunca imprime la clave.

## Preparar la microSD

`retrobridge-wifi.txt` en la raíz, dos líneas:

```text
<nombre de la red>
<clave>
```

Solo se quitan los saltos de línea; los espacios cuentan. Red de 1 a 32 bytes; clave vacía (red abierta) o de
8 a 63 bytes. El archivo no se sube al repositorio.

## Qué hace

Repite este ciclo sin parar, 30 s por fase, y anuncia cada fase en la consola USB y en
`retrobridge-banco-wifi.txt` de la microSD (con el tiempo desde el arranque):

| Fase | Qué hace la radio |
| --- | --- |
| 0 REPOSO | Apagada |
| 1 BUSQUEDA | Búsquedas de redes seguidas |
| 2 CONECTADO | Conecta (se registra cuánto tarda) y queda sin tráfico |
| 3 TNFS | Descarga una y otra vez la ATR de ensayo de `tnfs.calaluis.cl:16384` y compara su SHA-256 |

ATR de ensayo: `/Aportes_Eduardo_Antonio (Chalo)/Demoscene/NumenPAL.atr`, 196 496 bytes.

## T1a: sin el Atari

La FujiNet **no** se conecta al 65XE. Se alimenta a 5 V por USB a través de un medidor en línea, o desde una fuente
de laboratorio con el amperímetro en serie. Anotar por separado la media y el pico de cada fase: son columnas
distintas de la tabla.

- Si alguna media pasa de 50 mA: parar y llevar las cifras al responsable. T1b no se hace.
- Si todas las medias quedan en 50 mA o menos pero los picos no se midieron, el resultado es **preliminar**. T1a no
  confirma el presupuesto eléctrico y T1b no se hace.
- T1a confirma el presupuesto (C1 de E2) solo con medias y picos medidos.

Antes de grabar el banco, medir el reposo del firmware `menu-sio` actual (placa en el menú). Se anota en su propia
fila, aparte de la fase 0 del banco: son firmwares distintos.

| Fila | Firmware | Media (mA) | Pico (mA) |
| --- | --- | --- | --- |
| Reposo E1 | `menu-sio` actual | | |
| Fase 0 radio apagada | `bench-wifi` | | |
| Fase 1 búsqueda | `bench-wifi` | | |
| Fase 2 conectado | `bench-wifi` | | |
| Fase 3 TNFS | `bench-wifi` | | |

Spec del banco: `02-DOCS/wiki/sdd/specs/banco-wifi-e2.md`.

## T1b: con el SIO del 65XE

**Solo si T1a confirma el presupuesto eléctrico** (C1 de E2), con medias y picos medidos. Con un resultado
preliminar, o sin medida, T1b no se hace.
USB desconectado; cable SIO de paso con el pin 10 abierto y el amperímetro en serie. Las fases se leen después en
`retrobridge-banco-wifi.txt`. Pasa si todas las filas quedan en 50 mA o menos.

## Comprobación funcional complementaria

No sustituye a T1a ni a T1b. Con la alimentación que se use (USB en el banco, o el SIO solo dentro de T1b):

1. Dejar correr al menos 3 ciclos (unos 6 minutos).
2. Leer `retrobridge-banco-wifi.txt` (o la consola USB).

Pasa si, durante los 3 ciclos, no hay ningún reinicio imprevisto (ningún `start` nuevo después del arranque
inicial, ningún `reset BROWNOUT`) y todas las descargas TNFS dan el SHA-256 esperado.

El motivo del arranque inicial es aparte y no cuenta como reinicio imprevisto. Lo fija cómo se arrancó la placa:
`POWERON` al conectar la alimentación; `OTHER (7)` tras un reinicio por RTS desde el PC, porque esta placa pasa por un
reinicio del watchdog RTC antes de arrancar.

Estado: **pendiente**. El ensayo con USB del 2026-11-22 en la FN32ROV-1.3-Q24
(`02-DOCS/raw/2026-11-22-banco-wifi-e2-usb.md`) documenta un solo ciclo completo: arranque inicial `OTHER (7)` tras
RTS, ningún reinicio imprevisto y 9 descargas TNFS con el SHA-256 correcto. Faltan los 3 ciclos.

## Volver al firmware anterior

Volver a grabar el paquete `menu-sio` validado. La NVS no se toca en ningún caso.
