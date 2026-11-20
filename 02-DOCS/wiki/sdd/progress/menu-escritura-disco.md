---
type: progress
title: Progreso — menu-escritura-disco
description: Avance por tarea del plan menu-escritura-disco, con evidencia.
tags: [sdd, progress, host, sio, netsio, escritura]
timestamp: 2026-11-19T23:59:00Z
topic: sdd
slug: menu-escritura-disco
plan: 02-DOCS/wiki/sdd/plans/menu-escritura-disco.md
---

# Progreso — menu-escritura-disco

## W001 — Medida de la escritura por NetSIO (hecha, 2026-11-19)

Método: sonda Python propia (no versionada, ADR-012) registrada en el hub como dispositivo; atiende solo D2 sobre
una copia de una ATR de 720 x 128. DOS 2.5 en D1 de Altirra 4.31; hub `netsiohub` de laboratorio. Copia de
`D1:TORRES.BAS` a `D2:TORRES.BAS` desde el DOS.

Resultado con `81 <sync> 01 'A' <write_size>` y `write_size` = 129 (`81 00`, LE: datos + checksum):

- 36 de 36 escrituras `W` completas (sectores 4 a 35, 360 y 361). Todas: 129 bytes recibidos, checksum válido.
- Cada escritura llega como `02` (65 bytes de datos) + `02` (63 bytes) + `09 <checksum> <sync>`; los bloques
  pueden partirse de otra forma: se juntan hasta `09`.
- `09` lleva el byte de checksum y un número de sincronización nuevo; se contesta con `81 <sync> 01 'A' 00 00` y
  luego COMPLETE en un `02`.
- La ATR quedó con `TORRES  BAS` en el directorio (sector 361) y la VTOC actualizada.

Conclusión para W003: `write_size` = tamaño del sector + 1. No se probó 128: no hace falta para el producto.

Hallazgos del laboratorio:

- Una unidad de Altirra solo vacía sigue contestando. Debe estar en **Off** para que sus comandos lleguen por
  NetSIO. Con la misma ATR en D2 de Altirra (VRWSafe) y en la sonda, el Atari descartaba tramas (8 lecturas
  seguidas del sector 361, error 143). El README del host dice «Dejar D1 vacía»: corregir en W007 a «D1 en Off».
- La sonda necesita la pausa t5 (≥ 250 µs) entre ACK y COMPLETE, como `SioSession` (300 µs).
- Formatear (`!`, `"`) por la sonda no se completó; está fuera del alcance de la spec.

Evidencia: `D:\rb-e0\m45-w001\plus1.jsonl` (intento 6) y `sonda-d2-red.atr`; intentos 1 a 5 conservados con sus
nombres (`plus1-intento1..5.jsonl`).

## W002 — Dominio, archivos y puerto (hecha, 2026-11-19)

- `ErrorCode::WriteProtected`; `ImageByteSource::writable`/`writeAt` (por defecto: no escribible);
  `DiskImage::readOnly` sigue a la fuente y `writeSector`; `VirtualDrive::writeSector`/`validateWrite`;
  `SioTransport::receiveAfterAck` declarado (por defecto TransportError).
- `StdioByteSource::open(path, Access)`: `r+b` si se puede, si no `rb` (protegida). `writeAt`: fseek,
  fwrite y fflush. `StdioImageRepository(base, writes = false)`: un solo escritor por ruta (sin distinguir
  mayúsculas). Desvío del plan: la identidad es la ruta, no `file_identity` del host (`rb_file_storage` no puede
  depender del host); `build_image_path` ya rechaza rutas alternativas.
- Pruebas: `test_disk_write` (13 casos nuevos) y 4 casos nuevos en `test_stdio_storage`. 63 de 63 en verde;
  `check_layering` y `check_notices` OK. Firmware ESP-IDF: lo compila el CI.
- Riesgo `rsc risk`: tier 1. Un revisor (correctitud): sin defectos que bloqueen; 4 detalles menores, uno
  arreglado (comentario largo), tres aceptados (comparación sin mayúsculas también en Linux, conservadora; sin
  prueba de lectura-escritura-lectura intercaladas; `receiveAfterAck` se prueba en W004).
- Controles (verify por tarea, corrida nueva sobre aaec76f + cambios):
  - pruebas: PASS, 63 de 63 (`ctest --test-dir build/host`).
  - compilación: PASS, MinGW ucrt64, `-Werror`, 0 avisos.
  - seguridad: PASS por lectura dirigida. Solo `stdio_byte_source.cpp` abre para escribir, y solo con
    `ReadWriteIfAllowed`; la ruta la arma `build_image_path`, que rechaza `..`, `/` inicial, `\` y `:`.
  - trazabilidad: PASS. Escribir en la ATR = M45-1 (AC1-AC3); protección por atributo = M45-3 (AC4); un
    escritor por archivo = suposición de la spec; `writes = false` por defecto = firmware de solo lectura (AC10).
  - **SUSTITUIDA** — sanitizadores: el CI compila con `-DRB_SANITIZE=ON`; aquí no (MinGW). No detecta
    errores de memoria hasta que corra el CI.
  - **HERRAMIENTA-AUSENTE** — firmware ESP-IDF: sin `idf.py` en este PC; lo compila el CI.

## W003 — Recepción de datos por NetSIO (hecha, 2026-11-19)

- `NetSioSioTransport::receiveAfterAck(frame)`: con la sync del comando pendiente envía
  `81 <sync> 01 'A' <frame.size() LE>`; junta `01`/`02` en `frame` hasta `09`, cuyo byte es el último y cuya sync
  queda pendiente para el ACK/NAK de los datos (siguiente `send()`). Largo distinto, `11` nuevo, reset o
  `dataTimeoutUs` (1 s): TransportError; la sync pendiente se suelta vacía. Un `11` durante los datos conserva la
  trama nueva. Un `09` fuera de una escritura sigue contestándose vacío.
- Estadísticas nuevas: `dataFramesReceived`, `dataFramesDiscarded`.
- Pruebas: 10 casos nuevos en `test_netsio_transport` (37 en total) y 1 en `test_netsio_transport_allocations`
  (0 asignaciones). 63 de 63 en verde; `check_layering` y `check_notices` OK.
- Control negativo: con `dataComplete_ = true` fijo, fallan los casos de largo mayor y menor (2 fallos);
  restaurado, 36 de 36. Sin `abandonData()` en el reset, falla el caso de reset.
- Riesgo `rsc risk`: tier 1. Un revisor (correctitud): sin defectos. Detalles aplicados: prueba de reset durante
  los datos y comentario de `dataFramesDiscarded`. Aceptados: sin prueba de error de recepción durante los datos;
  `abandonData()` también corre fuera de una escritura (inofensivo).
- Controles (verify por tarea, corrida nueva tras el último cambio):
  - pruebas: PASS, 63 de 63; `test_netsio_transport` 37 de 37; asignaciones 4 de 4 (0 en la escritura).
  - compilación: PASS, MinGW ucrt64, `-Werror`, 0 avisos.
  - seguridad: PASS por lectura dirigida. Los bytes del Atari solo se copian dentro de `frame` (tope
    `dataSize_`); el exceso solo se cuenta.
  - trazabilidad: PASS. `write_size` = sector + 1 = medida W001 (M45-1); `09` fuera de escritura vacío = sin
    cambio para el resto.
  - **SUSTITUIDA** — sanitizadores: solo en el CI (`-DRB_SANITIZE=ON`). No detecta errores de memoria aquí.
  - **HERRAMIENTA-AUSENTE** — firmware ESP-IDF: sin `idf.py`; lo compila el CI.

## W004 — Escritura en el despachador SIO (hecha, 2026-11-19)

- `command::kWriteSector` (0x57) y `kPutSector` (0x50). `SioCommandDispatcher(bay, allowWrites = false)`: apagado,
  `W`/`P` reciben NAK y D0 como antes. Encendido: sector 0 o fuera de rango, NAK; si no, Accepted con una trama de
  datos del tamaño del sector + 1 (`incomingFrame()`), también con unidad vacía (luego ERROR).
- `acceptData()`: checksum malo, NAK de datos, nada escrito, D1. `execute()`: COMPLETE; protegida, ERROR y D3;
  otro fallo, ERROR y D2. Caso de uso nuevo `WriteSector` (rb_application).
- `SioSession`: con datos, `receiveAfterAck` en lugar de `send(ACK)`; luego ACK/NAK de datos; t5 desde el ACK de
  datos; COMPLETE o ERROR solo. Cualquier fallo de transporte abandona el comando sin escribir.
- STATUS (`StatusFrameEncoder`): con escrituras activas, disco protegido = byte 1 `$BF`. D1-D3 describen la última
  lectura o escritura, como el byte 1: STATUS no los borra; los borra el siguiente R/W/P aceptado. Un rechazo por
  protección deja `$BF`, no `$EF`. Desvío menor del plan: el plan no decía cuándo se borran D1-D3; si se borraban
  al informar, el segundo STATUS mostraba `$AF`.
- `ScriptedSioTransport::receiveAfterAck`: registra el ACK como un envío y entrega la trama encolada; sin trama o
  con otro largo, TransportError.
- Pruebas: `test_sio_dispatcher` +9 (24), `test_sio_session` +6 (22), `test_status_frame_encoder` +2 (11),
  `test_sio_session_allocations` +1 (5, 0 asignaciones en W completa, NAK de datos y trama ausente). 63 de 63.
- Controles negativos: sin el control de checksum fallan 2 casos; sin `allowWrites`, 3 casos.
- Riesgo `rsc risk`: tier 1. Un revisor (correctitud): sin defectos. Aceptados: `abandon()` redundante tras un
  NAK fallido; un fallo de transporte antes de los datos no marca D1 (el Atari reintenta).
- Controles (verify por tarea, corrida nueva tras el último cambio):
  - pruebas: PASS, 63 de 63.
  - compilación: PASS, MinGW ucrt64, `-Werror`, 0 avisos; `check_layering` y `check_notices` OK.
  - seguridad: PASS por lectura dirigida. La trama de datos va a un búfer fijo de 257 bytes; su largo sale de
    `validateWrite`, no del Atari.
  - trazabilidad: PASS. W/P completos = AC1-AC2; protegida = ERROR y `$BF` = AC4; `allowWrites` apagado = AC8.
  - **SUSTITUIDA** — sanitizadores: solo en el CI.
  - **HERRAMIENTA-AUSENTE** — firmware ESP-IDF: sin `idf.py`; lo compila el CI.

## W005 — Escritura en el host (hecha, 2026-11-19)

- `HostImageRepository`: archivos con `writes = true` (un solo escritor por ruta). Menú e imágenes TNFS siguen
  sin escritura. `main.cpp`: `SioCommandDispatcher(bay, true)` solo en `--images`; `--image` sigue de solo
  lectura.
- `MountReport::writeProtected` (rb_application). Pantalla principal: unidad protegida = nombre recortado a 29
  columnas + ` SL`. TNFS siempre protegida. D1: la marca se toma al asignar y se confirma al arrancar.
- `MenuSioTransport` reenvía `receiveAfterAck` y sigue cada `W`/`P` a D1-D8: un registro por escritura al
  observador. `main.cpp` lo escribe como evento JSONL `sio_write`:
  `drive=n cmd=W|P sector=s result=complete|error|nak [crc32=xxxxxxxx] [cause=no_disk|protected|io] [reply=lost]`
  (`host/src/sio_write_log.hpp`, CRC-32 IEEE propio, sin tabla).
- Desvío del plan: la causa es una clase (`no_disk`, `protected`, `io`), no el texto del sistema; `rb_file_storage`
  no guarda `errno`. El archivo bloqueado de verdad se prueba en la demo (W006); aquí, con una fuente que falla.
- Pruebas: `test_host_write` nuevo (9 casos): P y W leídos de vuelta y conservados tras cerrar y reabrir (AC1-AC3);
  atributo de solo lectura = STATUS `$BF`, ERROR, bytes iguales, marca (AC4, AC7); misma ATR en dos unidades;
  menú en D1 (AC6); imagen TNFS no escribible (AC5); fallo al guardar = ERROR y `cause=io` (AC9); NAK del comando
  y de datos; COMPLETE perdido sigue registrado; CRC-32 de `123456789` = `CBF43926`. `test_menu_tnfs`: 5 líneas de
  unidad TNFS ahora llevan ` SL` (lo pide AC7). 64 de 64 en verde. Ejecutable `retrobridge_host` compila.
- Controles negativos: con `writes = false`, fallan 2 casos; sin la marca, 2 casos; sin el registro del COMPLETE
  perdido, 1 caso.
- Riesgo `rsc risk`: tier 1. Un revisor (correctitud): un defecto arreglado (un COMPLETE que no salía dejaba la
  escritura sin registro; ahora queda con `reply=lost`). Aceptado: tras un arranque y un reset en frío, la marca
  de D1 es la de su último montaje (cosmético). Sin pruebas de la marca tras desmontar o reset (aceptado).
- Controles (verify por tarea, corrida nueva tras el último cambio):
  - pruebas: PASS, 64 de 64.
  - compilación: PASS, MinGW ucrt64, `-Werror`, 0 avisos; `retrobridge_host.exe` enlaza; `check_layering` y
    `check_notices` OK.
  - seguridad: PASS por lectura dirigida. Solo se escriben archivos de `--images` que el sistema deja escribir;
    la ruta la arma `build_image_path`; el registro no copia datos del Atari, solo su CRC.
  - trazabilidad: PASS. AC1-AC7 y AC9 en `test_host_write`; AC11 (pruebas de menús) en verde.
  - **SUSTITUIDA** — sanitizadores: solo en el CI.
  - **HERRAMIENTA-AUSENTE** — firmware ESP-IDF: sin `idf.py`; lo compila el CI.

## W006 — Demo en Altirra (hecha, 2026-11-19)

Montaje: Altirra 4.31 con todas las unidades D en **Off**; hub `netsiohub` en 9997; un solo `retrobridge_host`
(04af098) con `--images D:\rb-e0\m45-escritura\images --peer netsiohub`. Todo por el host: menú en D1, DOS 2.5
chileno (`DOS25D1D2D3.ATR`) en D1, ATR de datos en D2. Carpeta `D:\rb-e0\m45-escritura` (no `m45`: esa carpeta
ya tiene la demo de otra función).

1. Copia `D1:` → `D2:PRUEBA.BAS` con `escritura-d2.atr` (escribible): 8 `sio_write` `result=complete`
   (datos, VTOC 360, directorio 361). SHA-256 de la ATR: `93641c15…` → `1dc6b9f8…` (AC1, AC2).
2. Host cerrado y relanzado; reset en frío, D1 y D2 montadas otra vez desde el menú: el directorio de D2 muestra
   `TORRES BAS 032` y `PRUEBA BAS 004`, 671 libres (AC3). Antes de volver a montar, `D2:` daba ERROR 144: la
   unidad estaba vacía tras relanzar (comportamiento correcto, captura conservada).
3. `escritura-ro.atr` con atributo de solo lectura en D2: STATUS de D2 = `02 43 00 BF E0 00 A0` (byte 1 `$BF`);
   la copia da error en el DOS; 8 `sio_write` `result=error cause=protected` (VTOC); SHA-256 sin cambio
   `93641c15…` (AC4). Luego el DOS manda 112 `W` al sector 0, todos NAK (`result=nak`): basura del DOS tras el
   error, igual que en W001; nada escrito.
4. El menú en D1 también contesta STATUS con `$BF` (protegido).

Evidencia en `D:\rb-e0\m45-escritura`: `host.jsonl` (2 corridas; `host-run1.jsonl` = la primera),
`sha256-antes.txt`, `sha256-despues-escritura.txt`, `sha256-despues.txt`, `2-sin-montar-error144.png`,
`3-relanzado-prueba-bas.png`, `version.txt`.

- **HERRAMIENTA-AUSENTE** — firmware ESP-IDF (AC10): sin `idf.py` en este PC; lo compila el CI.
- No anotado: el número de error que mostró el DOS en el paso 3.

## W007 — Documentación (hecha, 2026-11-19)

- `docs/adr/0019-escritura-de-sectores.md` (nuevo): secuencia NetSIO de escritura, `allowWrites` solo con
  `--images`, STATUS `$BF` y D1 a D3, un solo escritor, evento `sio_write`.
- ADR-016: Estado y tabla apuntan a ADR-019. ADR-017: enmienda 8 (marca « SL»).
- `host/README.md`: unidades de Altirra en Off; párrafo de escritura. `README.md`: enlace a ADR-019.
- `docs/fuentes.md`: FEP-003 releída (no fija `write_size`) y medición propia W001/W006.
- Controles: `check_notices` OK; `check_layering` OK; sin emoji; `rsc risk` tier 1, un revisor (refuter-correctness): 1 hallazgo menor (faltaba la causa `no_drive` en ADR-019), corregido.
- **NO-APLICA** — tests y build: solo cambian documentos.

## Cierre (2026-11-19)

- Verificación: `02-DOCS/wiki/sdd/verifications/menu-escritura-disco-2026-11-19.md`.
- Prueba nueva de AC8 (reintento). Corrección del re-montaje de la misma ATR (opción A, aprobada): desmontar y
  después montar; prueba `test_remounting_the_same_atr_keeps_it_writable` en rojo antes y en verde después.
- Desviación anotada: sector fuera de rango en `W`/`P` = NAK al comando (spec línea 106 dice ERROR).

## Entrega

PR #53 fusionado en main (01dbfe1). CI 6 de 6 en verde: sanitizers y firmware (AC10). Veredicto final:
PASA (`02-DOCS/wiki/sdd/verifications/menu-escritura-disco-2026-11-19-r2.md`).
