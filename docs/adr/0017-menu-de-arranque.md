# ADR-017 — Menú de arranque y dispositivo SIO propio del host

- Estado: Propuesta (2026-11-17), Issue #38 (continuación de #32). Spec `menu-arranque` aprobada el 2026-11-17.
  Enmendada el 2026-11-17 por la Issue #40 (spec `menu-principal`): ver «Enmienda 1». Las decisiones 3 y 4 quedan
  reemplazadas; 1, 2 y 5 siguen vigentes. Enmendada el 2026-11-18 por la spec `menu-tnfs` (PR #44): ver
  «Enmienda 2», que reemplaza el punto 4 de la enmienda 1. Enmendada el 2026-11-19 por la spec
  `menu-navegacion`: ver «Enmienda 3», que amplía el punto 3 de la enmienda 2, y «Enmienda 4», que cambia
  sus consecuencias tras la demo. «Enmienda 5» cierra el límite conocido del punto 2 de la enmienda 4. «Enmienda 6» (spec
  `menu-editar-servidor`) agrega la acción 3, el comando `T` y el código `$E0`. «Enmienda 7» (spec
  `menu-borrar-fuente`) cambia el punto 3 de la enmienda 6: el texto vacío borra la fuente. «Enmienda 8» (spec `menu-escritura-disco`) agrega la marca « SL».
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

## Enmienda 2 (2026-11-18, spec `menu-tnfs`, PR #44): acción 2, sondeo y fuentes TNFS

Contexto: la spec `menu-tnfs` abre las fuentes 2 a 8 con un cliente TNFS propio
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

## Enmienda 3 (2026-11-19, spec `menu-navegacion`): cursor, joystick y páginas

Contexto: el responsable pidió elegir con un cursor, por flechas o joystick, y confirmar con RETURN o el botón
rojo. También pidió paginar las carpetas con más de 20 imágenes. El host sigue siendo el único dueño de la
pantalla: el 6502 solo traduce entradas a códigos.

1. **Códigos de navegación (M42-1; amplía el punto 3 de la enmienda 2).** Las teclas reales siguen en `$00`-`$3F`
   tras `AND #$3F`. Cinco códigos nuevos van por encima de la máscara y lejos de `$FE` y `$FF`: `$40` arriba,
   `$41` abajo, `$42` izquierda, `$43` derecha, `$44` fuego. Las flechas son Control + `-` `=` `+` `*`
   (CH `$8E` `$8F` `$86` `$87`, también con Shift); `=` solo sigue siendo `$0F`. El joystick es el del puerto 1
   (`STICK0` `$0278`, `STRIG0` `$0284`), y se lee en la espera de tecla de la acción 0 y en la espera de la
   acción 2.
2. **Repetición y fuego.** Una dirección nueva se envía al instante. Si se mantiene, se repite a los 30 cuadros y
   después cada 9 (31 y 10 cuando cada paso va al host, desde la Enmienda 10). La cuenta solo avanza en los cuadros que el 6502 ve, así que una respuesta lenta solo la
   pausa. Una diagonal no envía nada. El fuego envía `$44` una vez por pulsación, al pasar de suelto a pulsado.
   Un botón ya pulsado al arrancar no cuenta.
3. **El host reconoce `$40`-`$44` antes de la máscara (M42-2).** RETURN (`$0C`) y `$44` confirman lo marcado,
   igual que su letra o su dígito. El host dibuja el cursor en vídeo inverso, en las 40 columnas de la línea.
4. **Listas de hasta 999 entradas en páginas de 20 (M42-3).** Arriba y abajo mueven una fila y cruzan de página
   en el borde. Izquierda y derecha cambian de página y dejan el cursor en la fila 1. Las letras A-T eligen en la
   página visible. Debajo del pie aparece `PAGINA n DE m (t)`, o `(999 DE t)` si hay más de 999. Ese texto
   reemplaza a `MOSTRANDO 20 DE n` de M41-21. Esc vuelve a la página y fila desde donde se entró, hasta 32
   niveles de carpetas.
5. **Elegir unidad sobre la pantalla principal (M42-4).** Al montar o desmontar, el cursor recorre D1 a D8 con su
   contenido. El mensaje es `UNIDAD PARA: <nombre>`. Los dígitos siguen valiendo. En la principal, el cursor
   recorre las 8 fuentes, también las vacías, y RETURN equivale a su dígito.

Consecuencias: el programa 6502 ocupa 12 sectores desde `$0700` (eran 11). La pantalla vive en `$08F0`-`$0CAF`,
sin cruzar 4 KB. El pie de la lista pasa a `A-T ELEGIR`, `RET OK`, `<> PAG`, `ESC VOLVER`, en 40 columnas con sus
marcos gráficos. Cada movimiento es una transacción SIO más, dentro de los 250 ms de `key()`. El registro
`menu_entry` numera las imágenes locales desde 1 en lugar de A-T.

## Enmienda 4 (2026-11-19, spec `menu-navegacion`, tras la demo): cursor local y línea de ruta

Contexto: en la demo en Altirra el responsable pidió dos cambios. Mover el cursor no debe procesar, salvo RETURN o
el botón rojo. Cada movimiento era una transacción SIO con la pantalla entera (962 bytes, unos 0,6 s). Además, el
título debe ser inmutable: la fuente y la ruta van en otro lugar.

1. **Cursor local en el 6502 (M42-5).** El host escribe un bloque de 5 bytes al final de la pantalla, en
   `kMenuCursor` (955), fuera de todo layout: base de la fila 0, filas, fila marcada y qué páginas hay (bit 0: una anterior; bit 1: una siguiente). Con la
   acción 0 y filas > 0, arriba y abajo mueven la barra en el Atari, sin SIO. RETURN y el botón rojo envían
   `$C0` + fila (rango de Control+Shift, lejos de las teclas tras la máscara, de `$40`-`$44`, `$FE` y `$FF`). En el
   borde de la página, e izquierda y derecha, el 6502 pregunta al host solo si hay una página en esa dirección
   (arriba e izquierda: la anterior; abajo y derecha: la siguiente); si no, la barra queda en su fila y no se
   envía nada. Con 0 filas (carga en curso, lista vacía) o sin respuesta del host, todo va al host, como en la
   enmienda 3.
2. **El host y la fila del Atari.** `$C0` + fila fija la fila de la pantalla mostrada y equivale a RETURN; una
   fila que no existe no hace nada. Arriba en una lista parte de la fila 0 y abajo de la última, que es desde donde
   las envía el 6502. Límite conocido, cerrado por la enmienda 5: una tecla que el host no usa redibujaba la barra en la fila que el host dibujó
   la última vez.
3. **Título fijo y línea de ruta (M42-6).** El título de la lista es el de la principal, `RETROBRIDGE CHILE`, y no
   cambia. La fuente y la carpeta (`FUENTE n: <ruta>`, recortada por la izquierda con `..`) van en una línea de 40
   columnas donde estaban las 8 líneas en blanco bajo el título. La pantalla mide lo mismo.

Consecuencias, en lugar de las de la enmienda 3: el programa 6502 ocupa 13 sectores desde `$0700`. La pantalla
vive en `$09B0`-`$0D6F`, sin cruzar 4 KB. En la lista, las entradas empiezan en 60, el pie en 860 y el mensaje en
900. Moverse dentro de una página no usa SIO; RETURN, el botón rojo y cada cambio de página son una transacción,
dentro de los 250 ms de `key()`.

## Enmienda 5 (2026-11-19, rama `fix/menu-fila-en-daux2`): la fila viaja en AUX2

Contexto: el límite conocido del punto 2 de la enmienda 4. El usuario mueve la barra en el Atari y pulsa una
tecla que el host no usa: la barra vuelve a la fila que el host dibujó la última vez.

1. **AUX2 lleva la secuencia y la fila.** Los bits 7-5 son el número de secuencia, que cambia con cada envío. Los
   bits 4-0 son la fila que marca la barra local, o `$1F` (`kMenuNoRow`) si la pantalla mostrada no tiene cursor
   local. Los reintentos de SIOV repiten el byte entero, así que el host los sigue reconociendo.
2. **El host parte de esa fila.** Antes de procesar una tecla (no `$FF` ni `$FE`), el host pone el cursor de la
   pantalla mostrada en esa fila. Una fila que no existe no cambia nada. Con `kMenuNoRow`, el host conserva la suya.

Consecuencias: el programa 6502 ocupa 14 sectores desde `$0700`. La pantalla vive en `$09C2`-`$0D81`, sin cruzar
4 KB. Una tecla que el host no usa deja la barra donde la muestra el Atari.

## Enmienda 6 (2026-11-19, spec `menu-editar-servidor`): editar un servidor con E

Contexto: `E` mostraba `PENDIENTE`. Faltaban una entrada de texto en el Atari y un archivo `--sources` que el host
pueda escribir. Escribir cada tecla con `K` costaría una pantalla entera (962 bytes) por letra. `SioTransport` no
recibe datos del Atari, y cambiarlo choca con ADR-016.

1. **Acción 3 «editar» (M43).** El host responde a `E` con la pantalla principal y la acción 3. Tres bytes en
   `kMenuEdit` (952), justo antes del bloque del cursor, dan la posición del campo en la pantalla (bajo y alto) y
   el largo del texto. El campo mide 32 columnas y empieza después de `n  ` en la fila de la fuente. El 6502
   edita sin SIO: letras (minúsculas; con Shift, mayúsculas), dígitos, `.` y `-`; Delete borra; el cursor es la
   celda en vídeo inverso que sigue al texto. Control y las demás teclas no escriben.
2. **Comando `T` (`$54`) de `$5A`.** Con RETURN, el 6502 envía las 32 posiciones del campo en ASCII, una por
   comando: AUX1 la posición y AUX2 el caracter. No hay datos en ningún sentido (`DSTATS` = 0): ACK, al menos
   250 µs y COMPLETE. Escribir dos veces la misma posición da el mismo texto, así que un reintento de `SIOV` no
   necesita memoria en el host. Si un `T` falla, el 6502 muestra «host no responde» y no guarda.
3. **`K` con `$E0` guarda; Esc cancela.** Tras el texto, el 6502 envía `K` con `$E0` (fuera de `$C0`-`$D3`, de
   `$40`-`$44`, `$FE` y `$FF`) y una secuencia nueva en AUX2, sin fila. El host compara `$E0` antes de la máscara.
   Un texto válido se escribe en `--sources`; uno no válido deja la acción 3 con el texto escrito; vacío equivale a
   Esc. Esc envía `K` con `$1C` y el host vuelve a la principal.
4. **El cambio vale sin relanzar.** `main.cpp` crea un enlace TNFS para cada fuente 2 a 8, configurada o no. El
   enlace guarda su propia copia del nombre. `MenuLoader::changeServer` la cambia en el hilo del cargador antes del
   siguiente trabajo, después de un UMOUNT sin espera a la sesión anterior.
5. **Sin respuesta, se vuelve a abrir (demo).** Si un trabajo termina en «no responde», el cargador cierra el
   enlace de esa fuente. El siguiente trabajo resuelve el nombre otra vez. En la demo, un registro DNS corregido
   con el host en marcha seguía apuntando a la dirección vieja.
6. **«Host no responde» limpia la pantalla (demo).** Al fallar `SIOV`, el 6502 borra todo menos el título, que es
   igual en las dos pantallas, y muestra la principal con el aviso. Antes la display list principal mostraba lo que
   quedaba en el buffer: una lista, o dos respuestas mezcladas cuando dos hosts atendían el mismo hub.

Consecuencias: el programa 6502 ocupa 16 sectores desde `$0700`. La pantalla vive en `$0B16`-`$0ED5`, sin cruzar
4 KB. Guardar cuesta 33 transacciones cortas y una pantalla. El host escribe un archivo en el hilo SIO; es pequeño
y cabe en los 250 ms de `key()`.

## Enmienda 7 (2026-11-19, spec `menu-borrar-fuente`): el texto vacío borra la fuente

Contexto: `E` agregaba y cambiaba servidores, pero no los quitaba. El responsable eligió reusar `E` antes que una
tecla nueva con confirmación.

1. **Vacío borra (M44), reemplaza «vacío equivale a Esc» del punto 3 de la enmienda 6.** `K` con `$E0` y las 32
   posiciones en blanco sobre una fuente configurada quitan su línea de `--sources`. Las fuentes siguientes suben
   un número (el formato no guarda huecos). Mensaje `FUENTE n BORRADA`; evento `menu_source_deleted`. Sobre una
   fila vacía sigue siendo Esc. La fila de edición dice `RETURN GUARDA, VACIO BORRA, ESC CANCELA`.
2. **Misma escritura que M43.** `writeSourcesFile` con nombre nulo omite esa línea; conserva comentarios y las
   demás; escribe `.tmp` y lo pone en lugar del original. Si falla, nada cambia y sale `NO SE PUDO GUARDAR EL
   ARCHIVO`.
3. **Los enlaces siguen a los números.** El host llama a `changeServer` para la fuente borrada y cada una que
   cambió de número, con el nombre que quedó en ese número (vacío si ya no hay). Cada sesión vieja termina con
   UMOUNT sin espera, como en el punto 4 de la enmienda 6.

Consecuencias: solo cambia el host; el programa 6502 y `menu_program.inc` no cambian. Un RETURN con el texto vacío
borra sin pedir confirmación: es un riesgo aceptado por el responsable. Un reintento de `SIOV` del mismo `K` se
descarta como siempre (M40-2), así que no borra dos fuentes.

## Enmienda 8 (2026-11-19, spec `menu-escritura-disco`): marca de solo lectura

Contexto: [ADR-019](0019-escritura-de-sectores.md) permite escribir en las ATR de `--images`. El usuario debe ver
en el menú qué unidades no aceptan escrituras.

1. Cada línea de unidad del menú muestra « SL» al final si la unidad está protegida: ATR con atributo de solo
   lectura o imagen de una fuente TNFS.
2. El nombre se corta a 29 columnas para dejar sitio a la marca. Las unidades sin protección no llevan marca.
3. La marca se fija al asignar D1, al montar y al arrancar. Una unidad vacía no lleva marca.

Consecuencias: solo cambia el host; el programa 6502 no cambia. La marca dice lo que el host sabe al montar; si el
atributo cambia después, la marca no se actualiza hasta el siguiente montaje.

## Enmienda 9 (2026-11-20, spec `menu-formatear-disco`): crear una ATR con N

Contexto: para formatear un disco nuevo, el usuario necesita un archivo ATR vacío en la carpeta local. Hasta ahora
había que crearlo en el PC.

1. La tecla `N` de la pantalla principal crea una ATR en la carpeta `--images`, en tres pasos:
   1. **Nombre.** El 6502 lo edita en la fila de mensajes, después de `NOMBRE:`. Usa la acción 3, igual que el
      editor de servidores (Enmienda 6). El nombre tiene de 1 a 25 caracteres de A-Z, 0-9, `-` y `_` (M46-3). Las
      minúsculas valen como mayúsculas. Se rechazan los nombres de dispositivo de Windows (CON, NUL, COM1...).
   2. **Densidad.** `S` simple (720 x 128) o `M` media (1040 x 128).
   3. **Unidad.** `2`..`8` o RETURN sobre la barra. D1 no se ofrece, porque sirve el menú.
   Esc cancela en cualquier paso.
2. El archivo `<NOMBRE>.ATR` se crea con apertura exclusiva: un archivo existente nunca se sobrescribe. Lleva la
   cabecera de 16 bytes y todos los sectores en `$00`, sin formatear; el DOS lo formatea después.
3. El archivo entra en la lista en su lugar alfabético y se monta en la unidad elegida. Las posiciones guardadas de
   las demás unidades se corren, así que cada unidad conserva su nombre.
4. Mensajes:
   - ` NOMBRE NO VALIDO: LETRAS, 0-9 Y -`
   - ` YA EXISTE <NOMBRE>.ATR`
   - ` LISTA LLENA: 999 IMAGENES` (antes de pedir el nombre)
   - ` SIN CARPETA LOCAL`
   - ` ERROR: NO SE PUDO CREAR`
5. Evento `menu_create` con nombre, densidad, unidad y resultado
   (`created`, `exists`, `invalid_name`, `io`, `full`, `no_folder`, `not_listed`).

Consecuencias: solo cambia el host; el programa 6502 no cambia. El editor del 6502 no tiene `_`, así que el
mensaje no lo anuncia, aunque la regla lo acepta. Una ATR creada queda en la carpeta aunque el montaje falle.

## Enmienda 10 (2026-11-20, spec `menu-presentacion`): carga sin barrido y pie con todas las teclas

Contexto: la pantalla nueva se veía dibujarse de arriba hacia abajo mientras llegaba, porque SIOV escribía la
respuesta en la memoria que muestran las listas de visualización. Además, el pie no mostraba `N`. Fuente: *Altirra
Hardware Reference Manual* (ANTIC: `DLISTL`; SO: `SDLSTL`, `RTCLOK`).

1. **Búfer oculto.** La respuesta de `K` llega a un búfer de 962 bytes fuera de la vista: RAM en la primera página
   después del bloque que carga el arranque (hoy `$1000`-`$13C2`). No se carga del disco, así que el bloque no crece
   por él. El generador falla si no cabe bajo `$4000`.
2. **Aviso.** Antes de cada `K`, salvo la consulta `POLL` de la acción 2, el 6502 escribe ` CARGANDO...` en la fila
   de mensajes de la pantalla que se ve (la de la principal o la de la lista). En la acción 2 se sigue viendo el
   ` CARGANDO nn%` del host.
3. **Mostrar de una vez.** Si SIOV termina bien, el 6502 espera un cambio de `RTCLOK` (borrado vertical), pone la
   lista de visualización del layout nuevo en `SDLSTL` y en `DLISTL`, y copia el búfer a la pantalla de arriba hacia
   abajo. Si SIOV falla, no se copia nada: queda la pantalla vieja y sale ` ERROR: EL HOST NO RESPONDE`, como antes;
   ahora además el layout visible vuelve a la principal.
4. **Pie principal.** `[1-8] FUENTE`, `[E] SERVIDOR`, `[N] NUEVA ATR` / `[A] ARRANCAR D1`, `[U] DESMONTAR UNIDAD`.
   El pie de la lista no cambia.

Consecuencias: el protocolo con el host no cambia. El programa pasa de 16 a 17 sectores. Cada respuesta se muestra
un cuadro más tarde (la espera del borrado vertical): la repetición del joystick que va al host pasa de 30/9 a 31/10
cuadros (Enmienda 3, punto 2); el cursor local no cambia. La demo en Altirra no mostró barrido.
