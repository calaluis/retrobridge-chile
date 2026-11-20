---
type: progress
title: Progreso — menu-formatear-disco
description: Avance por tarea del plan menu-formatear-disco, con evidencia.
tags: [sdd, progress, host, sio, menu, formateo]
timestamp: 2026-11-20T03:00:00Z
topic: sdd
slug: menu-formatear-disco
plan: 02-DOCS/wiki/sdd/plans/menu-formatear-disco.md
---

# Progreso — menu-formatear-disco

## F001 — Dominio y app (hecha, 2026-11-20)

- `ErrorCode::DensityMismatch` (al final del enum; nombre en `diagnostic_console`).
- `DiskImage::format(Density)`: fuente nula, después protección y después densidad; ninguna de las tres escribe.
  Solo `Single` (720 x 128) y `Enhanced` (1040 x 128). Escribe `$00` sector a sector con un búfer fijo de 128 bytes
  (`static constexpr`, sin reservas). El primer error de E/S lo detiene; los sectores anteriores ya quedaron en
  cero.
- `VirtualDrive::format` (`NoDiskMounted`, registra el fallo) y `rb::app::FormatDisk`.
- Pruebas: 6 casos nuevos en `test_disk_write` (simple, media, otra densidad incluida la doble, protegida, error
  de E/S a mitad, unidad vacía). El fake `MemoryByteSource` gana `fail_writes_after(n)`. Primero en rojo (no
  compilaba), después en verde: 19 de 19 en `test_disk_write`, 64 de 64 en total.
- Desvío del plan: `FormatDisk` no tiene prueba propia, como `WriteSector`; lo prueban el despachador y la sesión
  en F002.
- Riesgo `rsc risk`: tier 1. Un revisor (correctitud): sin hallazgos. Un detalle aceptado: no hay prueba de
  formatear una ATR con cabecera inconsistente o bytes de sobra (el revisor confirmó que `sectorCount` sale del
  tamaño real y la cabecera y los bytes de sobra no se tocan).
- Controles (verify por tarea, corrida nueva sobre b872251 + cambios):
  - pruebas: PASS, 64 de 64 (`ctest --test-dir build/host`).
  - compilación: PASS, MinGW ucrt64, `-Werror`, 0 avisos.
  - seguridad: PASS por lectura dirigida. Solo escribe por `ImageByteSource::writeAt`, dentro de los rangos de
    `AtrLayout::locate`, y nunca en una fuente protegida.
  - trazabilidad: PASS. Formatear simple y media = M46-1 (AC1, AC2); otra densidad = AC3; protegida = AC4.
  - **SUSTITUIDA** — sanitizadores: el CI compila con `-DRB_SANITIZE=ON`; aquí no.
  - **HERRAMIENTA-AUSENTE** — firmware ESP-IDF: sin `idf.py` en este PC; lo compila el CI.

## F002 — Formateo por SIO (hecha, 2026-11-20)

- `sio_codes.hpp`: `kFormat` `$21` y `kFormatMedium` `$22`.
- Despachador: sin `allowWrites`, NAK como hoy. Con él, acepta sin E/S (AUX no importa) y `execute` llama a
  `FormatDisk` (`$21` = simple, `$22` = media). Responde COMPLETE o ERROR y siempre un búfer del tamaño del sector:
  `$FF` en todo (lista vacía terminada en `$FFFF`, M46-4) más checksum. 128 bytes con la unidad vacía; 256 en
  doble densidad (que da ERROR). STATUS: D3 si está protegida, D2 en otro fallo (densidad, E/S, unidad vacía).
- `SioSession` no cambia: el formateo va por el camino de lectura (ACK, ejecución, código + trama).
- Pruebas: 8 casos en `test_sio_dispatcher`, 2 en `test_sio_session`, 1 en `test_sio_session_allocations`
  (formatear y rechazar sin reservas). Primero en rojo (3 suites fallaban), después en verde: 64 de 64.
- Riesgo `rsc risk`: tier 1 (previsto 2). Un revisor (correctitud): un hallazgo menor, ya planeado para F004: el
  host aún no registra el formateo en `sio_write` (`menu_sio_transport.cpp` solo sigue `W`/`P`). Sin otros
  defectos: sesión, banderas de STATUS, largo del búfer y firmware (`allowWrites = false`, NAK) revisados.
- Controles (verify por tarea, corrida nueva sobre 15850fd + cambios):
  - pruebas: PASS, 64 de 64. `retrobridge_host.exe` compila.
  - compilación: PASS, MinGW ucrt64, `-Werror`, 0 avisos.
  - seguridad: PASS por lectura dirigida. El formateo solo escribe con `allowWrites` y por `DiskImage::format`.
  - trazabilidad: PASS. `$21`/`$22` = M46-1 (AC1, AC2); otra densidad = AC3; protegida y vacía = AC4; búfer = M46-4.
  - **SUSTITUIDA** — sanitizadores: el CI compila con `-DRB_SANITIZE=ON`; aquí no.
  - **HERRAMIENTA-AUSENTE** — firmware ESP-IDF: sin `idf.py` en este PC; lo compila el CI.

## F003 — Crear ATR y lista (hecha, 2026-11-20)

- `host/src/create_atr.{hpp,cpp}`: `isNewAtrName` (1 a 25 de A-Z, 0-9, `-` y `_`, M46-3) y `createAtr`. Escribe
  `<NOMBRE>.ATR` con cabecera de 16 bytes y 720 x 128 (simple) o 1040 x 128 (media) sectores en `$00`, sin
  formatear (M46-2). Abre con `"wbx"` (creación exclusiva): un fichero existente nunca se toca (`AlreadyExists`).
  Si falla la escritura, borra lo que creó esta llamada (`IoError`).
- `MenuCatalog::insert`: devuelve la posición en orden; `kNotInserted` si la lista está llena (999), si el nombre
  ya está o si no es un nombre del catálogo. `full()` nuevo.
- Pruebas: `test_create_atr`, 8 casos. Primero en rojo (no compilaba), después en verde: 65 de 65.
- Riesgo `rsc risk`: tier 1. Un revisor (correctitud): un hallazgo, corregido. La regla de nombre aceptaba nombres
  de dispositivo de Windows (`NUL`, `CON`, `AUX`, `PRN`, `COM0-9`, `LPT0-9`): `NUL.ATR` abre el dispositivo,
  devuelve `Created` y no queda fichero. Ahora `isNewAtrName` los rechaza; prueba añadida (incluye `CONSOLA`,
  `COM10` y `NULO`, que sí valen). Sin otros defectos: cabecera, duplicados por mayúsculas (`EEXIST` en un
  sistema de ficheros insensible) y `std::remove` revisados.
- Controles (verify por tarea, corrida nueva sobre 6c61485 + cambios):
  - pruebas: PASS, 65 de 65. `retrobridge_host.exe` compila.
  - compilación: PASS, MinGW ucrt64, `-Werror`, 0 avisos.
  - seguridad: PASS por lectura dirigida. El nombre no puede salir de la carpeta ni abrir un dispositivo; nunca
    sobrescribe.
  - trazabilidad: PASS. Crear = M46-2 (AC5); nombre existente = AC6; nombre inválido = AC7; lista llena = AC8.
  - **SUSTITUIDA** — sanitizadores: el CI compila con `-DRB_SANITIZE=ON`; aquí no.
  - **HERRAMIENTA-AUSENTE** — firmware ESP-IDF: sin `idf.py` en este PC; lo compila el CI.

## F004 — Menú «Crear» y registro del formateo (hecha, 2026-11-20)

- `MenuController`: tecla `N` (`$23`) en la pantalla principal y vista `Create` en tres pasos (M46-5):
  - nombre: el 6502 lo edita en la fila de mensajes, tras `NOMBRE:` (acción 3, como el editor de servidores). Las
    minúsculas valen como mayúsculas: sin Shift el 6502 manda minúsculas. Nombre inválido = ` NOMBRE NO VALIDO:
    LETRAS, 0-9 Y -`; nombre ya en la lista = ` YA EXISTE <NOMBRE>.ATR`.
  - densidad: `S` simple, `M` media.
  - unidad: `2`..`8` o RETURN sobre la barra; `1` no se ofrece (D1 sirve el menú). Esc cancela en cada paso.
- Al crear: `createAtr`, `MenuCatalog::insert`, `shiftEntries` (corre en +1 las posiciones guardadas de las
  unidades, el montaje en vuelo, el remontaje y el cursor de la lista) y `mountOn`. Evento
  `menu_create name=… density=single|medium|- drive=n result=created|exists|invalid_name|io|full|no_folder|not_listed`.
- Sin carpeta: ` SIN CARPETA LOCAL`. Lista llena (999): ` LISTA LLENA: 999 IMAGENES`, antes de pedir el nombre.
- `main.cpp`: `setImagesFolder(--images, catalog)`.
- `sio_write` del formateo: `drive=n cmd=F|FM result=… [cause=no_disk|protected|density|io] [reply=lost]`, sin
  sector ni crc32. `MenuSioTransport` sigue `$21`/`$22`: tras el ACK pasa a la respuesta final.
- Pruebas: 10 casos nuevos en `test_host_write` (crear simple y media, formatear `F` y `FM`, otra densidad, protegida
  y vacía, nombre existente, nombre inválido, minúsculas, cancelar y D1, lista llena, nombres de unidades tras
  insertar). Primero en rojo (no compilaba), después en verde: 21 de 21 en `test_host_write`, 65 de 65 en total.
  Cinco pruebas del menú enlazan ahora `create_atr.cpp`.
- Riesgo `rsc risk`: tier 1. Un revisor (correctitud): un hallazgo, corregido. El editor del 6502 manda minúsculas
  sin Shift y no tiene `_`; el host rechazaba `disco1`. Ahora pasa el nombre a mayúsculas, y el mensaje ya no
  anuncia `_` (la regla M46-3 sigue aceptándolo). Detalle también corregido: el campo empezaba en la columna 8 y el
  cursor del 6502 quedaba fuera de la fila; ahora empieza en la 7. Sin otros defectos: índices tras insertar, orden
  de `key()`, máquina de estados del transporte y causa `density` revisados.
- Controles (verify por tarea, corrida nueva sobre 02c3981 + cambios):
  - pruebas: PASS, 65 de 65. `retrobridge_host.exe` compila.
  - compilación: PASS, MinGW ucrt64, `-Werror`, 0 avisos.
  - seguridad: PASS por lectura dirigida. El nombre pasa por `isNewAtrName`; crear nunca sobrescribe.
  - trazabilidad: PASS. Formatear = AC1, AC2; otra densidad = AC3; protegida = AC4; crear = AC5; existente = AC6;
    inválido = AC7; lista llena = AC8; pruebas anteriores en verde = AC10.
  - **SUSTITUIDA** — sanitizadores: el CI compila con `-DRB_SANITIZE=ON`; aquí no.
  - **HERRAMIENTA-AUSENTE** — firmware ESP-IDF: sin `idf.py` en este PC; lo compila el CI.

## Pendiente

F005 (demo en Altirra), F006 (docs).
