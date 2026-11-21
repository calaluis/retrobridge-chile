# Registro: demo E1 del menú por SIO físico en el 65XE (paquete `menu-sio`)

> **Estado: PENDIENTE.** La demo no se ha ejecutado. La aceptación de riesgo no está firmada.
> No marca H4 ni M3a como aprobadas: `docs/mediciones/puertas.json` no cambia.

Procedimiento: `docs/hardware/procedimiento-demo-menu-sio.md`.
Spec: `02-DOCS/wiki/sdd/specs/menu-sio-fisico.md` (E1, criterios 1–4).

## 1. Aceptación de riesgo (la firma el responsable)

Con H4 y M3a sin aprobar, acepto por escrito estos riesgos para una sola sesión de demo con el paquete
`retrobridge-fw-menu-sio-fujinet-atari-v1x-e65c2bd`:

| # | Riesgo aceptado | Base | Mitigación en la demo |
|---|---|---|---|
| A1 | DATA IN (pin 3) se transmite sin medir su nivel en el 65XE | C8 superada en el ESP32; GPIO21 sale por un 74LS07 de colector abierto | El firmware libera DATA IN tras cada respuesta y tras cada error (criterio 6, pruebas de host) |
| A2 | Reposo de DATA OUT (pin 5) y de COMMAND (pin 7) sin medir | Llegan al ESP32 a través de 74LS07 | El firmware registra su nivel al arrancar |
| A3 | Corriente que toma la FujiNet del pin 10 sin medir (M6 pendiente) | USB y SIO entran por diodos Schottky | Se lee la tensión por GPIO35; la FujiNet se alimenta por USB |
| A4 | Adaptador sin pin 8, sin mediciones con energía. Vía usada: **PENDIENTE** (A por continuidad o B por inspección) | Mitigación C (`mitigacion-c-motor-pin8.md` §2, §5.2 y §5.3); spec, «Evidencia para la demo física» | Vía A: contacto 8 identificado por continuidad con IO36 antes de retirarlo y abierto después. Vía B: figura del manual (C016555 §9) para la cara identificada y foto C1 sin ambigüedad. En las dos: si hay duda, no se conecta; FujiNet al final y J2 libre |

| Campo | Valor |
|---|---|
| Responsable | |
| Fecha | |
| Firma o confirmación escrita | **PENDIENTE** |

## 2. Resultado

| Paso del procedimiento | Resultado | Evidencia |
|---|---|---|
| §3 Paquete: SHA-256 del zip = `b1b7455d…a902` y `verify` OK | PENDIENTE | |
| §3 `demo-dos.atr` = `aa96d7dd…d1f1` en la raíz de la microSD | PENDIENTE | |
| §3.4 Vía usada para el pin 8 (A o B) | PENDIENTE | |
| §3.4 Vía A, A1: contacto 8 con continuidad con IO36 (FujiNet desenergizada); 4 y 6 a masa | PENDIENTE (o «no aplica») | foto A1; multímetro (modelo) |
| §3.4 Vía A, A3: pin 8 abierto de extremo a extremo y contra los otros 12; 1–7 y 9–13 con continuidad | PENDIENTE (o «no aplica») | lecturas |
| §3.4 Vía A, A3: en J1 desenergizada, posición 8 contra IO36 abierta; 4 y 6 a masa | PENDIENTE (o «no aplica») | lecturas |
| §3.4 Vía B, B1–B2: tipo y cara del conector; figura del manual usada | PENDIENTE (o «no aplica») | foto B2 |
| §3.4 Vía B, B4: 12 contactos completos y hueco en la posición 8 de la figura | PENDIENTE (o «no aplica») | |
| §3.4 Foto C1 (hueco del 8 visible sin ambigüedad) y foto C2 (rótulo) | PENDIENTE | fotos C1 y C2 |
| §4 `write_flash` + `verify_flash` del paquete `menu-sio` | PENDIENTE | |
| §5 Consola sin reinicio al abrir | PENDIENTE | |
| §5 Arranque por USB: líneas esperadas, `catalog: 3`; reinicio conocido como máximo una vez | PENDIENTE | |
| §5 Línea base de contadores sin Atari (dos `sio status`) | PENDIENTE | |
| §6.1 Criterio 1: menú al encender el 65XE | PENDIENTE | |
| §6.4 Criterio 2: pantallas 2–4 iguales a Altirra | PENDIENTE | |
| §6.4 Criterio 3: **A** arranca `DEMO-DOS.ATR` hasta `DEMO DOS OK` | **PENDIENTE** | |
| §6.1 Lectura L0 (`sio status` antes de encender el 65XE) | PENDIENTE | |
| §6.4 Lectura L1 (`sio status` con el menú en pantalla, antes de pulsar teclas) | PENDIENTE | |
| §6.5 Lectura L2 (`sio status` con `DEMO DOS OK`) | PENDIENTE | |
| §6.5 Entre L1 y L2: `err_rx`, `err_tx` y `command_pegado` sin subir | PENDIENTE | |
| §6.6 Criterio 4: el botón A devuelve el menú desde `DEMO-DOS.ATR` en D1 | PENDIENTE | |
| §6.6 Criterio 4: `menu` devuelve el menú tras volver a arrancar `DEMO-DOS.ATR` | PENDIENTE | |
| §8 Restauración de `f405106` (`App version: f405106`) | PENDIENTE | |

**Resultado de la demo E1:** PENDIENTE.

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

## 4. Antecedente (no es esta demo)

La regresión en Altirra con el `retrobridge_host` de `feat/menu-sio-fisico` pasó el 2026-11-21:
- recorrido: menú → `1`, `A`, `1`, `A` → `DEMO DOS OK` → Shift+F5 → menú;
- `host.jsonl`: 7 órdenes del menú, 0 reintentos, 0 NAK;
- evidencia en `D:\rb-e0\m50-sio-fisico\`; la captura `5-arranca-a.png` tiene el SHA-256 `10c6adede691d198…`.

Prueba el mismo programa de menú y la misma imagen, no el SIO físico.
