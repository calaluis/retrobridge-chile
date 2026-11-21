---
type: ftd
title: menu-sio — buffer de rutas del catálogo fuera de la pila
timestamp: 2026-11-21
---

# menu-sio: buffer de rutas del catálogo fuera de la pila

## Intención

El paquete menu-sio `e65c2bd` entraba en pánico en la FujiNet al leer el catálogo de la microSD. En Altirra
funciona porque allí el menú corre en `retrobridge_host`, en el PC.

## Causa (comprobada)

- `rb::menusio::start()` corre en `app_main`, cuya pila mide 3584 B por defecto.
- `forEachRegularFile()` declaraba `char path[4096]` en la pila → desbordamiento.
- Backtrace decodificado con el ELF exacto (`bdea655d…`):
  - core 0: `vfs_fat_stat`, es decir, `stat()` dentro del bucle;
  - core 1: `prvIdleTask → esp_task_wdt_reset → find_entry_…`, que recorre memoria ya dañada.

## Alcance

- `forEachRegularFile`: el buffer de 4 KB pasa al heap. El límite de rutas largas no cambia: se siguen
  saltando las que no caben.
- Si la reserva falla, devuelve false con `errno = ENOMEM`; `menu_sio` lo informa con un error claro.
- `menu_sio` registra la pila de `app_main` que nunca se usó, como evidencia para el banco.

## Lista

- [x] Buffer fuera de la pila, con error ENOMEM.
- [x] Backtrace decodificado.
- [x] CI verde en `e9c0f2b`: pruebas de host (Linux, ASan), compilación ESP-IDF v6.1 y paquete menu-sio.
- [x] Banco de `e9c0f2b`: arranca por USB, `catalog: 17`, `D1: menu`, dos `sio status`, sin pánicos.
- [x] UART corregida en `a724b43`. Con el 65XE encendido, `e9c0f2b` vio 168 tramas cortas y 0 completas.
- [x] CI verde en `b8e6152`, con el fake de IDF y la prueba de los umbrales; un mutante con umbral 2 falla.
- [x] Con el 65XE y `b8e6152`: `tramas 38 cortas 2`, `err_rx 0 err_tx 0`, latencia máxima 216 µs, el menú en
  pantalla y `DEMO-DOS.ATR` hasta `DEMO DOS OK` (fotos 01–04 en `Respaldos\FujiNet-RB-menu-sio-b8e6152\fotos\`).

## Segundo fallo: la UART entrega tarde los bytes

- `waitCommandFrame` lee los 5 bytes con un plazo de `kFrameReadUs` = 6 ms desde que baja COMMAND.
- Con los umbrales por defecto del driver (120 bytes, o 10 caracteres de línea en reposo ≈ 5,2 ms), los bytes
  llegan a `uart_read_bytes` hacia los 9 ms. Toda trama queda corta.
- Arreglo: `uart_set_rx_full_threshold(kUart, 1)` y `uart_set_rx_timeout(kUart, 1)` en `EspSioUart::begin()`.
  El empaquetador (`MENU_SIO_CALLS`) cuenta las dos llamadas nuevas.

## Siguiente

Fusionar el PR #76. Lo que falta de T7 está en `docs/mediciones/menu-sio-fisico-registro.md`.
