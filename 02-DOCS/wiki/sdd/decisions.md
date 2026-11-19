---
type: decisions
title: Decisiones del método ASDD
description: Registro de solo anexado de las decisiones tomadas en los bucles ASDD y la tarea en que se tomaron.
topic: sdd
---

# Decisiones del método ASDD

Registro de solo anexado. Cada decisión indica la tarea y quién la tomó. El detalle técnico vive en `docs/adr/`;
aquí solo queda el resumen y el enlace. Las decisiones del arnés siguen en [`wiki/harness/decisions.md`](../harness/decisions.md).

## Pantalla principal del menú (Issue #40, PR #41)

Referencia de las decisiones del producto: [ADR-017](../../../docs/adr/0017-menu-de-arranque.md), enmienda 1.
Alcance: [Issue #40](https://github.com/calaluis/retrobridge-chile/issues/40).

| # | Fecha | Decisión | Tarea | Procedencia |
|---|---|---|---|---|
| M40-1 | 2026-11-17 | Terminal delgado: el programa 6502 envía cada tecla con `$5A` `K` y el host dibuja la pantalla entera. Reemplaza el comando `M` y el catálogo dentro del disco. | T1, T4 | Propuesta de Claude en el plan; aprobada con la spec. ADR-017, enmienda 1, puntos 1 y 2. |
| M40-2 | 2026-11-18 | AUX2 numera cada tecla; si `SIOV` repite AUX1 y AUX2, el host reenvía la respuesta sin aplicar la tecla otra vez. Un redibujo (`$FF`) o una trama descartada tras `FF` borra esa memoria. | T4 | Hallazgo «should-fix» del revisor de tier 1; corrección de Claude. ADR-017, enmienda 1, punto 1. |
| M40-3 | 2026-11-17 | D1 queda asignada y sigue sirviendo el menú hasta `A`; D2 a D8 se montan enseguida por el buzón. | T3 | Propuesta de Claude en el plan; aprobada con la spec. ADR-017, enmienda 1, punto 3. |
| M40-4 | 2026-11-17 | TNFS (fuentes 2 a 8) y `E` quedan pendientes y lo dicen en pantalla. `--sources` admite hasta 7 nombres, `U` desmonta y C: muestra «SIN CINTA». | T2, T3 | Decisión del usuario (Q1 TNFS pendiente; Q2 a Q4 aceptadas). ADR-017, enmienda 1, punto 4. |
| M40-5 | 2026-11-18 | La línea de mensajes va debajo del pie de teclas, en la pantalla principal y en la lista. | T1 | Pedido del usuario en la revisión humana; la lista se alineó por coherencia (Claude). ADR-017, enmienda 1, consecuencias. |

## Fuentes TNFS de solo lectura (menu-tnfs, Issue #41)

Plan: `wiki/sdd/plans/menu-tnfs.md`. M41-1 a M41-7 los escribe T014; estas filas son las decisiones tomadas al implementar.

| # | Fecha | Decisión | Tarea | Procedencia |
|---|---|---|---|---|
| M41-8 | 2026-11-18 | Tras reconectar, el cliente TNFS repite la operación entera desde el principio: LSEEK no está entre las órdenes permitidas. `ReadSink::begin()` se llama otra vez y la lectura vuelve al desplazamiento 0; un listado llama a `DirVisitor::restart()`. | T005 | Propuesta de Claude al implementar. |
| M41-9 | 2026-11-18 | La espera por intento que anuncia el servidor tiene un techo de 5 s (`kMaxWaitUs`). EAGAIN espera lo que pide, pero nunca más que una espera de intento, y como mucho 10 veces por orden. Se aparta del plan, que no ponía techo. | T005 | Hallazgos de seguridad del revisor de tier 1 (rondas 1 y 2); el techo de 5 s lo decidió el responsable. |
| M41-10 | 2026-11-18 | La cancelación corta las esperas de READDIR, STAT y READ en tramos de 100 ms; `stat()` acepta un `CancelCheck` opcional. OPEN y OPENDIR se esperan hasta el final, para conocer su número y cerrarlo siempre. | T005 | Hallazgos del revisor (rondas 1 y 2); corrección de Claude. |
| M41-11 | 2026-11-18 | Si el MOUNT de la reconexión falla o es rechazado, el resultado es SinRespuesta. Un fallo al recibir que no sea un tiempo agotado (por ejemplo «puerto inalcanzable») termina el intento al momento. | T005 | Hallazgo del revisor (ronda 1); el segundo punto se mantuvo a propósito: no hay nadie escuchando. |
| M41-12 | 2026-11-18 | Lectura: 3 respuestas OK seguidas sin datos dan MalFormada; recibir más datos de los pedidos, o EOF antes del tamaño que dio STAT, también. | T005 | Propuesta de Claude al implementar. |
| M41-13 | 2026-11-18 | El cliente no reserva memoria: el listado y la lectura entregan por `DirVisitor` y `ReadSink`. El progreso sale de `begin(size)` y de cada `data(offset, bytes)`, sin una llamada aparte. El reloj es `TnfsClock`, propio, porque las capas no dejan a `rb_net_tnfs` usar `rb/app`. | T005 | Propuesta de Claude al implementar; pregunta del revisor sobre la interfaz, respondida así. |
| M41-14 | 2026-11-18 | `UdpTnfsPort` resuelve el nombre solo a IPv4 (getaddrinfo con AF_INET) y reutiliza `UdpDatagramPort`, que gana `openTo(dirección, puerto)`: liga loopback si el par es loopback y cualquier interfaz si no; conectado, solo acepta datagramas de ese par. `open(puerto)` de NetSIO no cambia. El resolutor se inyecta para que las pruebas no usen DNS. | T008 | Propuesta de Claude al implementar. |
| M41-15 | 2026-11-18 | En POSIX, `UdpDatagramPort::receive` usa `recvmsg` y rechaza un datagrama marcado con MSG_TRUNC: con `recv` un datagrama mayor que el búfer se cortaba sin aviso, contra el contrato de `DatagramPort`. En Windows ya era WSAEMSGSIZE. | T008 | Hallazgo del revisor de tier 1 (ronda 1); corrección de Claude. La rama POSIX la compila y prueba la CI de Linux. |
| M41-16 | 2026-11-18 | En el programa 6502 solo la acción 2 espera y sondea con $FE. La acción 1 y cualquier otra distinta de 0 arrancan con COLDSV, como antes de menu-tnfs. El bloque pasa a 11 sectores; un relleno mantiene las dos display lists en una página. La constante del sondeo es `kMenuPollKey`, el nombre que consume T012. | T006 | Hallazgos del revisor de tier 1 (ronda 1); corrección de Claude. |
| M41-17 | 2026-11-18 | En la lista TNFS, una carpeta lleva la marca `/` al final del nombre (`JUEGOS/`), y la marca no se pierde al recortar: el nombre ocupa 37 columnas y la barra la 38. Un título más largo que su ancho se recorta por la izquierda y empieza con `..`. Las entradas con nombre vacío no se cuentan. Al ordenar se usa `compareNames` de la lista local (#40). | T010 | Decisión de Claude dentro de la regla de §0; el plan no fijaba la posición de la marca ni el indicador de recorte. |
| M41-18 | 2026-11-18 | `MemoryImageStore` guarda cada imagen en un `ImageBuffer` propio, reservado con `new (std::nothrow)`, en lugar de un `std::vector`. El proyecto compila sin excepciones (ADR-004): un vector sin memoria aborta el host, y la spec pide «ERROR: NO SE PUDO ABRIR» con la causa en el registro. `adopt` recibe un `ImageBuffer`; un búfer movido queda vacío también en tamaño (revisión L6). | T011 | Decisión de Claude; cambia la firma de `adopt` de T009 (los bytes siguen siendo la entrada). |
| M41-19 | 2026-11-18 | `TnfsLoader`: (1) `start` no recibe el tamaño esperado, porque `readWholeReadOnly` ya hace STAT y da el tamaño a `begin`; (2) un MOUNT rechazado es `NoSePudoAbrir`, no `NoResponde`, porque el servidor sí contestó; (3) una fuente sin servidor da `NoEncontrado`; (4) un STAT rechazado (la entrada desapareció) quita esa entrada, y uno sin respuesta falla el listado; (5) una entrada cuya ruta pasa de 255 bytes se omite, porque nunca se podría abrir; (6) el SHA-256 se calcula en el hilo de carga y viaja con el resultado, para no hacerlo al pulsar una tecla; (7) el listado pasa al menú con `TnfsFolder::swap`, sin reservar ni liberar en el hilo del menú; (8) no hay tope de tamaño propio (spec): una reserva imposible da `NoSePudoAbrir` con la causa. | T011 | Decisión de Claude; revisión de tier 1 (L7, L8 y L9: intencionados). |
| M41-20 | 2026-11-18 | El menú no libera huecos de `MemoryImageStore`: los entrega con `TnfsLoader::recycle(hueco)`, que solo marca el hueco en una cola fija de 9 marcas y despierta al hilo de carga. Ese hilo llama a `release` antes de cada trabajo y al quedar ocioso; si el hueco sigue abierto o asignado, la marca queda para el siguiente intento. Así `adopt` y `release` siguen en el hilo de carga (contratos de T009 y T011) y liberar una imagen de hasta 16 MB no ocurre en `key()` ni al colectar una respuesta (ADR-011). | T012 | Decisión del responsable (opción B, «B: recycle»), sobre la recomendación de Claude. |
| M41-21 | 2026-11-18 | Pantalla de una carpeta TNFS: título ` FUENTE n: ` y la ruta recortada por la izquierda en las 20 columnas del título; cada fila es la letra, un espacio y el `display` de 38 columnas de T010 (la fila local sigue siendo ` A  ` y 30 columnas). Debajo del pie: el mensaje del menú si hay uno; si no, `CARPETA VACIA` o `MOSTRANDO 20 DE <total>`; si no, el texto de la lista local. `CARGANDO nn%` lleva el número sin relleno (`CARGANDO 0%`). Un error del cargador deja la vista donde estaba; una ATR inválida o la unidad ocupada al terminar la carga también (la fuente local vuelve a la principal, como en #40). La espera de `key()` es de 200 ms (`kLoadWaitMicros`). | T012 | Decisión de Claude dentro de §0 y de la regla de pantalla; revisión de tier 1 (L12 y L13: intencionados). |
