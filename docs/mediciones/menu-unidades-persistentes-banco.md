# Unidades persistentes del menú en la FujiNet FN32ROV-1.3-Q24 (2026-11-22)

> Estado: **T6 cerrada.** Confirmados: arranque USB con microSD sin pánicos, guardado de D1–D8 en la microSD
> (con su traza en consola), restauración al arrancar solo por el SIO y vuelta al menú al apagar el Atari con un
> programa corriendo (C10, firmware `0afb749`, PR #84). NVS intacta.
> **H4 y M3a siguen sin aprobar.** menu-sio es un paquete de prueba que transmite por DATA IN.

**Convención:**
- **[H]** hecho comprobado en el equipo;
- **[V]** verificado en el PC;
- **[C]** comprobado en la consola USB (COM3, solo lectura, MAC enmascarada);
- **[P]** comprobado en la pantalla del Atari por el dueño del equipo.

## 1. Objetivo

Validar en hardware real la memoria de unidades (spec `menu-unidades-persistentes`, T6) con el arreglo de pila
(#81) y la guarda NVS (#80): primer arranque solo por SIO, restauración, resultado del guardado y regreso al menú
al apagar y encender el Atari con la FujiNet alimentada por USB.

## 2. Entorno

| Elemento | Valor |
|---|---|
| Placa | FujiNet Atari **FN32ROV-1.3-Q24**, ESP32-D0WD rev. 1.0, flash 16 MB |
| Atari | 65XE por el SIO físico |
| microSD | La de la FujiNet; 18 imágenes en la raíz |
| Firmware anterior | `7a1ebf0` (#80): **pánico en bucle con la microSD** (desborde de pila de `app_main`) |
| Firmware instalado | `067962128c57678bfb395a16c9074177383d9169`; el ESP32 reporta `App version: 0679621` |
| Rama | `test/menu-unidades-banco` = `origin/main` `3193481` + #80 (`7a1ebf0`) + #81 (`9926bbb`) |
| Origen del binario | Job `menu-sio`, ejecución `45056344113` de GitHub Actions (`workflow_dispatch`) |
| Artefacto | `retrobridge-fw-menu-sio-fujinet-atari-v1x-0679621` (id 14766659261) |
| SHA-256 del zip | `da4da2210c2d461e3a52753a483e24c46389e833634d65d07b133eb82f131306` |
| SHA-256 del ELF | `02f5b243d86348f1c46fdf3a984a11e609df71f91802dc5fc801d845e0226a3d` |
| SHA-256 de la app | `91d52125d1d87b567be8a14627963306a9f19920ff6a7954eae0dab7ca7ab3ec` |

## 3. Paquete

- `SHA256SUMS`: todos los archivos OK. `firmware_package.py verify`: «todas las comprobaciones OK». [V]
- Guarda NVS: `sin nvs_* en main/ ni components/ (código)`: OK, ninguna.
- Guarda de pila (marcos del ELF + peor ruta FatFs 816 B + reserva 768 B ≤ pila configurada):

| Tarea | Cadena | Uso / pila |
|---|---|---|
| app_main | finish → load + microSD | 1872 / 3584 |
| app_main | finish → load → decode | 1104 / 3584 |
| app_main | finish → planDriveMemory | 1024 / 3584 |
| rb_drive_save | save → load + microSD | 1808 / 6144 |
| rb_drive_save | save → load → decode | 1040 / 6144 |
| rb_drive_save | save + microSD | 1744 / 6144 |

La guarda rechaza el ELF real de `7a1ebf0` (`app_main` 7008/3584) y el de `fc176ee` (`decode` 1072 B ≥ 1024).

## 4. Instalación

| Paso | Resultado |
|---|---|
| Hash NVS antes (0x9000+0x6000, en memoria, sin guardar contenido) | `d8a04e88…a7d55c1d` [H] |
| `write_flash` 0x1000, 0x8000, 0x10000 | «Hash of data verified» ×3 [H] |
| `verify_flash` | 3 de 3 «verify OK (digest matched)» [H] |
| Hash NVS tras flashear | igual [H] |
| Hash NVS al cerrar el banco | igual: `d8a04e883160efe379c45f9b317884c043eedd81f710366f818bd2b1d7a55c1d` [H] |

## 5. Resultados

Tiempos desde EN: el PC suelta RTS (EN) y marca t0; cada línea lleva `perf_counter` desde t0. Es una **cota
superior** (línea a 115200 baudios y retardo USB). Entre paréntesis, el reloj del ESP32.

| Caso | Prueba | Resultado |
|---|---|---|
| C1 | Arranque USB con microSD, sin pánicos | ✅ [C] un `RTCWDT_RTC_RESET` conocido a ~392 ms; `panics 0` |
| C2 | D1 lista antes de 1 s | ✅ [C] 825.6 ms desde EN (145 ms) |
| C3 | Catálogo | ✅ [C] 18 imágenes a 1654.9 ms |
| C4 | Lectura de la memoria (vacía) | ✅ [C] «no valid slot» en 40 ms; «guardado activo» |
| C5 | Pila de `app_main` medida | ✅ [C] 1960 B nunca usados (la guarda prevé ≥ 1712 B) |
| C6 | Montar `Congo Bongo.atr` en D1 desde la lista y guardar | ✅ [P] «GUARDADO» y la ranura se lee tras reiniciar (C9); [C] con `2552703`: «drive memory version 1 saved in 366 ms» a 35.7 s desde EN |
| C7 | Arrancar D1 con A | ✅ [P] cargó; [C] «D1: Congo Bongo.atr» |
| C8 | Reinicio solo por SIO (sin USB) | ✅ confirmado: [P] D1 = Congo Bongo, «UNIDADES RESTAURADAS»; A la cargó |
| C9 | Lectura de la ranura tras reiniciar | ✅ [C] «drive memory read in 53 ms: slot found» |
| C10 | Apagar y encender el Atari con la FujiNet en USB | ✅ con `0afb749`: [C] «D1: demo-dos.atr» a 120.5 s; «65XE off … D1 back to the menu, D1-D8 kept» y «D1: menu» a 129.8 s; «65XE on (SIO +5V 4780 mV)» a 135.6 s; primera trama 1575 ms después del +5 V, con D1 ya en el menú. Sin `rst:` fuera del arranque inicial. [P] volvió el menú, no el programa. Con `2552703` fallaba (§6) |

## 6. Observaciones

- **Arranque en frío:** al recibir corriente, el primer arranque de la ROM da `flash read err, 1000` y se
  recupera con `RTCWDT_RTC_RESET`; el segundo arranca normal. Es el reinicio por watchdog ya conocido.
- **Consola muda tras reenchufar el USB (`0679621`):** con el ESP32 ya alimentado por el SIO, COM3 abrió sin
  error pero no entregó líneas en dos capturas. Las capturas posteriores se abrieron con un reinicio por EN.
- **C10 fallaba con `2552703`:** con DEMO DOS corriendo, el apagado del 65XE no se detectaba y al encender volvía
  a cargar la imagen. Causa: con el Atari apagado la línea COMMAND queda baja, cada paso SIO termina en
  `TransportFailed` y el +5 V solo se leía tras un paso `Idle`. El PR #84 (`0afb749`) lee el +5 V también tras
  `TransportFailed`; en el banco se detectó el apagado sin reinicio del ESP32.
- **Traza de guardado:** con `0679621` el guardado ocurrió antes de abrir la captura. Con `2552703` quedó en la
  consola. En el ciclo C10 de `0afb749` no apareció, porque D1 ya tenía guardada demo-dos.atr.

## 7. Pendiente para cerrar T6

- [x] Traza de guardado en consola («drive memory version N saved») tras un cambio de unidad (`2552703`).
- [x] C10 completo: FujiNet encendida por USB durante el ciclo del Atari (sin reinicio en la consola) y D1 lista
  antes de la primera trama del Atari (`0afb749`).

## 8. Evidencia

`D:\GitHub\Respaldos\FujiNet-RB-menu-sio-0679621\` (fuera del repo): ZIP y paquete, `esptool-write.txt`,
`esptool-verify.txt`, `nvs-hash-antes.txt`, `nvs-hash-despues.txt`, `nvs-hash-final-banco.txt`,
`consola-usb-con-sd.txt`, `consola-sio-montaje-d1.txt`, `consola-usb-ciclo-atari*.txt` (vacías).
`D:\GitHub\Respaldos\FujiNet-RB-menu-sio-2552703\consola-cierre-t6.txt` (traza de guardado; fallo de C10).
`D:\GitHub\Respaldos\FujiNet-RB-menu-sio-0afb749\consola-c10.txt` (C10 completo). Hash de la NVS sin cambios en
todos los flasheos (`d8a04e88…a55c1d`).
