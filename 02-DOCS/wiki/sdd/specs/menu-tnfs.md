---
type: spec
title: Spec — Fuentes TNFS en el menú del Atari (solo lectura)
description: QUÉ y POR QUÉ de abrir un servidor TNFS desde las fuentes 2 a 8 del menú, navegar sus carpetas, elegir una ATR y arrancarla desde D1.
tags: [sdd, spec, host, netsio, menu, tnfs]
timestamp: 2026-11-18T05:00:00Z
topic: sdd
slug: menu-tnfs
status: clarified
---

# Spec — Fuentes TNFS en el menú del Atari

> Slug: `menu-tnfs` · Estado: aclarada y aprobada por el responsable el 2026-11-18 («Aprobar spec aclarada») · Creada: 2026-11-18
> Origen: pedido del responsable del 2026-11-18, tras la fusión del PR #41 (merge `170a411` en main).
> Continúa `menu-principal` (Issue #40) y ADR-017. Hereda: [constitución](../constitution.md), que sigue en
> borrador: no aporta principios ratificados, así que las restricciones vienen de los ADR y de las decisiones
> M40-1 a M40-5.
> Modo: por fases. Cada fase espera la aprobación del responsable.

## Problema y por qué

Las fuentes 2 a 8 del menú muestran nombres de servidores, pero al pulsarlas solo aparece
«PENDIENTE: FALTA EL CLIENTE TNFS». Las imágenes que viven en un servidor TNFS del laboratorio no se pueden usar
desde el Atari: hay que copiarlas a la carpeta de `--images` y relanzar el host. El servidor organiza las
imágenes en carpetas, y el menú no tiene cómo recorrerlas.

## Cost of not building it

- Cada imagen nueva del servidor exige copiarla a mano al PC y relanzar `retrobridge_host`.
- Las fuentes 2 a 8 siguen siendo decorativas: una tecla dibujada que no hace nada.
- El objetivo del producto (usar D1 desde el host propio) queda limitado a una carpeta local plana.
- El firmware ESP32 no tendrá carpeta local de PC: sin un cliente TNFS propio no hay camino para sus imágenes
  remotas.

## The cheapest alternative

Montar en el PC la carpeta compartida del servidor como unidad de red y apuntar `--images` a ella. Resolvería
parte del problema en el PC, sin código nuevo de red, si el servidor ofrece ese tipo de carpeta compartida. No
basta porque:

- la fuente local lista una sola carpeta, sin subcarpetas;
- no ejercita TNFS, que es lo que usará el firmware ESP32;
- las fuentes 2 a 8 seguirían sin hacer nada.

Hacer nada también es una opción. Su consecuencia es la de la sección anterior.

## Goals

- Desde la pantalla principal, una tecla `2`-`8` con un servidor configurado abre ese servidor en solo lectura.
- El usuario recorre las carpetas del servidor y elige una `.ATR` con el teclado del Atari.
- Esa imagen se asigna a D1 (o se monta en D2-D8) igual que una imagen de la fuente local, y `A` la arranca.
- Si el servidor no responde, el Atari lo dice debajo del pie de teclas y nada asignado se pierde.
- La fuente local `1` se comporta exactamente como en #40.

## Non-goals / out of scope

- `E` (editar servidores): sigue mostrando `PENDIENTE: E NECESITA ENTRADA DE TEXTO`.
- Escribir en el servidor: crear, borrar, renombrar o modificar archivos, carpetas o imágenes.
- Paginar carpetas con más de 20 entradas.
- TNFS sobre TCP o WSS; usuario y contraseña en el servidor.
- El firmware ESP32 y cualquier prueba física, flasheo o GPIO.
- Cassette y escritura en disco.
- Cosas que nadie pidió y que no se agregan «por si acaso»:
  - búsqueda o filtro por nombre;
  - favoritos o historial de imágenes;
  - una caché de imágenes entre ejecuciones del host;
  - mostrar archivos que no sean carpetas ni `.ATR`;
  - descubrir servidores automáticamente en la red;
  - cambiar la pantalla principal o la de la fuente local.

## Users & context

El responsable del proyecto, en su laboratorio. Usa Altirra con el hub NetSIO y `retrobridge_host` en el PC.
Tiene un servidor TNFS de laboratorio, `pulgarcito`, accesible desde el PC por su red privada (Tailscale). Quiere
elegir y arrancar imágenes de ese servidor sin tocar el PC.

### Hechos comprobados en `pulgarcito` (2026-11-18, solo lectura)

Se comprobaron con una sonda de laboratorio propia, escrita desde el documento público del protocolo. La sonda no
forma parte del producto. Solo envió MOUNT, OPENDIR, READDIR, CLOSEDIR, STAT y UMOUNT: ninguna apertura, lectura ni
escritura de archivos.

- **Conexión:** responde por UDP en el puerto 16384, con protocolo 1.3, sin usuario ni contraseña. Pide un tiempo
  mínimo entre reintentos de 1000 ms.
- **Nombres:**
  - van en UTF-8 y llevan espacios, paréntesis, comas y «ñ» (por ejemplo `Atari Tarots Español5.atr`);
  - en la raíz hay dos rótulos, `--------- O ---------` y `Welcome to calaluis.cl TNFS server`. Son archivos,
    no carpetas, así que el filtro los oculta.
- **Tamaño de las carpetas, antes y después de filtrar** (filtrar = dejar solo las carpetas y los `.ATR`):

  | Carpeta | Entradas | Carpetas | `.ATR` | Otras (ocultas) | Total filtrado |
  |---|---|---|---|---|---|
  | `/` | 11 | 9 | 0 | 2 | 9 |
  | `/Aportes_Juan_Larrain` | 13 | 10 | 0 | 3 | 10 |
  | `/Aportes_Juan_Larrain/0- Prueba` | 11 | 0 | 3 | 8 | 3 |
  | `/Atari_Chile/ATR` | 129 | 2 | 126 | 1 | 128 |
- **Tipos de archivo:** las carpetas mezclan `.atr` con `.xex`, `.txt` e `.ini`.
- **Tamaño de las imágenes:** hay ATR de 92 176 bytes y una de 16 776 592 bytes (`ARCADE.ATR`).
- **Cabecera de `/Atari_Chile/ATR/ARCADE.ATR`** (leída en solo lectura el 2026-11-18, solo sus 16 primeros
  bytes): `96 02 d8 ff 00 01 0f 00 …`. Marca ATR correcta; 16 776 576 bytes de datos, que coinciden con el
  archivo menos la cabecera; sectores de 256 bytes. Con los tres primeros de 128 bytes da 65 535 sectores exactos.
  Es una ATR válida y el host admite esa geometría (`atr_layout`: sectores de 128 o 256, como máximo 65 535).
- **Tiempos medidos desde el PC del laboratorio** (`clarify`, 2026-11-18, solo lectura):
  - conectar: 0,016 s;
  - leer entera la ATR de referencia: entre 0,36 y 0,47 s, unos 2 ms por cada 512 bytes;
  - listar `/Atari_Chile/ATR` (129 entradas) y consultar cada una: unos 1,0 s en total.
  
  A ese ritmo, `ARCADE.ATR` tardaría unos 65 s. El menú del Atari espera unos 7 s la respuesta de cada tecla.

## Qué podrá hacer el usuario

- **Condición de partida:** el host corre con `--images` y un archivo `--sources` en el que una línea nombra el
  servidor (por ejemplo `pulgarcito`). El Atari muestra la pantalla principal del menú.
- **Abrir la fuente:** pulsar el dígito de esa fuente (`2`-`8`). Se abre la raíz del servidor.
- **Navegar:**
  - pulsar la letra de una carpeta para entrar en ella;
  - pulsar Esc para subir un nivel;
  - en la raíz, Esc vuelve a la pantalla principal.
- **Elegir una imagen:** pulsar la letra de una `.ATR` y luego la unidad `1`-`8`. Esc mientras pide la unidad
  vuelve a la carpeta (como Q3 de #40).
- **Arrancar:** de vuelta en la principal, `A` arranca lo asignado a D1.
- **Quién puede:** cualquiera frente al Atari. El servidor solo se lee. No hay cuentas.

## Qué resultado obtiene

Referencias aprobadas por el responsable:

- la pantalla principal y la lista de la fuente local en el PR #41 (`run4`: `10-error-rota.png`, `03-lista.png`);
- la regla M40-5: los mensajes van debajo del pie de teclas.

Lo que verá:

- **Lista de una carpeta:** misma pantalla que la lista local.
  - Título: la fuente y la carpeta actual.
  - Hasta 20 entradas `A`-`T`: carpetas primero, marcadas con `/`, y después las `.ATR`. Cada grupo va en orden
    alfabético, sin distinguir mayúsculas.
  - El pie de teclas.
  - La línea de mensajes debajo del pie.
- **Imagen asignada:** la principal muestra en D1 el nombre de la imagen elegida y el mensaje
  `D1: <NOMBRE> (A ARRANCA)`, como con la fuente local.
- **Arranque:** el Atari arranca la imagen de referencia del servidor y muestra lo mismo que en el arranque
  directo de referencia (ver *Imagen de referencia de la demo*).
- **Avisos aprobados por el responsable (2026-11-18), siempre debajo del pie de teclas:**
  - «ERROR: SERVIDOR NO RESPONDE»;
  - «ERROR: SERVIDOR NO ENCONTRADO» (nombre que no se puede resolver);
  - «CARPETA VACIA»;
  - «MOSTRANDO 20 DE <total>».
  
  `<total>` se cuenta después de filtrar: solo carpetas y `.ATR`. Por ejemplo, `/Atari_Chile/ATR` muestra
  «MOSTRANDO 20 DE 128», no 129.
- **Carga en curso (decidido en `clarify`):** mientras una imagen llega del servidor, la vista queda donde
  estaba y debajo del pie aparece «CARGANDO nn%». El porcentaje avanza sin que el usuario pulse nada, y el menú
  sigue respondiendo: Esc cancela en cualquier momento.
- **Tamaño de imagen:** el menú no añade un tope de tamaño propio. Siguen valiendo los límites que ya existen: la
  geometría ATR que el host admite (sectores de 128 o de 256 bytes, los tres primeros de 128, como máximo 65 535
  sectores) y el espacio disponible en el host. Una imagen de 16 MB se carga, aunque tarde más de un minuto.

### Imagen de referencia de la demo

Es una imagen real, que ya está en el servidor y arranca. Se registró el 2026-11-18. El responsable la aprobó
como referencia de aceptación el mismo día.

- **Ruta exacta en `pulgarcito`:** `/Aportes_Juan_Larrain/0- Prueba/Atari Logo.atr`.
- **Copia de referencia:** 92 176 bytes, igual al tamaño que informa el servidor. Cabecera ATR válida: 720
  sectores de 128 bytes.
- **SHA-256:** `9f21ef9a3dbf9791e6d94975b2eed9ab3fff7feaa4e05c76c1f14dbbd4c99c96`.
- **Cómo se leyó:** con la sonda de laboratorio, solo con MOUNT, STAT, OPENDIR, READDIR, CLOSEDIR, OPEN en solo
  lectura, READ, CLOSE y UMOUNT. El servidor no recibió ninguna escritura.
- **Arranque directo de referencia:** Altirra 4.31, en una instancia aparte de la del laboratorio.
  - Opciones: `/nosi /tempprofile /cleardevices /nobasic`, con la imagen en el modo por defecto de Altirra, que
    es de solo lectura.
  - Configuración: la de la máquina del responsable (XL, NTSC, 128K), sin NetSIO y sin guardar cambios.
- **Lo que aparece en pantalla:**
  - **A los 10 s:** pantalla azul con el cursor y la unidad 1 activa (sigue cargando).
  - **A los 25 s y a los 45 s:** la palabra «ATARI» en letras grandes, centrada, sobre fondo negro, rellena con
    franjas horizontales de color. Los colores cambian con el tiempo: rosa y azul a los 25 s, verde y amarillo a
    los 45 s.
  - **Criterio de comparación:** la palabra «ATARI» centrada sobre fondo negro. El color exacto no cuenta,
    porque cambia.
- **Evidencia:** carpeta local del laboratorio `D:\rb-e0\m41\ref`, no versionada. Contiene:

  | Archivo | SHA-256 |
  |---|---|
  | `Atari Logo.atr` | `9f21ef9a...` |
  | `ref-10s.png` | `a3f99b44...` |
  | `ref-25s.png` | `e0c4373f...` |
  | `ref-45s.png` | `38b77e27...` |
  | `boot-ref.log` | `eb699b88...` |

- **Para repetir la prueba con la misma imagen:** leer la ruta exacta y comprobar el SHA-256 completo antes de
  arrancar. Si no coincide, la imagen del servidor cambió y la referencia ya no vale.
- **Servidor caído:** debajo del pie aparece un error que nombra al servidor, nunca «EL HOST NO RESPONDE». D1
  conserva lo que tenía asignado.

El texto de los avisos está aprobado por el responsable (2026-11-18). La lista de carpetas no tiene captura de
referencia: se comprueba contra las reglas de esta sección (orden, marca `/`, filtro y avisos).

## Qué debe respetar

- **ADR-012:** el cliente TNFS es propio. Se escribe desde la documentación pública del protocolo, sin copiar,
  traducir ni adaptar código de FujiNet ni de otros emuladores. La fuente del protocolo se registra en
  `docs/fuentes.md`, que hoy no tiene ninguna sobre TNFS.
- **ADR-017, enmienda 1, y las decisiones M40-1 a M40-5:**
  - el host dibuja la pantalla y el Atari es un terminal delgado;
  - D1 queda asignada hasta `A`;
  - D2 a D8 se montan por el buzón (ADR-014);
  - la línea de mensajes va debajo del pie;
  - una tecla repetida por `SIOV` se aplica una sola vez.
- **Programa 6502 del menú (decidido en `clarify` y al revisar el plan):** cambia solo en dos cosas:
  - vuelve a preguntar al host mientras dure una carga, sin que el usuario pulse nada;
  - quita los bits de Shift y Control de cada tecla antes de enviarla (`AND #$3F`), para que el sondeo nunca se
    confunda con una tecla (M41-3). El significado de las teclas no cambia, porque el host ya los ignoraba.
  
  Sigue siendo un terminal delgado: no decide qué mostrar ni guarda estado del menú. Una tecla repetida por `SIOV`
  se sigue aplicando una sola vez (M40-2).
- **ADR-016:** `rb_sio` y `rb_netsio` no cambian.
- **ADR-002 y ADR-014:** TNFS es un adaptador nuevo detrás de los puertos de imagen. Abrir una imagen lenta no
  frena la atención de las unidades.
- **Fuente local `1`:** su lista, su selección, su montaje y su arranque no cambian (#40).
- **`--sources`:** como máximo 7 nombres de 1 a 32 caracteres. Se rechaza sin `--images` y si `--log` nombra
  ese archivo.
- **Solo lectura:** el servidor de laboratorio no recibe ninguna escritura, ni en la demo ni en las pruebas.
- **Reglas del proyecto:**
  - commit, push y PR solo con aprobación;
  - main se cambia solo con rama y PR;
  - sin pruebas físicas, flasheo ni GPIO sin aprobación;
  - sin emoji en PR, commits ni documentos;
  - `diag_h3b.txt` nunca se versiona.

## Cómo lo demuestra

- **Pruebas automáticas en la CI de host:** un servidor TNFS de prueba propio, dentro del arnés de pruebas y sin
  código de terceros, con carpetas, ATR válidas, una inválida y fallos simulados. Cubren:
  - abrir, entrar, subir y elegir;
  - más de 20 entradas;
  - carpeta vacía;
  - servidor que no responde o que responde tarde;
  - carga larga de la ATR de referencia con el servidor ralentizado a más de 7 s, que termina montada en D2;
  - Esc durante la carga y respuesta tardía después de cancelar;
  - rechazo de apertura («ERROR: NO SE PUDO ABRIR»);
  - imagen inválida, en una prueba aparte;
  - conservación de lo asignado en D1;
  - y que la fuente `1` sigue igual.
- **Demo en Altirra contra `pulgarcito`, solo lectura:** el recorrido del responsable, de principio a fin.
  - Deja capturas de cada pantalla, el registro del recorrido (teclas y hora) y el registro del host con los
    eventos de cada paso.
  - Las capturas y el registro del recorrido se adjuntan al PR.
  - Incluye la carga larga de `ARCADE.ATR` en D2, con capturas de «CARGANDO nn%» y del montaje final.
- **Caso de servidor caído en Altirra:** una fuente que nombra un destino sin servidor TNFS. Deja la captura del
  error debajo del pie y la de D1 conservada.
- **Fuente local:** el mismo recorrido de `run4` (abrir `1`, elegir, montar en D2, asignar D1, arrancar con `A`,
  Shift+F5 de vuelta), con capturas.
- **Evidencia que queda:** salida de las suites, capturas y registros de la demo, y el resultado de la CI del
  commit publicado.

## Behaviour

- **Camino principal:**
  1. `2`-`8` con servidor configurado muestra la raíz.
  2. La letra de una carpeta muestra su contenido; Esc sube un nivel.
  3. La letra de una `.ATR` pide la unidad.
  4. `1` asigna a D1 y vuelve a la principal con el mensaje de asignación.
  5. `A` arranca.
- **Casos borde:**
  - **Carpeta vacía:** la lista muestra «CARPETA VACIA» y Esc sube.
  - **Más de 20 entradas después de filtrar:** se muestran las 20 primeras según el orden de *Qué resultado
    obtiene* y «MOSTRANDO 20 DE <total>» debajo del pie.
  - **Nombres largos o con caracteres no imprimibles:** se recortan o sustituyen al mostrarlos. La entrada se
    sigue pudiendo elegir.
  - **La misma imagen en dos unidades:** se permite, igual que en la fuente local.
  - **Una fuente sin servidor configurado:** sigue mostrando `FUENTE n SIN CONFIGURAR`.
  - **Una tecla repetida por `SIOV`:** se aplica una vez.
  - **Carga larga (imagen o carpeta):** si la respuesta no está lista enseguida, debajo del pie aparece
    «CARGANDO nn%» y el Atari vuelve a preguntar solo hasta que termina. Al terminar aparece el resultado normal:
    la asignación, el montaje, la carpeta o un error que conserva lo anterior.
  - **Esc durante una carga:** la cancela; la vista y lo asignado o montado quedan como antes. Si después llega
    una respuesta tardía del servidor para esa carga, se descarta: no cambia ninguna unidad.
  - **Otras teclas durante una carga:** se ignoran.
- **Errores:**
  - **El servidor no responde al abrir, al navegar o al elegir:**
    - error debajo del pie;
    - la vista queda donde estaba (la principal o la carpeta anterior);
    - nada asignado ni montado cambia;
    - el Atari nunca llega a mostrar «EL HOST NO RESPONDE» por culpa de un servidor lento.
  - **Imagen inválida:** «ERROR: ATR INVALIDA» y nada cambia, como en la fuente local.
  - **Nombre de servidor que no se puede resolver:** «ERROR: SERVIDOR NO ENCONTRADO» debajo del pie.
  - **El servidor cae después de asignar D1:** el arranque con `A` y el uso de la imagen siguen funcionando (ver
    la suposición en *Points to clarify*).
  - **El servidor cae a mitad de una carga:** «ERROR: SERVIDOR NO RESPONDE» debajo del pie; nada asignado ni
    montado cambia.
  - **El servidor rechaza abrir una carpeta o una imagen:** «ERROR: NO SE PUDO ABRIR» debajo del pie; la vista
    queda donde estaba.
  - **Respuesta mal formada del servidor:** se trata como servidor que no responde; el host no se cae.
  - **No se revela:** nada del PC (rutas locales, direcciones internas) en la pantalla del Atari.

## Acceptance criteria

- Given un `--sources` con `pulgarcito` en la fuente 2, When el usuario pulsa `2`, Then el Atari muestra la raíz
  del servidor: carpetas marcadas con `/` primero, después las `.ATR`, y el mensaje debajo del pie.
- Given una carpeta abierta, When el usuario pulsa la letra de una subcarpeta, Then ve el contenido de esa
  subcarpeta; When pulsa Esc, Then vuelve a la carpeta anterior; When pulsa Esc en la raíz, Then vuelve a la
  pantalla principal.
- Given el usuario en `/Aportes_Juan_Larrain/0- Prueba`, When pulsa la letra de `Atari Logo.atr` y luego `1`,
  Then la principal muestra `ATARI LOGO.ATR` en D1 y `D1: ATARI LOGO.ATR (A ARRANCA)` debajo del pie.
- Given esa imagen asignada a D1, When el usuario pulsa `A`, Then el Atari arranca y muestra la palabra «ATARI»
  centrada sobre fondo negro, como en el arranque directo de referencia; y el registro del host muestra que la
  imagen servida tiene el SHA-256 `9f21ef9a3dbf9791e6d94975b2eed9ab3fff7feaa4e05c76c1f14dbbd4c99c96`.
- Given una imagen ya asignada a D1, When el usuario abre una fuente cuyo servidor no responde, Then aparece
  «ERROR: SERVIDOR NO RESPONDE» debajo del pie, D1 sigue mostrando la imagen anterior y `A` la sigue arrancando.
- Given un servidor que no responde, When el usuario pulsa su dígito, Then el Atari muestra «ERROR: SERVIDOR NO
  RESPONDE» y no la pantalla «EL HOST NO RESPONDE».
- Given una fuente con un nombre que no se puede resolver, When el usuario pulsa su dígito, Then aparece «ERROR:
  SERVIDOR NO ENCONTRADO» debajo del pie.
- Given una ATR inválida en el servidor de prueba, When el usuario la elige para D3, Then aparece «ERROR: ATR
  INVALIDA» y D3 no cambia. Es una prueba aparte de la carga larga.
- Given `/Atari_Chile/ATR` en `pulgarcito` (129 entradas, 128 después de filtrar), When se abre, Then muestra 20
  entradas y «MOSTRANDO 20 DE 128» debajo del pie.
- Given una carpeta sin carpetas ni `.ATR`, When se abre, Then muestra «CARPETA VACIA» debajo del pie.
- Given la fuente local `1`, When se repite el recorrido de `run4`, Then cada paso da la misma pantalla y el
  mismo evento del host que en #40.
- Given la demo completa contra `pulgarcito`, When termina, Then el servidor no recibió ninguna escritura.
  Evidencia: el registro del host, que lista cada operación pedida, y ninguna es de escritura.
- Given la tecla `E`, When se pulsa, Then sigue mostrando `PENDIENTE: E NECESITA ENTRADA DE TEXTO`.
- Given `/Atari_Chile/ATR/ARCADE.ATR` en `pulgarcito` (16 776 592 bytes, ATR válida de 65 535 sectores de 256
  bytes), When el usuario la elige para D2, Then debajo del pie aparece «CARGANDO nn%» con un porcentaje que
  sube; el menú sigue respondiendo y el Atari nunca muestra «EL HOST NO RESPONDE»; y al terminar la principal
  muestra `ARCADE.ATR` montada en D2. Evidencia: capturas de al menos dos porcentajes distintos y de la pantalla
  final, y el registro del host con el montaje en D2.
- Given la ATR de referencia en el servidor de prueba, ralentizado para que la transferencia dure más de 7 s,
  When el usuario la elige para D2, Then aparece «CARGANDO nn%» y al terminar D2 muestra `ATARI LOGO.ATR`
  montada. Es la versión automática del criterio anterior, sin depender de `pulgarcito`.
- Given una carga en curso en el servidor de prueba, When el servidor deja de responder, Then aparece «ERROR:
  SERVIDOR NO RESPONDE» debajo del pie y D1 y D2 a D8 siguen como estaban.
- Given una carga en curso, When el usuario pulsa Esc, Then la carga se cancela y nada asignado ni montado cambia.
- Given D2 con una imagen montada y una carga para D2 cancelada con Esc, When el servidor de prueba envía tarde la
  respuesta de esa carga, Then D2 conserva la imagen anterior y no aparece ningún mensaje de montaje.
- Given el servidor de prueba que tarda más de 7 s en listar una carpeta, When el usuario la abre, Then aparece
  «CARGANDO nn%» y después la carpeta, sin «EL HOST NO RESPONDE».
- Given el servidor de prueba que rechaza abrir una carpeta, When el usuario la elige, Then aparece «ERROR: NO SE
  PUDO ABRIR» debajo del pie y la vista no cambia.
- Given el servidor de prueba que envía respuestas mal formadas, When el usuario navega, Then aparece «ERROR:
  SERVIDOR NO RESPONDE» y el host sigue atendiendo las unidades.

## Points to clarify

- **suposición tomada** — El arranque directo de referencia usó la configuración de Altirra del responsable (XL,
  NTSC, 128K) sin NetSIO; la demo de aceptación usa la misma máquina con NetSIO. *Base:* el título de la ventana de
  ambas instancias («XL NTSC / 128K»). *Riesgo:* si el SO o la ROM cambian entre las dos, la pantalla podría
  diferir; el criterio compara la palabra «ATARI» y no el color.
- **suposición tomada** — La línea de `--sources` de la demo es `pulgarcito`. *Base:* el responsable pidió el
  servidor TNFS de `pulgarcito` (2026-11-18). `tnfs.calaluis.cl` resuelve a una dirección pública y su raíz rotula
  «calaluis.cl TNFS server»; puede ser la misma máquina, pero la demo no lo usa. *Riesgo:* ninguno para la demo.
- **suposición tomada** — Los nombres del servidor se tratan como UTF-8. Lo que el Atari no puede mostrar se
  sustituye al mostrarlo, pero la entrada se sigue pudiendo elegir. *Base:* lo comprobado en `pulgarcito`.
  *Riesgo:* un servidor con otra codificación mostraría nombres raros, sin dejar de funcionar.
- **suposición tomada** — Las entradas que no son carpetas ni `.ATR` (por ejemplo los dos rótulos de la raíz,
  `.xex` o `.txt`) no se muestran. *Base:* la regla de solo carpetas y `.ATR`. En `pulgarcito`, los rótulos de la
  raíz son archivos. *Riesgo:* quien busque un `.xex` no lo verá hasta otra etapa.
- **suposición tomada** — TNFS por UDP en el puerto 16384, sin usuario ni contraseña. *Base:* respuesta del
  responsable del 2026-11-18. *Riesgo:* si `pulgarcito` exige usuario, hace falta un campo más en `--sources` o
  esperar a `E`.
- **suposición tomada** — Cada línea de `--sources` es solo el nombre del servidor; el puerto es 16384 y se
  empieza en la raíz. *Base:* el formato vigente de #40 (nombres de 1 a 32 caracteres). *Riesgo:* un servidor en
  otro puerto o una carpeta inicial distinta obligan a ampliar el formato.
- **suposición tomada** — Una imagen asignada o montada desde TNFS queda disponible entera en el host. Arrancar
  y leer no dependen de que el servidor siga respondiendo. *Base:* el pedido de conservar lo asignado si el
  servidor falla, y que el arranque no se cuelgue a mitad. El tiempo de traerla se cubre con «CARGANDO nn%»
  (*Clarifications*, 2026-11-18). *Riesgo:* imágenes muy grandes ocupan memoria del PC (16 MB la mayor medida).
  En el firmware ESP32 esta suposición no sirve y tendrá que revisarse.
- **suposición tomada** — Durante una carga, las teclas distintas de Esc se ignoran. *Base:* Esc ya significa
  «volver sin cambiar» en el menú; que Esc cancele lo decidió el responsable. *Riesgo:* quien pulse otra tecla
  esperando cancelar verá que no pasa nada hasta pulsar Esc.
- **suposición tomada** — Un listado de carpeta lento usa la misma pantalla «CARGANDO nn%» que una imagen.
  *Base:* listar 129 entradas tardó cerca de 1 s en el laboratorio; por un enlace lento puede pasar de 7 s.
  *Riesgo:* en un listado el porcentaje es una estimación, porque el total no se conoce hasta el final.
- **suposición tomada** — Si el servidor se reinicia y olvida la sesión, el host vuelve a conectar una vez sin
  avisar; si falla, muestra «ERROR: SERVIDOR NO RESPONDE». *Base:* el protocolo exige montar antes de cualquier
  orden. *Riesgo:* ninguno visible para el usuario.
- **suposición tomada** — Una respuesta mal formada nunca cae el host: se trata como servidor que no responde.
  *Base:* ADR-002 (las unidades siguen atendidas). *Riesgo:* un servidor defectuoso parece caído.
- **suposición tomada** — Al volver a abrir una fuente se empieza otra vez en su raíz; no se recuerda la última
  carpeta. *Base:* «se empieza en la raíz» del formato de `--sources`. *Riesgo:* volver a una carpeta profunda
  cuesta varias teclas.
- **suposición tomada** — Una ruta larga en el título se recorta por la izquierda, para que se vea la carpeta
  actual. *Base:* el ancho de 40 columnas. *Riesgo:* ninguno funcional.
- **suposición tomada** — Las entradas que empiezan por punto no se muestran. *Base:* son ocultas por
  convención y no son carpetas de juegos. *Riesgo:* una carpeta con punto no se podrá abrir.
- **suposición tomada** — Varias fuentes TNFS pueden estar en uso a la vez (por ejemplo D1 desde la fuente 2 y
  D3 desde la 3). *Base:* D1 a D8 funcionan como en la fuente local. *Riesgo:* ninguno, porque cada imagen se
  guarda entera en el host.
- **suposición tomada** — Más de 20 entradas: se muestran las 20 primeras y un aviso con el total, sin paginar.
  *Base:* respuesta del responsable del 2026-11-18. *Riesgo, ya medido:* en `pulgarcito`, `/Atari_Chile/ATR`
  tiene 128 entradas después de filtrar (2 carpetas y 126 `.ATR`), así que 108 no se podrán elegir hasta que
  haya paginación.
- **decisión diferida** — Paginación, TNFS por TCP o WSS, escritura en el servidor, `E`, cliente TNFS en el
  firmware ESP32.
- **área no formulable** — El comportamiento con enlaces muy lentos o con pérdidas (Tailscale por relé). Se sabe
  que va a importar al medir la demo, pero todavía no se puede fijar un umbral con sentido.

## Clarifications

- 2026-11-18 — **Pregunta:** el Atari espera unos 7 s la respuesta de cada tecla; a 2 ms por bloque de 512 bytes,
  una imagen de más de 1,5 MB, o una pequeña por un enlace lento, no llega a tiempo. ¿Qué hace el menú?
  **Decisión:** pantalla «CARGANDO nn%». El host responde enseguida con el progreso y el Atari vuelve a preguntar
  solo hasta que termina. Sin límite de tamaño. Cambia el programa 6502 del menú. **Por qué:** el responsable
  prefiere poder cargar cualquier imagen del servidor (hasta 16 MB) a un límite de tamaño o a un aviso de imagen
  demasiado grande, aunque la etapa se alargue.
- 2026-11-18 — **Resultado de cada punto recibido de `specify`:**
  - entorno de arranque de referencia: validada, se mantiene (mismo título de ventana en las dos instancias);
  - `--sources` con `pulgarcito`: validada, se mantiene;
  - nombres UTF-8: validada, se mantiene (nombres con «ñ», espacios y paréntesis leídos bien);
  - solo carpetas y `.ATR`: validada, se mantiene (los rótulos de la raíz son archivos);
  - UDP 16384 sin usuario: validada, se mantiene (montaje anónimo aceptado, protocolo 1.3);
  - línea de `--sources` solo con el nombre: validada, se mantiene;
  - imagen entera en el host: validada y ampliada; el tiempo de traerla lo cubre «CARGANDO nn%»;
  - 20 entradas sin paginar: validada, se mantiene (decisión del responsable; 108 imágenes quedan fuera);
  - decisión diferida (paginación, TCP o WSS, escritura, `E`, ESP32): se deja diferida;
  - área no formulable (enlaces lentos): se mantiene sin umbral, pero en parte gradúa: un enlace lento ya no
    produce «EL HOST NO RESPONDE», porque cae en «CARGANDO nn%».
- 2026-11-18 — Suposiciones nuevas del recorrido de huecos, sin pregunta porque no cambian lo que se construye o
  tienen una opción razonable: Esc cancela la carga; listados lentos con «CARGANDO»; «ERROR: NO SE PUDO ABRIR»;
  reconexión silenciosa; respuestas mal formadas; raíz al reabrir; título recortado; entradas con punto; varias
  fuentes TNFS a la vez. Están en *Points to clarify* para que el responsable las vete si quiere.
- 2026-11-18 — **Revisión del responsable:**
  - Esc cancela una carga: decidido, deja de ser suposición.
  - «ERROR: NO SE PUDO ABRIR» cuando el servidor rechaza la apertura: texto aprobado.
  - Una carga cancelada conserva el disco anterior aunque llegue una respuesta tardía: nuevo criterio.
  - El menú no añade un tope de tamaño propio: valen la geometría ATR admitida y el espacio del host.
  - La suposición sobre `ARCADE.ATR` estaba mal: la cuenta del tamaño era errónea. Se leyó su cabecera en solo
    lectura y es válida (65 535 sectores de 256 bytes). La carga larga se demuestra con ella en `pulgarcito` y
    debe terminar con D2 montada. Las pruebas automáticas usan la ATR de referencia con el servidor de prueba
    ralentizado a más de 7 s. El rechazo de una ATR inválida se prueba aparte.

## Revisions

- 2026-11-18: borrador inicial.
  - Respuestas del responsable: UDP 16384; letra entra y Esc sube; 20 entradas sin paginar; D1 a D8 como la
    fuente local; modo por fases.
  - Falta la ruta de la ATR de referencia.
  - El responsable indicó `tnfs.calaluis.cl`. Queda como suposición hasta confirmar si es la dirección pública
    de `pulgarcito`.
- 2026-11-18: el responsable pide que la demo use una ATR real y arrancable de `pulgarcito`, con su ruta exacta
  y su pantalla de arranque. Las imágenes generadas quedan para las pruebas automáticas.
  - La demo usa `pulgarcito`; `tnfs.calaluis.cl` queda fuera.
  - Se añaden los hechos comprobados del servidor y una ATR candidata, pendiente de comprobar su arranque.
- 2026-11-18: lectura y arranque de la referencia, aprobados por el responsable. La spec sigue en borrador hasta
  que revise el resultado.
  - `Atari Logo.atr` se leyó en solo lectura y arranca: muestra «ATARI» centrada sobre fondo negro.
  - Se registraron su SHA-256 y las capturas.
  - Avisos aprobados: «ERROR: SERVIDOR NO RESPONDE», «ERROR: SERVIDOR NO ENCONTRADO», «CARPETA VACIA» y «MOSTRANDO
    20 DE <total>», con `<total>` contado después de filtrar.
  - Corrección: los rótulos de la raíz son archivos y no carpetas. `/Atari_Chile/ATR` da 128 entradas después de
    filtrar, no 129.
- 2026-11-18: el responsable aprobó `Atari Logo.atr` como referencia de aceptación («Approve reference»). Eso
  aprueba la imagen, no la spec, que sigue en borrador hasta que la apruebe entera.
- 2026-11-18: el responsable aprobó la spec completa («Approve the spec»). Las suposiciones tomadas quedan para
  que `clarify` las valide antes del plan.
- 2026-11-18: `clarify`. Decisión del responsable: pantalla «CARGANDO nn%» para cargas largas. Se añaden los
  tiempos medidos, el comportamiento de carga, sus criterios de aceptación, la restricción del programa 6502 y
  nueve suposiciones nuevas. Pendiente de que el responsable apruebe la spec aclarada.
- 2026-11-18: cambios pedidos por el responsable antes de aprobar: cabecera de `ARCADE.ATR` validada y criterio
  de carga larga con montaje en D2; versión automática con servidor ralentizado; Esc y «ERROR: NO SE PUDO ABRIR»
  decididos; respuesta tardía tras cancelar; sin tope de tamaño propio. Pendiente de aprobación.
- 2026-11-18: el responsable aprobó la spec aclarada («Aprobar spec aclarada»). Queda lista para `plan`.
- 2026-11-18: `analyze` (hallazgo F1): *Qué debe respetar* nombra el segundo cambio del programa 6502, la normalización de teclas (M41-3), que el responsable pidió al revisar el plan. Aplicado con su aprobación («Corregir los 4 medios»).
