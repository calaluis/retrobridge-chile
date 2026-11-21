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
- [ ] CI verde: pruebas de host (Linux, ASan), compilación ESP-IDF v6.1 y paquete menu-sio.
- [ ] Banco: arranca por USB, lee la microSD, muestra `D1: menu` y responde a `sio status` sin pánicos.

## Siguiente

Repetir el banco con el paquete nuevo, con autorización para COM3.
