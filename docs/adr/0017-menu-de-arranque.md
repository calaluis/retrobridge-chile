# ADR-017 — Menú de arranque y dispositivo SIO propio del host

- Estado: Propuesta (2026-11-17), Issue #38 (continuación de #32). Spec `menu-arranque` aprobada el 2026-11-17.
- Contexto: con `--image`, `retrobridge_host` sirve una sola ATR por ejecución. La Issue #38 pide elegir la imagen
  D1 desde el Atari, con el teclado, sin relanzar el host ni montar discos desde Altirra.

## Contexto

- El Atari arranca desde D1 tras un reinicio en frío. NetSIO (FEP-003) avisa de ese reinicio con `COLD_RESET`
  (`FF`). `F5` (reinicio en caliente) llega como `WARM_RESET` (`FE`).
- Spike del 2026-11-17 en Altirra 4.31 con el hub v0.16: una imagen propia que salta a `COLDSV` (`$E477`) cada
  3 s arrancó 13 veces y el log del host registró un solo `FF`, el del Shift+F5 inicial. Un reinicio por software
  no envía `FF`.
- `SioSession` atiende a D1 por medio de `SioCommandDispatcher`, que ignora otros IDs de dispositivo. `rb_sio` y
  `rb_netsio` no deben cambiar para esto (ADR-016).
- El montaje ya tiene su camino: `MountDiskImage` (abre y valida con `DiskImage::open`) en el contexto de
  almacenamiento, `ApplyDriveRequest` entre transacciones en el de la unidad y `CollectDriveReply` para cerrar lo
  desplazado (ADR-014).

## Decisión

1. **D1 vuelve al menú en cada `FF`.** El observador NetSIO del host marca el reinicio. Tras cada paso de la
   sesión, el host pide montar el menú en cuanto el buzón está libre. Si la espera que vio el `FF` devuelve un
   marco, ese marco se responde vacío y se descarta: el Atari lo reintenta con el menú ya en D1. `FE` no cambia D1.
2. **El menú arranca la elegida por software.** Tras una respuesta afirmativa, el programa del menú salta a
   `COLDSV`. Como no hay `FF`, D1 conserva la imagen elegida y el SO arranca desde ella.
3. **Dispositivo propio `$5A`, comando `M` (`$4D`).** Lo atiende un decorador de `SioTransport` en `host/src`, con
   la misma secuencia que `SioSession`: ACK, al menos 250 µs, COMPLETE, 41 bytes y checksum. AUX1 lleva el índice de
   la entrada (0 a 19). Datos: el byte 0 vale 0 (montada) o 1 (rechazada); los bytes 1 a 40 son una línea de pantalla
   en códigos internos. Otro comando recibe NAK; un checksum malo se ignora. No se usan los IDs de FujiNet (`$70`,
   `$71`) ni los de disquetera (`$31` a `$3F`). `$5A` no figura en ninguna especificación consultada: es una
   elección propia, probada solo en emulación.
4. **El catálogo va dentro del disco del menú.** El host lee la carpeta una vez. Toma los `.atr` en orden
   alfabético sin distinguir mayúsculas, como máximo 20, y escribe sus nombres en las líneas del programa. Así el
   programa no necesita un comando de listado. La ATR del menú (720 x 128) vive en memoria con la ubicación
   reservada `*menu*`.
5. **Programa 6502 propio** (ADR-012), generado por `tools/make_menu.py` en `host/src/menu_program.inc`. Una prueba
   de `tools/tests` exige que el archivo versionado coincida con el generado, y otra ejecuta el programa en un
   intérprete.

## Consecuencias

- Ningún componente cambia; `check_layering` no se ve afectado. Las piezas del menú están en `host/src` y se
  prueban en `test/host` (CI).
- El menú depende del SO del Atari (`SIOV`, `CH`, `COLDSV`, `SDLSTL`). Se probó con el SO que usa Altirra en el
  laboratorio. No se probó en hardware.
- `$5A` podría chocar con un periférico real que use ese ID. Esa es otra razón para no llevar el menú al firmware
  sin revisar antes este ADR.
- Más de 20 imágenes: solo se muestran las 20 primeras; el log del host registra cuántas vio (`atr_seen`).
