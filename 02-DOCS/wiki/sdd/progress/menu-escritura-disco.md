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

## Pendiente

W004 a W007.
