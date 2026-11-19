---
type: plan
title: Plan — Fuentes TNFS en el menú del Atari (solo lectura)
description: Cómo se construye el acceso TNFS de solo lectura desde las fuentes 2 a 8 del menú, con carga en segundo plano y pantalla CARGANDO.
tags: [sdd, plan, host, netsio, menu, tnfs]
timestamp: 2026-11-18T05:30:00Z
topic: sdd
slug: menu-tnfs
status: approved
---

# Plan — Fuentes TNFS en el menú del Atari (solo lectura)

> Spec: [../specs/menu-tnfs.md](../specs/menu-tnfs.md) (aclarada y aprobada el 2026-11-18) · Constitución:
> [../constitution.md](../constitution.md) · Estado: aprobado por el responsable el 2026-11-18 · Base: `origin/main` en `170a411`.
> Los criterios de aceptación se citan como AC1 a AC21, en el orden en que aparecen en la spec.

## 0. Restricciones globales

Toda tarea las cumple, aunque no las repita. El revisor las lee primero.

- **Procedencia (ADR-012):** cliente TNFS, servidor de prueba y SHA-256 son código propio. Se escriben desde
  documentos públicos: el protocolo TNFS de spectranet (`tnfs/tnfs-protocol.md`, actualizado el 2020-07-15) y
  FIPS 180-4 para SHA-256. No se copia, traduce ni adapta código de FujiNet, de emuladores ni de otras
  implementaciones TNFS. Cada fuente se registra en `docs/fuentes.md`.
- **Solo lectura:** el código TNFS solo sabe codificar estas órdenes: MOUNT 0x00, UMOUNT 0x01, OPENDIR 0x10,
  READDIR 0x11, CLOSEDIR 0x12, READ 0x21, CLOSE 0x23, STAT 0x24 y OPEN 0x29, y nada más. No existe ninguna
  función que codifique otra orden.
- **Apertura de solo lectura:** el códec y el cliente exponen solo una apertura de lectura (`openReadOnly`), con
  los indicadores fijos en `O_RDONLY` = `0x0001` y el modo en 0. No hay ningún parámetro de indicadores, ni
  público ni interno. Las pruebas comprueban los indicadores de cada OPEN que recibe el servidor, además de la
  lista de órdenes.
- **Códigos de tecla (AUX1 de `'K'`):** el 6502 normaliza cada tecla antes de enviarla: `CH AND $3F`, sin los
  bits de Shift ($40) ni de Control ($80). Las teclas reales van de $00 a $3F. $FE es el sondeo y $FF el
  redibujo; los dos quedan fuera de ese rango y el host los compara antes de enmascarar. Los códigos de $40 a
  $FD no los envía el programa; si llegan, el host los enmascara con $3F, como en #40.
- **Falta de memoria:** en pantalla, `ERROR: NO SE PUDO ABRIR`. El registro del host anota la causa concreta
  (memoria pedida y fallo de la reserva). Lo asignado y lo montado no cambian.
- **Red:** UDP, puerto 16384, sin usuario ni contraseña. Una línea de `--sources` es solo el nombre del
  servidor; se empieza en `/`.
- **Esperas TNFS:** primer contacto con 3 intentos de 2000 ms, así que un servidor mudo da `ERROR: SERVIDOR NO
  RESPONDE` en 6 s como máximo. Después del MOUNT, 3 intentos por orden con la espera mínima que anunció el
  servidor (5 s como máximo, M41-9), o 2000 ms si no anunció ninguna.
- **Espera del Atari:** `MENU_TIMEOUT_S = 7` (DTIMLO). El host contesta cada `'K'` en 250 ms como máximo. Si el
  trabajo no terminó, contesta con la acción de espera y «CARGANDO nn%».
- **Textos debajo del pie, exactos:** `ERROR: SERVIDOR NO RESPONDE`, `ERROR: SERVIDOR NO ENCONTRADO`,
  `ERROR: NO SE PUDO ABRIR`, `ERROR: ATR INVALIDA`, `CARPETA VACIA`, `MOSTRANDO 20 DE <total>` y
  `CARGANDO nn%`. Nunca `EL HOST NO RESPONDE` por culpa de un servidor lento.
- **Lista:** solo carpetas y `.ATR` (cualquier caso); sin entradas que empiecen por punto; carpetas primero,
  marcadas con `/`; orden alfabético sin distinguir mayúsculas; como máximo 20; `<total>` se cuenta después de
  filtrar.
- **Sin cambios:** `rb_sio`, `rb_netsio`, `rb_domain` y `rb_application` no cambian (ADR-016; arquitectura
  §5.3, punto 6). La fuente local `1` no cambia de comportamiento (#40).
- **Memoria (ADR-011):** `MenuController::key()` y el paso de `SioSession` no reservan memoria. Reservar
  memoria solo pasa en el hilo de carga.
- **Tamaño:** el menú no pone un tope propio. Valen la geometría ATR que el host admite (`atr_layout`: sectores
  de 128 o 256, los tres primeros de 128, como máximo 65 535) y la memoria disponible.
- **Reglas del proyecto:** main cerrado (rama y PR); commit, push y PR solo con aprobación; sin emoji en
  commits, PR ni documentos; `diag_h3b.txt` nunca se versiona; sin pruebas físicas, flasheo ni GPIO.
- **Pie de los commits:** `Co-Authored-By: Claude Opus 5.5 <noreply@anthropic.com>`. El PR termina con
  «Generated with [Claude Code](https://claude.com/claude-code)», sin emoji.

## 1. Contexto y restricciones

- **Criterios que dan forma al diseño:** AC14, AC15, AC19 (cargas de más de 7 s), AC17 y AC18 (cancelar y
  descartar respuestas tardías), AC5 y AC16 (el error conserva lo que había), AC12 (ninguna escritura).
- **El dato que decide el diseño:** el Atari espera unos 7 s por tecla. Una ATR de 16 MB tarda unos 65 s a
  2 ms por bloque de 512 bytes. Por eso ninguna operación de red puede ir dentro de la respuesta a una tecla.
- **Lo que ya existe y se reutiliza (#40):** terminal delgado `'K'` con AUX1 = tecla y AUX2 = número de
  secuencia; respuesta de 962 bytes (acción, diseño y pantalla); vistas Main, List, Pick y Unmount en
  `MenuController`; D1 asignada hasta `A`; D2 a D8 por el buzón; `HostImageRepository` con el disco del menú y
  los archivos de la carpeta.
- **Límite de `ImageLocation`:** de 1 a 127 caracteres ASCII imprimibles. Las rutas de `pulgarcito` son UTF-8
  (con «ñ», espacios y paréntesis) y pueden ser más largas. Una ruta TNFS no cabe en una `ImageLocation`.
- **Fuera de alcance (spec, Non-goals):** `E`, escritura, paginación, TCP o WSS, autenticación, cliente en
  ESP32, cassette, búsqueda, favoritos, caché entre sesiones y archivos que no son ATR.

## 2. Arquitectura

```text
 Atari (programa 6502)                       PC: retrobridge_host
 ---------------------                       --------------------------------------------------------------
 tecla o sondeo ---$5A 'K'---> MenuSioTransport --> MenuController (hilo SIO, sin red, sin reservar memoria)
 <-- acción 0/1/2 + pantalla --                     |   pide trabajo / lee estado / cancela
                                                    v
                                              TnfsLoader (hilo de carga, un trabajo a la vez)
                                                    |  usa
                                                    v
                                              rb_net_tnfs: TnfsClient --> TnfsCodec
                                                    |  sobre DatagramPort (puerto de rb_netsio, sin cambios)
                                                    v
                                              UdpTnfsPort (resuelve el nombre, UDP 16384) ---> servidor TNFS
                                                    |
                         carga terminada: bytes --> MemoryImageStore (9 huecos en memoria)
                                                    ^
 HostImageRepository: "*menu*" -> disco del menú; "*tnfs*<n>" -> hueco n; otro nombre -> archivo de --images
```

- **Programa 6502** (interno, `tools/make_menu.py`): igual que en #40 más dos cambios. Normaliza cada tecla con
  `AND #$3F` antes de enviarla, y añade la acción «esperar»: espera unos 0,25 s y vuelve a preguntar con el
  código de sondeo $FE. Si en ese tiempo se pulsa una tecla, envía esa tecla, normalizada, en lugar del sondeo.
- **MenuSioTransport** (interno, sin cambios de contrato): atiende `'K'`. El sondeo lleva AUX2 nuevo como
  cualquier tecla, así que M40-2 (una repetición de `SIOV` no se aplica dos veces) sigue igual.
- **MenuController** (interno): decide qué significa cada tecla y dibuja. Nuevas vistas: carpeta TNFS, elegir
  unidad para una ATR TNFS y carga. Nunca toca la red: pide trabajos a `TnfsLoader` y lee su estado.
- **TnfsLoader** (interno, nuevo): un hilo y una ranura de trabajo protegida. Trabajos: listar una carpeta o
  traer una imagen. Publica progreso y resultado con un número de trabajo. Si el trabajo fue cancelado o
  reemplazado, descarta su resultado y libera la memoria.
- **rb_net_tnfs** (interno, componente nuevo y portable, C++17 sin reservas en el códec): `TnfsCodec` codifica
  las órdenes de solo lectura y analiza las respuestas sin confiar en ellas. `TnfsClient` lleva una sesión:
  número de conexión, número de secuencia, reintentos, descarte de datagramas que no corresponden y una
  reconexión silenciosa.
- **UdpTnfsPort** (interno, host): `DatagramPort` hacia un nombre de servidor. Si el nombre no se resuelve,
  devuelve NotFound, y eso se muestra como «ERROR: SERVIDOR NO ENCONTRADO».
- **MemoryImageStore** (interno, host): 9 huecos (8 unidades más uno en validación) que guardan imágenes
  enteras traídas por TNFS. Cada hueco expone un `ImageByteSource` de memoria.
- **HostImageRepository** (interno, se amplía): elige el origen por la ubicación. Cumple el papel del
  `RoutingImageRepository` de la arquitectura §5.3 en la raíz de composición, sin tocar `rb_application`.
- **Servidor TNFS** (externo): `pulgarcito` en la demo. En las pruebas, `FakeTnfsServer`, propio y
  programable.

**Decisión principal: la red vive en un hilo de carga, y la imagen TNFS se trae entera a memoria antes de
validarla y asignarla o montarla.** Hay dos motivos, los dos de §1:

- ninguna respuesta a una tecla puede esperar más de 7 s, y una imagen puede tardar 65 s;
- una ruta TNFS no cabe en una `ImageLocation`, así que la imagen no se abre «por nombre» desde el repositorio:
  se abre por un hueco de memoria con una ubicación ASCII reservada (`*tnfs*0` a `*tnfs*8`).

Así el SIO de las unidades nunca espera a la red, `rb_application` y `rb_sio` no cambian, y cumplir lo pedido
(conservar lo asignado si el servidor cae, arrancar sin depender del servidor) sale gratis.

Alternativa descartada: un `TnfsByteSource` que lea sectores por la red bajo demanda (arquitectura §5.3, punto
4). Ahorra memoria, pero el arranque dependería del servidor y de la latencia de Tailscale en cada sector, y la
spec pide lo contrario (suposición «imagen entera en el host»). Se deja para el firmware ESP32, que no tiene
memoria para 16 MB.

## 3. Interfaces y contratos

```text
TnfsCodec (rb_net_tnfs, sin estado, sin reservas)
  encode<Orden>(conexión, secuencia, argumentos, salida: Span) -> longitud | BufferTooSmall
    - solo existen las 9 órdenes de §0
    - encodeOpenReadOnly(conexión, secuencia, ruta, salida): escribe indicadores 0x0001 y modo 0; no recibe
      indicadores ni modo
  parse(datagrama: Span, orden esperada, secuencia esperada) -> Respuesta | Ajena | MalFormada
    - Ajena: otra secuencia u otra orden (se descarta, no es error)
    - MalFormada: longitud corta, campo fuera de rango, nombre sin terminador; nunca lee fuera del datagrama

TnfsClient (rb_net_tnfs; sobre un DatagramPort; lo usa un solo hilo)
  mount() -> Versión | SinRespuesta | Rechazado(código) | MalFormada
  listDir(ruta, visita(nombre)) -> Ok | SinRespuesta | NoSePudoAbrir | MalFormada | Cancelado
  stat(ruta) -> {esCarpeta, tamaño} | NoSePudoAbrir | SinRespuesta | MalFormada
  readWholeReadOnly(ruta, destino(bytes), progreso(leídos, total), cancelado())
      -> Ok | SinRespuesta | NoSePudoAbrir | MalFormada | Cancelado
    - abre siempre con encodeOpenReadOnly; no hay otra forma de abrir un archivo
  umount()
    - primer contacto (MOUNT de una sesión nueva): 3 intentos de 2000 ms cada uno; sin respuesta, SinRespuesta
      a los 6 s como máximo
    - después del MOUNT: hasta 3 intentos por orden; espera por intento = el mínimo que anunció el servidor
      (1000 ms en pulgarcito; 5 s como máximo, M41-9), o 2000 ms si no anunció ninguno; EAGAIN espera lo que
      pide, hasta una espera de intento, y no gasta intento
    - si el servidor contesta que la sesión no existe (se reinició): un MOUNT nuevo y un solo reintento de la
      orden; si vuelve a fallar, SinRespuesta
    - OPEN y OPENDIR siempre se cierran con CLOSE o CLOSEDIR, también al cancelar o al fallar a mitad

TnfsLoader (host; dueño de un TnfsClient por fuente)
  start(trabajo: Listar(fuente, ruta) | Traer(fuente, ruta, tamaño esperado)) -> número de trabajo
  poll(número) -> EnCurso(porcentaje) | Lista(carpeta) | Imagen(hueco) | Error(tipo) | Desconocido
  cancel(número)
    - un solo trabajo a la vez; start mientras otro está en curso lo cancela primero
    - un resultado de un trabajo cancelado o reemplazado nunca se publica: su hueco se libera
    - Error(tipo, causa) con tipo en {NoResponde, NoEncontrado, NoSePudoAbrir}; MalFormada cuenta como
      NoResponde; sin memoria para la imagen cuenta como NoSePudoAbrir, y la causa (bytes pedidos y fallo de
      la reserva) va al registro del host, nunca a la pantalla

TnfsFolder (resultado de Listar; lo dibuja MenuController sin reservar memoria)
  total (después de filtrar), hasta 20 entradas {esCarpeta, nombre para mostrar (38 columnas), ruta completa}
    - filtro, orden y marca de §0; los nombres que no se pueden mostrar se sustituyen al dibujarlos

MemoryImageStore (host)
  adopt(bytes) -> hueco | SinHueco ; source(hueco) -> ImageByteSource ; release(hueco)
    - un hueco no se libera mientras su fuente está adjunta (mismas reglas H1 que ImageRepository)

MenuController.key(código) -> MenuReply   (contrato de #40, ampliado)
  - espacio de códigos: $00..$3F tecla real; $FE sondeo; $FF redibujo; $40..$FD se enmascaran con $3F
  - $FE y $FF se comparan antes de enmascarar, así que nunca se confunden con una tecla
  - acción 2 = esperar: la pantalla muestra «CARGANDO nn%» debajo del pie y el Atari vuelve a preguntar
  - durante una carga: Esc cancela, el sondeo informa y cualquier otra tecla se ignora
  - Esc en la raíz de una fuente TNFS vuelve a la principal; en otra carpeta sube un nivel
```

Programa 6502:

- **Normalización:** después de leer `CH` y antes de `STA DAUX1`, hace `AND #$3F`. Ejemplos: Shift+A ($7F) se
  envía como $3F, Control+A ($BF) como $3F y Control+Shift+S ($FE) como $3E, que es `S`. Para el host no cambia
  nada, porque ya ignoraba Shift y Control (#40). Lo que cambia es que $FE y $FF ya no pueden salir de una
  tecla.
- **Límite que ya existía:** Control+Shift+A daría $FF en `CH`, que el SO usa para «sin tecla». No se puede
  leer, ni en #40 ni ahora.
- **Acción 2:** espera unos 15 tics de RTCLOK mirando `CH`. Si no hay tecla, envía $FE con un AUX2 nuevo; si
  hay tecla, la normaliza y la envía. Las acciones 0 y 1 no cambian, y la respuesta sigue midiendo 962 bytes.

## 4. Datos y flujo

**Entidades**

- **Fuente TNFS:** número (2 a 8) y nombre del servidor, de `--sources`. Su sesión vive en `TnfsClient`.
- **Ubicación en el menú:** fuente y ruta actual (UTF-8), solo en el host. Al volver a abrir una fuente se
  empieza en `/`.
- **Trabajo:** número, tipo, estado, porcentaje y resultado.
- **Hueco de memoria:** bytes de la imagen, su SHA-256 y quién lo usa (ninguno, en validación, unidad n).
- **Unidad:** lo que muestra (`shown_`) pasa de «entrada del catálogo local» a «entrada local o imagen TNFS
  (nombre corto y hueco)».

**Flujo principal** (AC3 y AC4: `Atari Logo.atr` a D1 y arranque):

1. El usuario está en `/Aportes_Juan_Larrain/0- Prueba` y pulsa la letra de la ATR. Se abre la vista Pick.
2. Pulsa `1`. `MenuController` pide `Traer(2, ruta)` y espera hasta 250 ms. La imagen pesa 92 KB y tarda unos
   0,4 s, así que la primera respuesta es acción 2 con «CARGANDO nn%».
3. El Atari sondea cada 0,25 s. Cuando el trabajo termina, el hueco pasa a «en validación»: se analiza la
   cabecera ATR con `atr_layout`. Si es válida, D1 queda asignada al hueco, la principal muestra
   `ATARI LOGO.ATR` y debajo del pie `D1: ATARI LOGO.ATR (A ARRANCA)`. El registro anota la ruta, los bytes y el
   SHA-256.
4. `A`: se monta `*tnfs*k` en D1 por el buzón, como en #40, y el Atari hace COLDSV. Desde aquí el servidor ya
   no hace falta.

**Fallo o cancelación:** la ranura del trabajo cambia de número. El resultado que llegue después se descarta y
su hueco se libera. Lo asignado y lo montado no se tocaron en ningún momento, porque solo cambian en el paso 3.

- **Consistencia:** una unidad cambia solo cuando una imagen completa y validada está en un hueco. No hay
  estado a medias.
- **Migración:** ninguna; no hay datos guardados. El formato de `--sources` no cambia.

## 5. Estrategia de pruebas

`FakeTnfsServer` es propio, vive en `test/host/fakes` y se escribe desde el documento del protocolo, sin
reutilizar `TnfsCodec`, para que un error del códec no se esconda en las dos puntas. Sirve un árbol en memoria
y se programa por orden: retraso, silencio, rechazo, respuesta mal formada, sesión olvidada y respuesta tardía.
Con un reloj controlado por la prueba, «más de 7 s» no cuesta 7 s reales.

| Criterio | Nivel | Qué afirma | Qué se finge |
| --- | --- | --- | --- |
| AC1, AC2, AC10 | integración de host | raíz, entrar y subir, Esc en la raíz a la principal, «CARPETA VACIA» | servidor (falso) |
| AC9 | unidad (`TnfsFolder`) + integración | 129 entradas a 128 filtradas, 20 mostradas, «MOSTRANDO 20 DE 128», orden y marca `/`, sin entradas con punto | servidor |
| AC3, AC4 | integración de host + demo | D1 asignada, mensaje exacto y SHA-256 en el registro; el arranque en Altirra | servidor en la prueba; `pulgarcito` en la demo |
| AC5, AC6, AC16 | integración de host | «ERROR: SERVIDOR NO RESPONDE», D1 a D8 iguales y nunca acción de host caído | servidor mudo |
| AC7 | integración de host | «ERROR: SERVIDOR NO ENCONTRADO» | resolución de nombres fingida en `UdpTnfsPort` |
| AC8 | integración de host | «ERROR: ATR INVALIDA» y D3 igual, en una prueba aparte | servidor con una ATR rota |
| AC11 | regresión | las pruebas de host de #40 pasan sin cambios y el recorrido `run4` da lo mismo. Una prueba Python de #40 cambia a propósito: Shift+A ahora sale como AUX1 = $3F, no $7F | nada nuevo |
| AC12 | unidad + integración + demo | el códec no expone otra orden ni indicadores; `FakeTnfsServer` registra cada orden y los indicadores y el modo de cada OPEN, y la prueba exige que todas las órdenes estén en §0 y que cada OPEN lleve indicadores 0x0001 y modo 0; el registro de la demo muestra lo mismo para `pulgarcito` | — |
| AC13 | regresión | `E` sigue pendiente | — |
| AC14 | demo en Altirra | «CARGANDO nn%» que sube, Esc atendida y D2 con `ARCADE.ATR` en pantalla (montaje); después, una lectura SIO del sector 65 535 de D2 cuyos 256 bytes son iguales a los de la imagen descargada (D2 sirve el último sector) | real: `pulgarcito` |
| falta de memoria | integración de host | `ERROR: NO SE PUDO ABRIR` en pantalla, causa concreta en el registro y la unidad igual | reserva que falla, forzada por la prueba |
| AC15, AC19 | integración de host | con más de 7 s simulados, cada `'K'` contesta en menos de 250 ms con acción 2; al final, D2 montada o la carpeta | servidor lento, reloj |
| AC17, AC18 | integración de host | Esc cancela; la respuesta tardía no cambia D2 y su hueco se libera | servidor con respuesta tardía |
| AC20 | integración de host | «ERROR: NO SE PUDO ABRIR» y la vista igual | servidor que rechaza |
| AC21 | unidad (`TnfsCodec`, con muestras aleatorias) + integración | ninguna entrada hace leer fuera del datagrama; error de servidor y el host sigue | servidor mal formado |
| acción 2 del 6502 | unidad (Python, `Menu6502`) | con acción 2 vuelve a preguntar con $FE y AUX2 nuevo; una tecla durante la espera sale en su lugar; acciones 0 y 1 iguales | host fingido |
| normalización de teclas | unidad (Python, `Menu6502`) + integración de host | el 6502 envía $3F para A, Shift+A y Control+A, y $3E para Control+Shift+S, nunca $FE; con acción 2, Control+Shift+S durante la espera sale como $3E y no como sondeo. En el host, $3E en la lista elige la entrada `S`, y $FE nunca elige nada | host fingido; servidor falso |
| procedimiento del sector 65 535 | unidad (Python, `Menu6502`) | el programa de lectura llama a SIOV con dispositivo $31, unidad 2, orden `R`, 256 bytes y sector $FFFF, y deja los bytes en la dirección fija y en pantalla en hexadecimal | SIOV fingido |
| `UdpTnfsPort` real | integración por loopback | un `FakeTnfsServer` sobre un socket UDP real atiende un MOUNT y un READ | nada |
| SHA-256 | unidad | los vectores de FIPS 180-4 | — |
| memoria en el paso SIO | regresión | `test_sio_session_allocations` sigue en cero; `key()` no reserva memoria durante una carga | — |

- **Dónde está la línea e2e:** la demo en Altirra contra `pulgarcito` (AC1 a AC4, AC9, AC12, AC14), el caso de
  servidor caído (AC5 y AC6, con un nombre sin servidor) y `run4` (AC11). No entra en la CI porque depende de
  Altirra y del servidor de laboratorio.
- **Lo que tiene que ser real:** el programa 6502 corre en el `Menu6502` de las pruebas y en Altirra; la
  validación ATR es la de `rb_domain`; el `UdpTnfsPort` se prueba con un socket real al menos una vez.

**Comprobación del último sector de D2 en la demo (AC14):**

1. Con `ARCADE.ATR` montada en D2 desde la fuente TNFS, se asigna a D1, desde la fuente local `1`, un disco de
   lectura propio (`SECTOR-PROBE.ATR`). Lo genera `tools/make_sector_probe.py` en la carpeta `--images` del
   laboratorio y no se versiona. Se pulsa `A`.
2. El programa de ese disco pide por SIO a D2 el sector 65 535 (dispositivo $31, unidad 2, orden `R`, AUX1 =
   $FF, AUX2 = $FF, 256 bytes). Deja los bytes en `$0600-$06FF` y los muestra en pantalla en hexadecimal, con el
   estado de SIO.
3. Evidencia: la captura de la pantalla y un volcado de `$0600-$06FF` con el depurador de Altirra. Si el
   volcado no se puede guardar, se usa el hexadecimal de la captura.
4. Referencia: la copia de `ARCADE.ATR` descargada en solo lectura con la herramienta de laboratorio, en
   `D:\rb-e0\m41\ref`. Primero se comprueba que su SHA-256 es igual al que anotó el host al cargarla. Después se
   comparan los bytes 16 776 336 a 16 776 591 de la copia (16 de cabecera, 3 x 128 y 65 531 x 256) con los 256
   bytes leídos. Tienen que ser iguales byte a byte.
5. La descarga de 16 MB para la referencia se pide al responsable antes de la demo, como se hizo con
   `Atari Logo.atr`.

Ver `ARCADE.ATR` en D2 confirma el montaje en pantalla. La lectura del sector 65 535 confirma que D2 sirve el
último sector de la imagen.

## 6. Secuencia y dependencias

1. `docs/fuentes.md`: protocolo TNFS y FIPS 180-4 — depende de: nada — paralelo.
2. `rb_net_tnfs`: `TnfsCodec` con pruebas, incluidas las de entradas mal formadas — depende de 1 — paralelo con
   3, 4 y 6.
3. `FakeTnfsServer` en `test/host/fakes`, sin usar el códec — depende de 1 — paralelo.
4. SHA-256 propio con vectores FIPS — depende de 1 — paralelo.
5. `TnfsClient`: sesión, reintentos, descarte de respuestas ajenas, reconexión y cancelación — depende de 2 y 3
   — en serie.
6. Programa 6502: normalización `AND #$3F`, acción 2, sondeo $FE y tecla durante la espera, con sus pruebas
   (incluidas las de modificadores) — depende de: nada — paralelo.
6b. `tools/make_sector_probe.py`: disco propio que lee el sector 65 535 de D2, con su prueba en `Menu6502` —
   depende de: nada — paralelo.
7. `UdpTnfsPort` con resolución de nombres y prueba por loopback — depende de 3 — paralelo con 5.
8. `MemoryImageStore` y ruteo en `HostImageRepository` — depende de: nada — paralelo.
9. `TnfsFolder` (filtro, orden, 20 y total, nombres para mostrar) — depende de: nada — paralelo.
10. `TnfsLoader` (hilo, ranura, número de trabajo, cancelación, liberación) — depende de 5, 8 y 9 — en serie.
11. `MenuController`: vistas TNFS, acción 2, Esc, errores y textos; las pruebas de #40 sin cambios — depende de
    6 y 10 — en serie.
12. `main.cpp`: conectar fuentes, cargador y puerto; registro de órdenes TNFS y del SHA-256 — depende de 4, 7 y
    11 — en serie.
13. Documentos: ADR-018 (cliente TNFS de solo lectura y carga en memoria), ADR-017 enmienda 2 (acción 2 y
    sondeo) y README — depende de 11 — paralelo con 12.
14. Demo en Altirra: recorrido con `Atari Logo.atr`, `ARCADE.ATR` en D2 con la lectura del sector 65 535,
    servidor caído y `run4` — depende de 6b y 12 — en serie y al final.

- **Candidatos en paralelo:** 1, 3, 4, 6, 6b, 8 y 9 tocan archivos distintos.
- **Orden obligado:** el cliente necesita el códec y el servidor falso; el controlador necesita el cargador y
  la acción 2; la demo necesita todo.

## 7. Riesgos y decisiones abiertas

**Riesgos** (de más a menos probable o grave)

| Riesgo | Cuándo aparece | Impacto | Cómo se mitiga |
| --- | --- | --- | --- |
| Carrera entre el hilo de carga y el hilo SIO | Cancelar justo cuando termina una carga | Se monta una imagen cancelada, o se libera un hueco en uso | Una sola ranura con cerrojo y número de trabajo; las unidades solo cambian en el hilo SIO; la prueba AC18 fuerza el orden malo; un revisor de tier 1 o más mira la concurrencia |
| La espera de 0,25 s del 6502 choca con SIOV o con NetSIO | La demo de `ARCADE.ATR` (65 s, unos 260 sondeos) | Altirra pierde una trama o muestra host caído | Se prueba primero en `Menu6502`; en Altirra, una carga larga temprana, antes de la demo completa |
| La espera de hasta 250 ms en `key()` frena NetSIO | Cada tecla durante una carga | Crédito o latido de NetSIO fuera de tiempo | 250 ms está muy por debajo de los 7 s del Atari; se mide en la demo; se puede bajar sin cambiar el contrato |
| Memoria del PC con varias imágenes de 16 MB | Ocho unidades con imágenes grandes | Hasta unos 144 MB; si no hay memoria, falla la carga | El hueco se libera al desmontar; falta de memoria da `ERROR: NO SE PUDO ABRIR`, con la causa en el registro y la unidad igual (M41-6) |
| Un cambio de 6502 rompe la lectura de teclas de #40 | La normalización `AND #$3F` | Una tecla de #40 deja de funcionar | La prueba de modificadores y las pruebas de host de #40 sin cambios; `run4` repetido en la demo |
| La lectura SIO del sector 65 535 no es posible desde el SO | El SO o la versión de Altirra limitan el sector o el tamaño de 256 bytes | No se puede demostrar que D2 sirve el último sector | El disco de lectura usa SIOV directo con la DCB completa, sin el FMS; se prueba en `Menu6502` y, antes de la demo, con una imagen local de 65 535 sectores |
| Código de error de «sesión desconocida» | El servidor se reinicia | No se reconecta | Se toma el código del documento del protocolo y se prueba con el servidor falso |
| Tailscale por relé, lento o con pérdidas | Demo desde otra red | Muchos reintentos | Tres intentos con la espera que anuncia el servidor; sigue siendo área no formulable en la spec |
| Una prueba sigue sin querer el código de otro | Al escribir el códec o el servidor falso | Viola ADR-012 | Solo el documento público; `docs/fuentes.md`; revisión de procedencia en el PR |

**Decisiones abiertas**

- **Número de la issue y nombre de la rama:** se deciden antes de escribir código (ver *Aislamiento*).
- **Descarga de 16 MB para la referencia del sector 65 535:** se pide al responsable antes de la demo.

## Decisiones tomadas en este plan

Van a `02-DOCS/wiki/sdd/decisions.md` en la rama de la etapa, con la tarea en la que se aplican. No van a
`wiki/harness/decisions.md`, que es solo del harness.

- **M41-1:** la red TNFS corre en un hilo de carga; `key()` contesta en 250 ms como máximo y, si el trabajo no
  terminó, contesta con la acción 2 «esperar».
- **M41-2:** una imagen TNFS se trae entera a un hueco de memoria (`*tnfs*0` a `*tnfs*8`) antes de validarla.
  `ImageLocation`, `rb_application` y `rb_domain` no cambian.
- **M41-3:** el 6502 normaliza cada tecla con `AND #$3F` antes de enviarla. Las teclas reales van de $00 a $3F;
  el sondeo es $FE y el redibujo $FF, fuera de ese rango. El sondeo lleva un AUX2 nuevo, así que M40-2 sigue
  igual. Pedido del responsable: los seis bits bajos de $FE coinciden con `S`.
- **M41-4:** el códec TNFS solo puede codificar las 9 órdenes de lectura, y la única apertura es
  `openReadOnly`, con indicadores fijos 0x0001 y sin parámetros de indicadores. Las pruebas comprueban órdenes e
  indicadores de cada OPEN. Es la garantía estructural de AC12 (pedido del responsable).
- **M41-5:** `FakeTnfsServer` se escribe sin usar `TnfsCodec`.
- **M41-6:** sin memoria para una imagen: `ERROR: NO SE PUDO ABRIR` en pantalla, la causa concreta en el
  registro del host y el disco anterior se conserva (decisión del responsable).
- **M41-7:** la demo de `ARCADE.ATR` en D2 lee por SIO el sector 65 535 con un disco de lectura propio y compara
  sus 256 bytes con la imagen descargada (pedido del responsable).

## Aislamiento

Main está cerrado. Antes de escribir código hay que elegir dónde se trabaja: esta rama
(`fix/e1-netsio-credit-reconnect`, que no tiene relación con esto), una rama nueva desde `origin/main` (la
recomendada, por ejemplo `feat/<issue>-menu-tnfs`) o desbloquear main. Esa elección es del responsable.

## Tasks
<!-- generated by tasks on 2026-11-18; IDs are stable, do not renumber -->

Notas para leer la tabla:

- **Comprobaciones:** se corren desde la raíz del repositorio, con los mismos comandos que la CI. «ctest X» significa
  `cmake --build build/host && ctest --test-dir build/host -R X --output-on-failure` en verde. «py X» significa
  `python -I -m unittest discover -s tools/tests -p X -v` en verde.
- **TDD:** en cada tarea de código, la prueba se escribe primero y se ve fallar por el motivo correcto. Ese fallo
  se anota en `progress/menu-tnfs.md` antes de implementar.
- **Trazas:** AC1 a AC21 se refieren a los criterios de la spec, en su orden. «R:» cita una restricción de *Qué
  debe respetar*; «M41-n», una decisión del plan.
- **Riesgo:** sale de `npx @ericrisco/rsc risk` sobre las rutas previstas (2026-11-18). En `implement` se
  recalcula sobre el diff real, y manda ese valor.

| ID | [P] | Tarea | Comprobación | Depende de | Traza | Riesgo |
| --- | --- | --- | --- | --- | --- | --- |
| T001 | [P] | Registrar el protocolo TNFS y FIPS 180-4 en `docs/fuentes.md` | las dos entradas existen, con título, fecha y uso; `python -I tools/check_notices.py` en verde | — | R: ADR-012 | 0 |
| T002 | [P] | Crear `rb_net_tnfs` con `TnfsCodec` de solo lectura | ctest `test_tnfs_codec`: las 9 órdenes byte a byte; `encodeOpenReadOnly` escribe `01 00 00 00`; la interfaz no tiene otra apertura; `parse` da Ajena y MalFormada, con 10 000 datagramas aleatorios sin leer fuera del datagrama (ASan); `python -I tools/check_layering.py` en verde | T001 | AC12, AC21, M41-4 | 1 |
| T003 | [P] | Escribir `FakeTnfsServer` sin usar `TnfsCodec` | ctest `test_fake_tnfs_server`: sirve un árbol en memoria; registra cada orden y los indicadores y el modo de cada OPEN; se programa con retraso, silencio, rechazo, mal formada, sesión olvidada y respuesta tardía; reloj de la prueba | T001 | AC12, AC15 a AC21, M41-5 | 1 |
| T004 | [P] | Implementar SHA-256 propio | ctest `test_sha256`: vectores de FIPS 180-4 (vacío, «abc», 448 bits, un millón de «a») y la ATR de referencia da `9f21ef9a…99c96` | T001 | AC4 | 1 |
| T005 |  | Implementar `TnfsClient` | ctest `test_tnfs_client`: MOUNT, listDir, stat y `readWholeReadOnly` contra el servidor falso; 3 intentos con la espera anunciada y EAGAIN; Ajena descartada; reconexión silenciosa una vez; OPEN y OPENDIR siempre cerrados al cancelar o fallar; el registro del servidor solo tiene órdenes de §0 y cada OPEN tiene `0x0001` y modo 0 | T002, T003 | AC12, AC16, AC20, AC21 | 1 |
| T006 | [P] | Normalizar teclas y añadir la acción 2 al programa 6502 | py `test_menu_program.py`: A, Shift+A y Control+A salen como $3F; Control+Shift+S como $3E y nunca $FE; acción 2 sondea con $FE y AUX2 nuevo a unos 15 tics; tecla durante la espera, normalizada, en lugar del sondeo; acciones 0 y 1 iguales; `menu_program.inc` regenerado y sin diferencias con el generador | — | AC15, AC17, AC19, M41-3 | 1 |
| T007 | [P] | Crear `tools/make_sector_probe.py` | py `test_sector_probe.py`: en `Menu6502`, SIOV con $31, unidad 2, `R`, AUX $FF $FF, 256 bytes; bytes en `$0600-$06FF` y en pantalla en hexadecimal, con el estado de SIO | — | AC14, M41-7 | 1 |
| T008 | [P] | Implementar `UdpTnfsPort` | ctest `test_udp_tnfs_port`: un `FakeTnfsServer` sobre un socket UDP real de loopback atiende MOUNT y READ; un nombre que no se resuelve da NotFound (resolutor inyectable) | T003 | AC7 | 1 |
| T009 | [P] | Añadir `MemoryImageStore` y el ruteo `*tnfs*n` en `HostImageRepository` | ctest `test_memory_image_store`: 9 huecos; un hueco adjunto no se libera; `*tnfs*0` a `*tnfs*8` abren la memoria; `*menu*` y los archivos siguen igual; ctest `test_host_menu` sin cambios en verde | — | AC3, AC14, M41-2 | 1 |
| T010 | [P] | Implementar `TnfsFolder` | ctest `test_tnfs_folder`: 129 entradas dan 128 y se muestran 20, «MOSTRANDO 20 DE 128»; carpetas primero con `/`; orden sin mayúsculas; sin entradas con punto; «CARPETA VACIA»; nombres que no se pueden mostrar se sustituyen; título recortado por la izquierda | — | AC1, AC9, AC10 | 1 |
| T011 |  | Implementar `TnfsLoader` | ctest `test_tnfs_loader`: un trabajo a la vez; número de trabajo; cancelar; un resultado tardío o reemplazado se descarta y libera su hueco, también con el orden malo forzado; NoResponde, NoEncontrado y NoSePudoAbrir; sin memoria da NoSePudoAbrir con la causa | T005, T009, T010 | AC5, AC16, AC17, AC18, AC20, AC21, M41-1, M41-6 | 1 |
| T012 |  | Añadir las vistas TNFS, la acción 2 y Esc a `MenuController` | ctest `test_menu_tnfs` y ctest `test_host_menu` (este, sin cambios en sus casos); cada `'K'` contesta en 250 ms o menos con más de 7 s simulados; $FE nunca elige y $3E en la lista elige `S`; textos exactos de §0; `E` sigue pendiente; ctest `test_sio_session_allocations` en verde y `key()` sin reservas durante una carga | T006, T011 | AC1 a AC3, AC5, AC6, AC8 a AC11, AC13, AC15 a AC21 | 1 |
| T013 |  | Conectar en `main.cpp` las fuentes, el cargador, el puerto y el registro | `ctest --test-dir build/host --output-on-failure` todo en verde; `retrobridge_host` compila; con una fuente cuyo nombre no se resuelve, el registro anota el error; el registro anota cada orden TNFS con los indicadores de OPEN, el SHA-256 al terminar una carga y la causa de una falta de memoria | T004, T008, T012 | AC4, AC7, AC12, M41-6 | 1 |
| T014 | [P] | Escribir ADR-018, la enmienda 2 de ADR-017, el README y M41-1 a M41-7 en `wiki/sdd/decisions.md` | los archivos existen y enlazan entre sí; sin emoji; `python -I tools/check_notices.py` en verde | T012 | R: ADR-012, ADR-017 | 0 |
| T015 |  | Probar en Altirra, antes de la demo, una carga larga y la lectura del sector 65 535 en local | en Altirra: una imagen local generada de 65 535 sectores en D2 y el disco de lectura en D1; los 256 bytes leídos son iguales a los de la imagen; una carga del servidor de prueba de más de 7 s muestra «CARGANDO nn%» sin host caído | T007, T013 | AC14, AC15 | 0 |
| T016 |  | Hacer la demo en Altirra contra `pulgarcito` | evidencia en `D:\rb-e0\m41`: capturas y registros del recorrido con `Atari Logo.atr` (AC1 a AC4, AC9, AC12); `ARCADE.ATR` en D2 con «CARGANDO nn%» y el sector 65 535 igual al de la copia descargada (antes, pedir permiso para la descarga de 16 MB); servidor caído (AC5, AC6, AC7); `run4` (AC11) | T015 | AC1 a AC7, AC9, AC11, AC12, AC14 | 0 |
| T017 |  | Cerrar: todas las comprobaciones y los controles de la CI | cada fila anterior marcada con evidencia; `ctest` completo con `-DRB_SANITIZE=ON`; `python -I -m unittest discover -s tools/tests -v`; `check_layering.py` y `check_notices.py` en verde; la CI del commit publicado en verde, incluida la compilación ESP-IDF | todas | todos los AC | — |

**T002 — Interfaces**
- Produce: `rb::net_tnfs::TnfsCodec` en `components/rb_net_tnfs`. Sigue el patrón de `components/rb_netsio/CMakeLists.txt`: el mismo archivo compila como componente ESP-IDF y como biblioteca de host.
- Produce: en `tools/check_layering.py`, la entrada `"rb_net_tnfs": ("rb/core/", "rb/netsio/", "rb/net_tnfs/")`, solo para usar `DatagramPort`. Sin sockets ni ESP-IDF.
- Produce: `encodeOpenReadOnly(conexión: uint16, secuencia: uint8, ruta: const char*, out: Span<uint8_t>) -> Result<size_t>`. No existe `encodeOpen` con indicadores.
- Produce: `parse(datagrama, ordenEsperada, secuenciaEsperada) -> Respuesta | Ajena | MalFormada`.

**T003 — Interfaces**
- Produce: `FakeTnfsServer` en `test/host/fakes/fake_tnfs_server.hpp`, usable como `rb::netsio::DatagramPort` en memoria y detrás de un socket real para T008.
- Produce: `log() -> [{orden, indicadores, modo}]`, y una programación por orden: `delay(ms)`, `silence()`, `reject(código)`, `malformed()`, `forgetSession()`, `lateReply()`.

**T005 — Interfaces**
- Consume: `TnfsCodec` (T002), `rb::netsio::DatagramPort` y un reloj inyectable.
- Produce: `mount()`, `listDir(ruta, visita)`, `stat(ruta)`, `readWholeReadOnly(ruta, destino, progreso, cancelado)` y `umount()`, con los resultados de §3 del plan.

**T009 — Interfaces**
- Produce: `MemoryImageStore::adopt(bytes) -> hueco | SinHueco`, `source(hueco) -> ImageByteSource&` y `release(hueco)`. La ubicación `*tnfs*<n>`, con n de 0 a 8, la abre `HostImageRepository::doOpen`.

**T010 — Interfaces**
- Produce: `TnfsFolder{total, entries[0..20]: {isDir, display[38+1], path}}`, y `title(ruta, ancho) -> texto recortado por la izquierda`.
- Regla de `display` (y del título): igual que #40, las letras ASCII `a`-`z` pasan a mayúsculas (`toScreen(..., upper = true)` de `menu_image.cpp`). Cada carácter que no es ASCII imprimible (0x20-0x7E) se sustituye por un solo `?`. Un carácter UTF-8 de varios bytes cuenta como uno: su byte inicial y los de continuación (0x80-0xBF) dan un único `?`. Un byte inválido también da `?`. Ejemplo: `Atari Tarots Español5.atr` se muestra `ATARI TAROTS ESPA?OL5.ATR`. `path` guarda los bytes originales sin tocar, y es lo que se usa para abrir.

**T011 — Interfaces**
- Consume: `TnfsClient` (T005), `MemoryImageStore` (T009) y `TnfsFolder` (T010).
- Produce: `start(Listar|Traer) -> número`, `poll(número) -> EnCurso(%) | Lista | Imagen(hueco) | Error(tipo, causa) | Desconocido` y `cancel(número)`.
- Regla del porcentaje, entero de 0 a 99 mientras está `EnCurso`, que nunca baja:
  - **Traer:** `100 * bytes leídos / tamaño` (el tamaño viene del STAT previo), en división entera y con tope en 99. El 100 no se muestra: al terminar aparece el resultado.
  - **Listar:** dos fases. Mientras dura READDIR, 0 (aún no se conoce el total). Durante los STAT, `100 * entradas consultadas / entradas por consultar`, con tope en 99. Así el porcentaje sube en cuanto se conoce el total. Las entradas que empiezan por punto se descartan sin STAT. Todas las demás se consultan, también las que no terminan en `.ATR`, porque una carpeta no se distingue por el nombre.
  - La prueba de AC19 programa el retraso del servidor falso en los STAT, para que el porcentaje suba de forma observable.

**T012 — Interfaces**
- Consume: `TnfsLoader` (T011) y las acciones 0, 1 y 2 de `make_menu.py` (T006), exportadas en `menu_program.inc` como `kMenuActionShow`, `kMenuActionBoot` y `kMenuActionWait`, con `kMenuPollKey = 0xFE`.
- Produce: `MenuController::key(code)`. Compara $FE y $FF antes de `& 0x3F` y contesta en 250 ms o menos.
- Regla de pantalla: los nombres de D1 a D8 y el mensaje `D1: <NOMBRE> (A ARRANCA)` usan el `display` de T010. Por ejemplo, `Atari Logo.atr` se ve `ATARI LOGO.ATR` (AC3).

## Review Workload Forecast

| Dimensión | Previsión | Por qué |
| --- | --- | --- |
| Líneas cambiadas | 2 500 a 3 500, con pruebas | componente nuevo (códec y cliente), servidor falso, cuatro piezas de host, controlador, 6502 y documentos |
| Archivos y áreas | unos 30: `components/rb_net_tnfs`, `host/src`, `test/host` (net_tnfs, menu, fakes), `tools` (generador, disco de lectura y capas), `docs` | 17 tareas, 13 de código |
| Riesgo | tier máximo 1; ninguna tarea de tier 2 | `rsc risk` sobre las rutas previstas; se recalcula en cada diff |
| Entrega sugerida | `ask-on-risk`, en dos PR | pasa de largo el presupuesto de 400 líneas. PR A: T001 a T005 y T008 (protocolo, servidor falso, SHA-256 y puerto UDP, sin cambio visible). PR B: el resto y la demo. La decisión es del responsable antes de `implement` |

## Revisions

- 2026-11-18: borrador inicial desde la spec aclarada y aprobada.
- 2026-11-18: cambios pedidos por el responsable antes de `tasks`:
  - apertura solo con `openReadOnly` e indicadores fijos 0x0001, con prueba de los indicadores de cada OPEN;
  - normalización de teclas `AND #$3F` en el 6502, para que $FE no se confunda con Control+Shift+S, con prueba
    de modificadores;
  - lectura del sector 65 535 de D2 en la demo, comparada con la imagen descargada;
  - falta de memoria: `ERROR: NO SE PUDO ABRIR`, causa en el registro y disco anterior conservado (M41-6).
- 2026-11-18: el responsable aprobó el plan («Aprobar plan»). Queda listo para `tasks`.
- 2026-11-18: `tasks`: 17 tareas (T001 a T017), interfaces por tarea y previsión de revisión. Aprobadas por el responsable («Aprobar tareas»); listas para `analyze`.
- 2026-11-18: `analyze`, hallazgos medios corregidos con la aprobación del responsable («Corregir los 4 medios»): F4, esperas TNFS en §0 y §3; F2, regla de pantalla en las Interfaces de T010 y T012; F3, regla del porcentaje en las Interfaces de T011. Ningún ID cambió.
