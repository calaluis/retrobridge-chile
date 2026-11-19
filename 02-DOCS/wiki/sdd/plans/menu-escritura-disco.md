---
type: plan
title: Plan — Escritura en disco desde el Atari (D1 a D8)
description: CÓMO se construye la spec menu-escritura-disco: W y P por NetSIO hasta la ATR local, protección por atributo, ADR-019.
tags: [sdd, plan, host, sio, netsio, escritura]
timestamp: 2026-11-20T00:30:00Z
topic: sdd
slug: menu-escritura-disco
spec: 02-DOCS/wiki/sdd/specs/menu-escritura-disco.md
branch: feat/menu-escritura-disco
status: approved
---

# Plan — Escritura en disco desde el Atari (D1 a D8)

## 0. Global constraints (literales, para cada tarea)

- C++17 con `-fno-exceptions -fno-rtti -Werror` (ADR-004). Sin reservas en la ruta SIO (ADR-011).
- Código propio (ADR-012). Fuentes: C016555 (comandos `W` `$57` y `P` `$50`, trama de datos + checksum) y FEP-003
  (`81` con `write_size` de 16 bits LE; `09` lleva el último byte, el checksum, y pide sincronización). Sin leer
  código de FujiNet, del hub ni de Altirra.
- `check_layering` en verde (ADR-002): el dominio no conoce archivos; `rb_netsio` no conoce `rb_sio`.
- Escrituras apagadas por defecto en cada pieza compartida: sin activarlas, todo se comporta como hoy (NAK a `W`/`P`,
  `write_size 0`, `09` vacía). Solo `retrobridge_host --images` las activa.
- La ATR se escribe en su sitio (M45-1). Protegida si el archivo no abre para escritura (M45-3). TNFS y `*menu*`
  siempre protegidos (M45-2). ADR-018 no cambia.

## 1. Context & constraints

- Spec §What result they get, §Behaviour y 11 criterios de aceptación.
- Hoy (main `57b8b0a`):
  - `ImageByteSource` solo tiene `readAt`; `DiskImage::readOnly()` devuelve siempre `true`.
  - `DriveStatus` ya tiene `writeProtected`, pero STATUS lo manda en 0.
  - `SioTransport` solo envía: no hay forma de recibir una trama de datos del Atari.
  - `SioCommandDispatcher` acepta `R` y `S`; el resto, NAK.
  - `NetSioSioTransport` contesta `81 … write_size 0` y responde `09` vacía.
  - `StdioByteSource::open` usa `"rb"`.
  - El firmware ESP32 no usa `SioSession` ni el despachador.
- FEP-003 no dice si `write_size` cuenta el checksum ni el orden de `02`/`01` antes de `09`. Lo mide la tarea W001.

## 2. Architecture

```text
 Atari --W/P--> NetSioSioTransport --frame--> SioSession --> SioCommandDispatcher --> WriteSector (app)
                 | 81 ACK, write_size = n      | receiveData(n)  | accept: W/P si escrituras activas   |
                 | 02/01 datos, 09 checksum    | ACK/NAK de datos| execute: COMPLETE o ERROR           v
                 | 81 ACK/NAK de datos         | COMPLETE/ERROR  |                         VirtualDrive::writeSector
                                                                                          DiskImage::writeSector
                                                                                          ImageByteSource::writeAt
                                                                                            StdioByteSource ("r+b")
```

- **Secuencia SIO de escritura (C016555):** trama de comando → ACK → trama de datos (n bytes + checksum) → ACK o
  NAK de datos → COMPLETE o ERROR. Un checksum malo recibe NAK de datos y no escribe.
- **Unidad protegida:** acepta el comando, recibe los datos y contesta ERROR sin escribir. STATUS lleva el bit
  de protección. Así el DOS informa «protegido», no «error de dispositivo».
- **Escrituras activas:** un indicador en el despachador (`allowWrites`, falso por defecto). Apagado: `W`/`P`
  reciben NAK como hoy.
- **El puerto declara el tamaño antes del ACK.** En NetSIO, el ACK del comando lleva `write_size`. Por eso el
  puerto recibe un método que envía el ACK y espera la trama en una sola llamada.

## 3. Interfaces & contracts

```text
// rb_domain
ImageByteSource::writable() const -> bool                         // por defecto false
ImageByteSource::writeAt(offset, Span<const uint8_t>) -> Result    // por defecto WriteProtected
DiskImage::readOnly() -> !source.writable()
DiskImage::writeSector(SectorNumber, Span<const uint8_t>) -> Result
VirtualDrive::validateWrite(SectorNumber) const -> Result<uint16_t>   // tamaño, sin E/S
VirtualDrive::writeSector(...) -> Result     // marca lastOperationFailed como readSector
VirtualDrive::status() -> writeProtected = hasDisk && disk.readOnly()
ErrorCode::WriteProtected                                           // nuevo, al final del enum

// rb_application
WriteSector (caso de uso, contexto de la unidad, como ReadSector)
SioTransport::receiveAfterAck(Span<uint8_t> frame) -> Result<void>
    // envía ACK anunciando frame.size() (datos + checksum) y llena frame;
    // por defecto NotSupported (ScriptedSioTransport y MenuSioTransport lo implementan)

// rb_sio
command::kWriteSector = 0x57, command::kPutSector = 0x50
SioCommandDispatcher(bay, allowWrites = false)
    accept: W/P con sector válido -> Accepted (dataLength = tamaño del sector); sector inválido -> NAK
    executeWrite(frame): checksum malo -> NAK de datos; protegida -> ERROR; fallo E/S -> ERROR; bien -> COMPLETE
StatusFrameEncoder: protección permanente en el byte 1 (estado FDC invertido): bit 6 en 0 ($BF con disco
    protegido y sin error). Byte 0: D3 = 1 tras una escritura rechazada por protección; D2 = 1 tras un PUT fallido;
    D1 = 1 tras una trama de datos con checksum malo. Ver §7, «Bit de protección».
SioSession::runAccepted: si el comando lleva datos, receiveAfterAck en lugar de send(ACK)

// rb_netsio
NetSioSioTransport::receiveAfterAck: 81 <sync> 01 'A' <write_size LE>; junta 02/01; 09 da el último byte y deja
    la sincronización pendiente; el siguiente send() de 1 byte (ACK/NAK de datos) la contesta con write_size 0.
    Más bytes de los esperados, o 09 antes de tiempo: TransportError y 81 vacío.

// rb_file_storage
StdioByteSource::open(path, Access) con Access::ReadOnly (por defecto) o Access::ReadWriteIfAllowed:
    intenta "r+b"; si falla, "rb" y writable() = false. writeAt: fseek + fwrite + fflush; corto o error = IoError.
StdioImageRepository(folder, writes = false): con writes, abre ReadWriteIfAllowed salvo que otra fuente abierta
    ya sea escribible sobre el mismo archivo (identidad de archivo, file_identity del host) -> esa abre ReadOnly.

// host
HostImageRepository pasa writes = true a sus archivos. MemoryImageStore y MenuImage no cambian (no escribibles).
main.cpp: SioCommandDispatcher(bay, allowWrites = true) solo con --images.
Pantalla principal: unidad protegida -> el nombre se recorta a 29 columnas y lleva " SL" al final de sus 32.
JSONL: evento sio_write | drive=n cmd=W|P sector=s result=complete|error|nak crc32=xxxxxxxx [cause=...]
```

## 4. Data model & flow

- Un sector de densidad simple: 128 bytes + 1 de checksum. Densidad doble: 256 + 1, salvo los sectores 1 a 3
  (128 + 1). El tamaño viene de `validateWrite`, igual que en la lectura.
- La trama de datos vive en un búfer fijo del despachador (`kMaxFrameSize`, ya 257). Sin reservas.
- La escritura va al archivo con `fflush` antes de COMPLETE (suposición de la spec). Sin `fsync`.
- Reintento de `SIOV`: el Atari repite comando y datos; el mismo sector recibe los mismos bytes. Idempotente.

## 5. Testing strategy

- **Dominio** (`test_disk_image`, `test_virtual_drive`): writeSector en densidad simple y doble, sectores 1-3 de
  128, fuera de rango, fuente no escribible = WriteProtected, `readOnly()` sigue a la fuente, status con bit.
- **SIO** (`test_sio_dispatcher`, `test_sio_session` con `ScriptedSioTransport`): `W` y `P` completos; checksum
  malo → NAK de datos y nada escrito; protegida → ERROR; `allowWrites` apagado → NAK como hoy; STATUS byte 1 = $BF;
  reintento = un solo resultado; 0 reservas en `step()`.
- **NetSIO** (`test_netsio_transport` con datagramas propios): `81` con `write_size` correcto en LE; `02` + `09`;
  `01` sueltos + `09`; bytes de más; `09` sin escritura planeada sigue vacía; 0 reservas.
- **Archivos** (`test_stdio_storage`): `r+b` escribe y relee tras cerrar y
  abrir; atributo de solo lectura → `writable()` falso; misma ATR dos veces → la segunda no escribible;
  `writes = false` → nunca `r+b`.
- **Host** (`test_host_menu`): marca ` SL` en la principal; TNFS y menú protegidos; JSONL `sio_write`.
- **Firmware**: compilación ESP-IDF y sus pruebas en verde; `check_layering` en verde.
- **Demo** (W006): DOS 2.5 en D1, guardar en D2, relanzar host, leer; luego ATR con atributo de solo lectura.

## 6. Sequencing & dependencies

W001 (medida) y W002 (dominio, archivos y puerto) en paralelo. W003 (NetSIO) depende de W001 y W002; W004 (SIO), de W002; W003 y W004 en paralelo.
W005 (host) depende de W003 y W004. W006 (demo) y W007 (docs) al final.

## 7. Risks & open decisions

- **`write_size` sin definir en FEP-003.** Riesgo alto si se adivina. W001 lo mide en Altirra con un `81` de
  `write_size` = n + 1 y luego n, registrando los datagramas. Lo que se observe queda en ADR-019 y en
  `docs/fuentes.md` como observación propia (como E0).
- **Un `fwrite` en el hilo SIO.** Pocos ms en disco local; dentro del límite de 16 ms del ACK de datos porque
  la emulación queda en pausa hasta el `81` (FEP-003). La escritura va después del ACK de datos, antes de COMPLETE.
- **Dañar una ATR.** Riesgo aceptado en la spec. Las pruebas usan copias en carpetas temporales.
- **Cambio de interfaz compartida.** `writeAt`/`receiveAfterAck` con implementación por defecto: los adaptadores
  y dobles actuales compilan sin tocarlos.
- **Bit de protección en STATUS.** H2b adoptó la lectura de Altirra: D3 del byte 0 es «falló por protección»,
  no una marca permanente. La marca permanente del 810 es el bit 6 del estado del controlador (byte 1, invertido).
  Fuente a registrar en W007: hoja de datos del FD1771 (estado tipo I, bit 6 «write protect»). W001 comprueba en
  Altirra qué error muestra el DOS (se espera 144 tras ERROR). El criterio de la spec «el bit de protección vale 1»
  se lee como «STATUS marca la protección»: byte 1 = $BF. Confirmado por el responsable (§8).
- **Marca en pantalla:** ` SL` (solo lectura) a la derecha del nombre, recortado a 29 columnas (§8).

## 8. Precisiones del responsable antes de aprobar (2026-11-19)

- STATUS: la protección permanente va en el byte 1 = `$BF` (bit 6 en 0); D3 del byte 0 solo tras un rechazo.
  Respeta H2b.
- Marca en la principal: ` SL` al final de las 32 columnas del nombre.

## Tasks

Rama `feat/menu-escritura-disco` (worktree `.worktrees/feat-menu-escritura-disco`, desde main `57b8b0a`). Riesgo
previsto por tarea (0/1/2); `rsc risk` decide el real.

| # | [P] | Tarea | Done-check | Depende de | Cubre | Riesgo |
|---|---|---|---|---|---|---|
| W001 | [P] | Medir en Altirra la secuencia NetSIO de una escritura: rama de prueba local del transporte que contesta `81` con `write_size` n + 1 y n, y registra cada datagrama | JSONL en `D:\rb-e0\m45-w001` con un `W` de 128 bytes del DOS: orden de `02`/`01`, posición de `09`, valor de `write_size` que lleva a `09` con el checksum; resultado escrito en `progress/menu-escritura-disco.md`; nada de esto se fusiona | — | R: FEP-003 | 1 |
| W002 | [P] | Dominio y archivos: `writable`/`writeAt`, `WriteProtected`, `DiskImage::writeSector`, `VirtualDrive::validateWrite`/`writeSector`/status, `StdioByteSource` con `Access`, `StdioImageRepository(writes)` con una sola fuente escribible por archivo; declarar `SioTransport::receiveAfterAck` con su versión por defecto | ctest `test_disk_image`, `test_virtual_drive`, `test_stdio_storage` con los casos de §5; `writes = false` nunca abre `r+b`; `check_layering` verde | — | AC1-AC4, M45-1, M45-3 | 2 |
| W003 | [P] | NetSIO: `receiveAfterAck` con el `write_size` medido; `09` fuera de escritura sigue vacía | ctest `test_netsio_transport`: casos de §5; `test_netsio_transport_allocations` 0 reservas | W001, W002 | AC1, AC2 | 2 |
| W004 | [P] | SIO: `W`/`P` en el despachador con `allowWrites`; `receiveAfterAck` en `ScriptedSioTransport`; `SioSession` con trama de datos; STATUS bit 3 | ctest `test_sio_dispatcher`, `test_sio_session`: casos de §5; STATUS de unidad protegida con byte 1 = $BF y D3 = 1 solo tras un rechazo; incluido `allowWrites` apagado = NAK byte a byte igual que hoy | W002 | AC1, AC2, AC4, AC8 | 2 |
| W005 |  | Host: `HostImageRepository` con escrituras, `MenuSioTransport` reenvía `receiveAfterAck`, `allowWrites` con `--images`, marca ` SL`, `sio_write` en JSONL, fallo al guardar | ctest `test_host_menu` y uno nuevo `test_host_write`: AC3 (cerrar y reabrir), AC5 (TNFS), AC6 (menú), AC7 (marca), AC9 (archivo bloqueado → ERROR + causa); todas las pruebas de menús anteriores en verde | W003, W004 | AC3, AC5-AC7, AC9, AC11 | 2 |
| W006 |  | Demo en Altirra: DOS en D1, guardar archivo en D2, relanzar host, leerlo; ATR con atributo de solo lectura da error de protección | `D:\rb-e0\m45`: `host.jsonl` con `sio_write`, SHA-256 de la ATR antes/después, capturas; firmware ESP-IDF compila; un solo `retrobridge_host` en el hub | W005 | AC1-AC4, AC10 | 0 |
| W007 | [P] | Docs: ADR-019 nuevo (escritura de sectores; reemplaza «`write_size 0`» y «`09` vacía» de ADR-016), estado de ADR-016 apunta a ADR-019, ADR-017 enmienda 8 (marca ` SL`), README del host, `docs/fuentes.md` (FEP-003 releído y medición W001) | archivos existen y enlazan; sin emoji; `python -I tools/check_notices.py` en verde | W005 | R: ADR-016, ADR-017 | 0 |
- Aprobado por el responsable («Apruebo») el 2026-11-19, con las precisiones de §8.
