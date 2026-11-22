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
de laboratorio con el amperímetro en serie. Anotar la media de cada fase (y el pico, si hay osciloscopio).
Si alguna fase pasa de 50 mA: parar y llevar las cifras al responsable. T1b no se hace.

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

## T1b: con el SIO del 65XE (sin instrumentos)

Decidido el 2026-11-22: sin medidor, T1 se hace así.

1. USB desconectado. La FujiNet en el SIO del 65XE, con la microSD preparada.
2. Encender el 65XE y dejar correr al menos 3 ciclos (unos 6 minutos).
3. Cortar enseguida si el 65XE se reinicia, la imagen falla o algo se calienta.
4. Apagar, sacar la microSD y leer `retrobridge-banco-wifi.txt`.

Pasa si cada arranque dice `start, reset POWERON`, no hay `reset BROWNOUT` ni arranques nuevos a mitad del ciclo, y
todas las descargas TNFS dan el SHA-256 esperado.

## Volver al firmware anterior

Volver a grabar el paquete `menu-sio` validado. La NVS no se toca en ningún caso.
