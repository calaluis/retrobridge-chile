# Registro: demo E1 del menú por SIO físico en el 65XE (paquete `menu-sio`)

> **Estado: EJECUTADA EN PARTE (2026-11-21), con el paquete `b8e6152`.** Pasan los criterios 1 (menú al encender) y
> 3 (**A** arranca `DEMO-DOS.ATR` hasta `DEMO DOS OK`). Los criterios 2 y 4 y las lecturas L1 y L2 siguen pendientes.
> **La sesión no cumplió las condiciones de §2 del procedimiento:** la aceptación de riesgo no estaba firmada, el
> adaptador sin pin 8 no tiene evidencia (vía A o B) y la microSD no tenía solo las tres ATR. El propietario conectó
> la FujiNet al 65XE por su cuenta.
> No marca H4 ni M3a como aprobadas: `docs/mediciones/puertas.json` no cambia.

Procedimiento: `docs/hardware/procedimiento-demo-menu-sio.md`.
Spec: `02-DOCS/wiki/sdd/specs/menu-sio-fisico.md` (E1, criterios 1–4).

## 1. Aceptación de riesgo (la firma el responsable)

Con H4 y M3a sin aprobar, acepto por escrito estos riesgos para una sola sesión de demo con el paquete
`retrobridge-fw-menu-sio-fujinet-atari-v1x-b8e6152` (sustituye a `e65c2bd`, que no pasó el banco; ver §4):

| # | Riesgo aceptado | Base | Mitigación en la demo |
|---|---|---|---|
| A1 | DATA IN (pin 3) se transmite sin medir su nivel en el 65XE | C8 superada en el ESP32; GPIO21 sale por un 74LS07 de colector abierto | El firmware libera DATA IN tras cada respuesta y tras cada error (criterio 6, pruebas de host) |
| A2 | Reposo de DATA OUT (pin 5) y de COMMAND (pin 7) sin medir | Llegan al ESP32 a través de 74LS07 | El firmware registra su nivel al arrancar |
| A3 | Corriente que toma la FujiNet del pin 10 sin medir (M6 pendiente) | USB y SIO entran por diodos Schottky | Se lee la tensión por GPIO35; la FujiNet se alimenta por USB |
| A4 | Adaptador sin pin 8, sin mediciones con energía. Vía usada: **PENDIENTE** (A por continuidad o B por inspección) | Mitigación C (`mitigacion-c-motor-pin8.md` §2, §5.2 y §5.3); spec, «Evidencia para la demo física» | Vía A: contacto 8 identificado por continuidad con IO36 antes de retirarlo y abierto después. Vía B: figura con vista identificada (fig. 9-4 de C016555, vista del conector del equipo según su pág. 145; espejada para la cara de acople del cable) y foto C1 sin ambigüedad. En las dos: si hay duda, no se conecta; FujiNet al final y J2 libre |

| Campo | Valor |
|---|---|
| Responsable | |
| Fecha | |
| Firma o confirmación escrita | **PENDIENTE** |

## 2. Resultado

| Paso del procedimiento | Resultado | Evidencia |
|---|---|---|
| §3 Paquete: SHA-256 del zip y `verify` OK | OK con `b8e6152`: zip `a220c1c3…0f2d` (igual al resumen de GitHub del artefacto 14728158699), `SHA256SUMS` sin fallos, `verify` con todas las comprobaciones OK | `Respaldos\FujiNet-RB-menu-sio-b8e6152\` |
| §3 `demo-dos.atr` = `aa96d7dd…d1f1` en la raíz de la microSD | EN PARTE: el archivo de origen tiene `aa96d7dd…d1f1`; la copia en la microSD no se midió. La raíz tenía 17 imágenes (no solo las tres) y `DEMO-DOS.ATR` se añadió después del primer arranque | `microSD\` en la carpeta de respaldo; foto 02 |
| §3.4 Vía usada para el pin 8 (A o B) | **NO DOCUMENTADA** | |
| §3.4 Vía A, A1: contacto 8 con continuidad con IO36 (FujiNet desenergizada); 4 y 6 a masa | PENDIENTE (o «no aplica») | foto A1; multímetro (modelo) |
| §3.4 Vía A, A3: pin 8 abierto de extremo a extremo y contra los otros 12; 1–7 y 9–13 con continuidad | PENDIENTE (o «no aplica») | lecturas |
| §3.4 Vía A, A3: en J1 desenergizada, posición 8 contra IO36 abierta; 4 y 6 a masa | PENDIENTE (o «no aplica») | lecturas |
| §3.4 Vía B, B1–B2: tipo y cara del conector; figura y página usadas; texto literal de su vista; directa o espejada | PENDIENTE (o «no aplica») | foto B2 |
| §3.4 Vía B, B4: 12 contactos completos y hueco en la posición 8 de la figura | PENDIENTE (o «no aplica») | |
| §3.4 Foto C1 (hueco del 8 visible sin ambigüedad) y foto C2 (rótulo) | **NO DOCUMENTADAS** | |
| §4 `write_flash` + `verify_flash` del paquete `menu-sio` | OK con `b8e6152`: «Hash of data verified» ×3 y «verify OK (digest matched)» ×3 | `esptool-write.txt`, `esptool-verify.txt` (MAC enmascarada) |
| §5 Consola sin reinicio al abrir | NO APLICA: la captura del banco reinicia por RTS a propósito; la captura en el 65XE abrió COM3 sin reinicio (no hubo arranque en el registro) | `consola-65xe-L0-L1.txt` |
| §5 Arranque por USB: líneas esperadas, `catalog: 3`; reinicio conocido como máximo una vez | OK salvo el catálogo: `App version: b8e6152`, reinicio conocido una vez, sin pánicos, `catalog: 17`, `D1: menu`, `app_main stack: 1928 B never used` | `consola-banco.txt` |
| §5 Línea base de contadores sin Atari (dos `sio status`) | EN PARTE: un `sio status` con el 65XE apagado: `tramas 0 cortas 0`, `err_rx 0 err_tx 0`, 0 mV; `command_pegado` sube con el Atari apagado (COMMAND bajo sin alimentación) | `consola-banco.txt` |
| §6.1 Criterio 1: menú al encender el 65XE | **PASA** (20:03 aprox.): pantalla «RETROBRIDGE CHILE», fuente 1 SD, D1–D8 sin disco | foto 01 |
| §6.4 Criterio 2: pantallas 2–4 iguales a Altirra | PENDIENTE: hay fotos de la lista de la SD y de D1 montada, sin comparación formal con Altirra | fotos 02 y 03 |
| §6.4 Criterio 3: **A** arranca `DEMO-DOS.ATR` hasta `DEMO DOS OK` | **PASA**: D1 = `DEMO-DOS.ATR` («A ARRANCA»), **A**, pantalla `DEMO DOS OK` | fotos 03 y 04 |
| §6.1 Lectura L0 (`sio status` antes de encender el 65XE) | OK (20:00:45): `tramas 0 cortas 0 command_pegado 1844 err_rx 0 err_tx 0 liberaciones 3689`; órdenes 0; 0 mV | `consola-65xe-L0-L1.txt` |
| §6.4 Lectura L1 (`sio status` con el menú en pantalla, antes de pulsar teclas) | APROXIMADA (20:04:26, menú en pantalla; sin constancia de teclas): `tramas 38 cortas 2 err_rx 0 err_tx 0`, latencia máxima 216 µs, órdenes 2, reintentos 0, NAK 0, 4780 mV | `consola-65xe-L0-L1.txt` |
| §6.5 Lectura L2 (`sio status` con `DEMO DOS OK`) | **NO CAPTURADA**: el arranque de `DEMO-DOS.ATR` fue después de cambiar la microSD, sin consola abierta | |
| §6.5 Entre L1 y L2: `err_rx`, `err_tx` y `command_pegado` sin subir | NO EVALUABLE (falta L2) | |
| §6.6 Criterio 4: el botón A devuelve el menú desde `DEMO-DOS.ATR` en D1 | PENDIENTE | |
| §6.6 Criterio 4: `menu` devuelve el menú tras volver a arrancar `DEMO-DOS.ATR` | PENDIENTE | |
| §8 Restauración de `f405106` (`App version: f405106`) | NO APLICA: `b8e6152` queda instalado para seguir la demo. `f405106` se restauró y verificó tras el fallo de `e65c2bd` (§4 de este registro) | |

**Resultado de la demo E1:** EN PARTE. Criterios 1 y 3 pasan en el 65XE real; criterios 2 y 4, L1 formal y L2
pendientes; condiciones de §2 incumplidas (sin firma, sin evidencia del pin 8).

Navegación: las flechas son CONTROL + `-` `=` `+` `*`. Sin CONTROL no mueven el cursor, por diseño (spec
`menu-navegacion`); en Altirra las flechas del PC ya envían CONTROL.

Fotos de la pantalla del 65XE (del propietario), en `D:\GitHub\Respaldos\FujiNet-RB-menu-sio-b8e6152\fotos\`:
`01-menu-principal.webp`, `02-fuente-sd-17-imagenes.webp`, `03-d1-demo-dos-montada.webp`, `04-demo-dos-ok.png`.

## 3. Datos para completar al ejecutar

- Fecha y hora de la sesión.
- Niveles de COMMAND y DATA OUT en reposo, sin Atari y con el Atari.
- Si apareció el reinicio conocido (`flash read err, 1000` → `RTCWDT_RTC_RESET`), y cuántas veces.
- Lecturas del multímetro del adaptador (§3.4).
- +5 V del SIO (pin 10) con el 65XE encendido.
- `sio status` de §5 (línea base) y de L0, L1 y L2:
  - tramas, cortas, `command_pegado`;
  - errores de recepción y transmisión;
  - liberaciones;
  - latencia última y máxima;
  - órdenes, reintentos y NAK del menú.
- Estado del BASIC al encender: con OPTION o sin él.
- Capturas de consola y fotos, con su ubicación en `D:\GitHub\Respaldos\FujiNet-RB-menu-sio\`.

## 4. Paquetes que no pasaron (2026-11-21)

| Paquete | Qué pasó | Causa | Arreglo (PR #76) |
|---|---|---|---|
| `e65c2bd` | Banco por USB: tras `SIO +5V`, `Double exception` en el core 0 y bucle de pánicos; nunca `D1: menu`. Se restauró `f405106` (write + verify OK, arranca sin pánicos) | `forEachRegularFile` tenía `char path[4096]` en la pila de `app_main` (3584 B). Backtrace decodificado con el ELF exacto: `vfs_fat_stat` (core 0); `prvIdleTask → esp_task_wdt_reset` sobre memoria dañada (core 1) | `e9c0f2b`: el buffer pasa al heap; `ENOMEM` con un error claro |
| `e9c0f2b` | Banco por USB OK. Con el 65XE encendido: `tramas 0 cortas 168`; el Atari nunca recibió respuesta | Con los umbrales por defecto del driver (120 bytes, o 10 caracteres ≈ 5,2 ms), los 5 bytes de la trama llegaban a `uart_read_bytes` después de `kFrameReadUs` (6 ms) | `a724b43`/`b8e6152`: `uart_set_rx_full_threshold(1)` y `uart_set_rx_timeout(1)` |

Capturas en `D:\GitHub\Respaldos\FujiNet-RB-menu-sio\` (`e65c2bd`) y `D:\GitHub\Respaldos\FujiNet-RB-menu-sio-e9c0f2b\`.

## 5. Antecedente (no es esta demo)

La regresión en Altirra con el `retrobridge_host` de `feat/menu-sio-fisico` pasó el 2026-11-21:
- recorrido: menú → `1`, `A`, `1`, `A` → `DEMO DOS OK` → Shift+F5 → menú;
- `host.jsonl`: 7 órdenes del menú, 0 reintentos, 0 NAK;
- evidencia en `D:\rb-e0\m50-sio-fisico\`; la captura `5-arranca-a.png` tiene el SHA-256 `10c6adede691d198…`.

Prueba el mismo programa de menú y la misma imagen, no el SIO físico.
