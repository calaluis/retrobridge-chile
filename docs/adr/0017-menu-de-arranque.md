# ADR-017 — Menú de arranque y dispositivo SIO propio del host

- Estado: Propuesta (2026-11-17), Issue #38 (continuación de #32). Spec `menu-arranque` aprobada el 2026-11-17.
  Enmendada el 2026-11-17 por la Issue #40 (spec `menu-principal`): ver «Enmienda 1». Las decisiones 3 y 4 quedan
  reemplazadas; 1, 2 y 5 siguen vigentes. Enmendada el 2026-11-18 por la Issue #41 (spec `menu-tnfs`): ver
  «Enmienda 2», que reemplaza el punto 4 de la enmienda 1.
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

## Enmienda 1 (2026-11-17, Issue #40): pantalla principal y terminal delgado

Contexto: la Issue #40 pide la pantalla principal de la maqueta aprobada (fuentes, estado de D1 a D8 y C:, pie de
teclas), montar en cualquier unidad y arrancar D1 cuando el usuario lo pida.

1. **Terminal delgado (reemplaza la decisión 3).** El comando de `$5A` pasa a ser `K` (`$4B`). AUX1 lleva el código
   de teclado de `CH` (`$FF`: solo dibujar) y AUX2 un número que cambia con cada tecla: si `SIOV` reintenta el
   mismo comando (mismo AUX1 y AUX2), el host reenvía la respuesta ya armada sin aplicar la tecla otra vez; cada
   arranque del menú empieza dibujando (`$FF`), lo que borra esa memoria. La respuesta tiene 962 bytes: acción (0 mostrar, 1 arrancar D1 con
   `COLDSV`), diseño (0 principal, 1 lista) y 960 bytes de pantalla en códigos internos, que las display lists
   muestran sin copiar. Se envía como COMPLETE + 962 + checksum, en bloques de 256 bytes (NetSIO admite hasta 512
   por bloque). `M` deja de existir y recibe NAK como cualquier otro comando.
2. **La pantalla la dibuja el host (reemplaza la decisión 4).** El disco del menú ya no lleva el catálogo: el host
   decide qué significa cada tecla y dibuja la pantalla entera. El generador exporta las plantillas (título,
   subtítulo y pie con `$42`/`$56` y teclas en inverso) para que el texto fijo tenga un solo origen.
3. **D1 asignada, no servida.** Lo que el usuario elige para D1 se valida y queda asignado; D1 sigue sirviendo el
   menú hasta `A`, que monta lo asignado y ordena el arranque. Con un `FF`, D1 vuelve al menú y lo asignado se
   conserva. D2 a D8 se montan y desmontan enseguida por el buzón (ADR-014) y siguen servidas con el menú en
   pantalla. El host configura 8 unidades y 9 archivos abiertos como máximo.
4. **Fuentes 2 a 8 solo se muestran.** Vienen de `--sources`. Abrirlas necesita un cliente TNFS propio, que no
   existe; `E` necesita entrada de texto en el Atari. Ambas teclas muestran `PENDIENTE` y no cambian nada.

Consecuencias: el programa 6502 ocupa 10 sectores desde `$0700`; la pantalla vive en `$081F`-`$0BDE` (sin cruzar
4 KB) y las dos display lists comparten la página `$07` (solo cambia el byte bajo de `SDLSTL`). Un reinicio en
frío deja la vista en la pantalla principal. En las dos pantallas, la línea de mensajes (avisos, errores y «host no
responde») va debajo del pie de teclas, como última línea (pedido en la revisión de la Issue #40).

## Enmienda 2 (2026-11-18, Issue #41): acción 2, sondeo y fuentes TNFS

Contexto: la Issue #41 abre las fuentes 2 a 8 con un cliente TNFS propio
([ADR-018](0018-cliente-tnfs-de-solo-lectura.md)). Traer un listado o una imagen tarda más de lo que un `SIOV`
espera, así que el Atari necesita una forma de esperar sin perder teclas.

1. **Acción 2 «esperar» (M41-1, M41-16).** La respuesta de 962 bytes admite una tercera acción: 2 muestra la
   pantalla y vuelve a preguntar al cabo de unos 15 tics. Solo la acción 2 espera; la acción 1 y cualquier otra
   distinta de 0 arrancan con `COLDSV`, como antes.
2. **Sondeo con `$FE` (M41-3).** Durante la espera, el 6502 envía `$FE` en AUX1 con un AUX2 nuevo, así que M40-2
   sigue igual. Si el usuario pulsa una tecla durante la espera, se envía esa tecla en lugar del sondeo.
3. **Teclas normalizadas (M41-3).** El 6502 aplica `AND #$3F` a cada tecla antes de enviarla: las teclas reales
   van de `$00` a `$3F`, fuera del sondeo (`$FE`) y del redibujo (`$FF`). El host compara `$FE` y `$FF` antes de
   aplicar la máscara.
4. **Fuentes 2 a 8 (reemplaza el punto 4 de la enmienda 1).** Con un cargador TNFS, una fuente abre su carpeta
   raíz, se navega por carpetas y una imagen elegida se carga en memoria y se monta (D1 asignada, D2 a D8 por el
   buzón). Esc cancela una carga. Sin cargador, las fuentes 2 a 8 siguen mostrando `PENDIENTE`. `E` sigue
   pendiente.

Consecuencias: el programa 6502 ocupa 11 sectores desde `$0700`; la pantalla vive en `$0879`-`$0C38` (sin cruzar
4 KB) y un relleno mantiene las dos display lists en una página. El diseño de la carpeta TNFS (título, filas y
«CARGANDO nn%») está en M41-21.
