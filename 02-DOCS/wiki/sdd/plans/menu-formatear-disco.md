---
type: plan
title: Plan — Formatear y crear discos desde el Atari
description: CÓMO se construye la spec menu-formatear-disco: órdenes $21 y $22 hasta la ATR local, y la acción «crear» del menú.
tags: [sdd, plan, host, sio, menu, formateo]
timestamp: 2026-11-20T02:00:00Z
topic: sdd
slug: menu-formatear-disco
spec: 02-DOCS/wiki/sdd/specs/menu-formatear-disco.md
branch: feat/menu-formatear-disco
status: approved
---

# Plan — Formatear y crear discos desde el Atari

## 0. Global constraints (literales, para cada tarea)

- C++17 con `-fno-exceptions -fno-rtti -Werror` (ADR-004). Sin reservas en la ruta SIO (ADR-011).
- Código propio (ADR-012). Fuente del formateo: *Altirra Hardware Reference Manual* (ya en `docs/fuentes.md`),
  §10.2 «Format ($21)» y la sección del 1050 «Format Medium Density command». Sin leer código de FujiNet, del hub
  ni de Altirra.
- `check_layering` en verde (ADR-002): el dominio no conoce archivos; la regla del nombre vive en el host.
- El formateo sigue la misma llave que la escritura: `allowWrites` apagado = NAK, como hoy. Solo
  `retrobridge_host --images` la enciende (ADR-019).
- Una ATR solo la escribe una fuente a la vez (ADR-019). TNFS, `--image` y `*menu*` siguen protegidos.

## 1. Context & constraints

- Spec: §What result they get, §Behaviour, 10 criterios de aceptación, 6 puntos tipados.
- Hoy (main `c21cbdf`):
  - `SioCommandDispatcher` acepta `R`, `S`, `W`, `P`; el resto recibe NAK. `$21` y `$22` reciben NAK.
  - `DiskImage::writeSector` y `VirtualDrive::writeSector` existen; `validateWrite` = `validateRead`.
  - STATUS ya manda byte 2 = `$E0` (tiempo de formateo) y D7 = densidad media según la geometría.
  - `MenuCatalog` es una lista ordenada (máximo 999) que se llena al arrancar; `offer()` ya inserta en orden.
    `Shown::entry` guarda la posición en esa lista.
  - El editor de texto del 6502 edita cualquier fila: el host pone la dirección y el largo del campo en
    `kMenuEdit` (menu-editar-servidor). «Crear» no cambia `menu_program.inc`.
- Manual de Altirra: `$21` escribe 40 pistas, llena todo con `$00` y al terminar devuelve un búfer del tamaño de
  un sector con la lista de sectores malos (números de 16 bits) terminada en `$FFFF`. `$22` hace lo mismo en
  densidad media (1050). Se recomienda AUX1 = AUX2 = `$00`; el host no los mira.

## 2. Architecture

```text
 Atari --$21/$22--> SioSession --> SioCommandDispatcher --> FormatDisk (app)
                     ACK            accept: con allowWrites   VirtualDrive::format(density)
                     COMPLETE/ERROR execute: búfer FF FF..   DiskImage::format -> writeAt de ceros, sector a sector
                     + trama de 128

 Menú: N -> nombre (editor 6502) -> S/M -> unidad 2..8 -> CreateDiskImage (host) -> archivo nuevo
       -> MenuCatalog::insert -> corre las posiciones guardadas -> mountOn (como al elegir una imagen)
```

- **El formateo es una orden de lectura para SIO:** ACK, ejecución, COMPLETE o ERROR, y siempre una trama de
  datos del tamaño del sector (como `R`). `SioSession` ya maneja ese camino; no hay trama de datos del Atari.
- **Densidad:** `$21` vale solo si la ATR tiene 720 sectores de 128; `$22`, solo si tiene 1040 de 128. Otra
  geometría (incluida la doble densidad): ERROR, sin tocar el archivo (spec).
- **Búfer de respuesta:** `$FF $FF` en los dos primeros bytes (lista vacía) y `$FF` en el resto (M46-4, elección
  propia: el manual no dice qué va tras el terminador).
- **Crear:** el host escribe la cabecera ATR (16 bytes) y los sectores en cero con apertura exclusiva: si el
  archivo existe, no se toca. Después lo agrega a la lista y lo monta con el camino de siempre.

## 3. Interfaces & contracts

- `DiskImage::format(Density)` y `VirtualDrive::format(Density)` → `Result<void>`: `WriteProtected`,
  `NoDiskMounted`, `DensityMismatch` (código nuevo) o el error de E/S de `writeAt`. Escriben de a un sector con un
  búfer fijo de ceros (sin reservas).
- `rb::app::FormatDisk::execute(drive, density)` → `Result<void>`, junto a `WriteSector`.
- `sio_codes.hpp`: `kFormat = 0x21`, `kFormatMedium = 0x22`.
- Despachador: `accept` de `$21`/`$22` con `allowWrites` (sin él, NAK); unidad vacía: se acepta y responde ERROR
  con la trama. `execute` → COMPLETE o ERROR + 128 bytes. Banderas de STATUS como la escritura: D3 si está
  protegida, D2 si falla la E/S o la densidad.
- Host:
  - `SioWriteRecord` gana el comando `!`/`"` (`cmd=F` o `cmd=FM` en `sio_write`), con `crc32` omitido y la causa
    `density` nueva. `MenuSioTransport` lo reporta tras la respuesta final, como hoy.
  - `createAtr(folder, name, geometry)` en `host/src` (archivo nuevo): valida el nombre (M46-3: 1 a 25 de A-Z,
    0-9, `-`, `_`), abre `<NOMBRE>.ATR` con `"wbx"`, escribe cabecera y ceros. Errores: `NameInvalid`,
    `AlreadyExists`, `IoError`.
  - `MenuCatalog::insert(name)` → posición, o `kNoEntry` si la lista está llena (999).
  - `MenuController`: tecla `N` (`$23`), vista `Create` en tres pasos: nombre en la fila de mensajes
    (` NOMBRE: ` + campo de 25), densidad (` S SIMPLE  M MEDIA  ESC CANCELA`), unidad (` UNIDAD 2-8, ESC CANCELA`).
    Tras crear: corre en +1 toda posición guardada mayor o igual a la nueva (`shown_`, `previous_`, `incoming_`,
    `remountEntry_`, cursor de la lista) y llama a `mountOn`. Evento `menu_create name=… density=… result=…`.
  - Sin `--images`: `N` muestra ` SIN CARPETA LOCAL`.

## 4. Data model & flow

- ATR nueva: cabecera `$96 $02`, párrafos de 16 bytes, tamaño de sector 128, resto en cero; 92 160 bytes de datos
  (simple) o 133 120 (media). El archivo queda sin formatear; el DOS lo formatea (spec, suposición tomada).
- Formatear: 720 o 1040 `writeAt` de 128 bytes antes de COMPLETE. El tiempo del DOS para formatear es el del byte
  2 de STATUS (`$E0`, minutos).

## 5. Testing strategy

- Dominio: `format` simple y media dejan ceros; densidad distinta = `DensityMismatch` sin cambios; protegida =
  `WriteProtected`; fallo de `writeAt` a mitad = error y se informa.
- SIO: `$21`/`$22` con y sin `allowWrites`; búfer `FF FF` + `FF`; ERROR con trama; STATUS tras el fallo (D2/D3);
  0 reservas en la ruta (prueba de asignaciones existente).
- Host (`test_host_write` o `test_host_format` nuevo): AC1–AC4 con archivos reales y SHA; crear con nombre nuevo,
  repetido, vacío, de 26, con `.`, `\` y espacio; lista llena; posiciones corridas tras crear (una imagen montada
  sigue mostrando su nombre); `N` sin `--images`.
- Demo en Altirra (AC9): crear `NUEVO.ATR`, formatear con el DOS 2.5, guardar, relanzar, leer.

## 6. Sequencing & dependencies

F001 (dominio) → F002 (SIO); F003 (crear archivo y lista) en paralelo; F004 (menú y registro) tras F002 y F003;
F005 demo; F006 docs.

## 7. Risks & open decisions

- **`"wbx"` en ucrt:** C11 lo define; si la biblioteca de MSYS2 no lo acepta, F003 lo detecta en su prueba y usa
  la alternativa: probar existencia con `"rb"` y luego crear (con una ventana mínima entre las dos).
- **Posiciones de la lista:** un desfase deja una unidad con el nombre de otra imagen. Lo cubre una prueba con
  una imagen montada detrás de la nueva en orden alfabético.
- **El DOS 2.5 y `$22`:** el manual describe el 1050; se confirma en la demo que el DOS 2.5 manda `$22` para
  densidad media.
- **Corrección menor de paso:** en `docs/fuentes.md` la fila de menu-escritura-disco dice `D:` + retorno + `b-e0`;
  F006 la corrige a `D:\rb-e0`.
- **Decisión de plan (M46-4):** contenido del búfer tras `$FFFF`.
- **Decisión de plan (M46-5):** tecla `N`, densidad con `S`/`M`, unidad con `2`..`8`.

## Tasks

Rama `feat/menu-formatear-disco` (worktree `.worktrees/feat-menu-formatear-disco`, desde main `c21cbdf`). Riesgo
previsto por tarea (0/1/2); `rsc risk` decide el real.

| # | [P] | Tarea | Done-check | Depende de | Cubre | Riesgo |
|---|---|---|---|---|---|---|
| F001 | [P] | Dominio y app: `DensityMismatch`, `DiskImage::format`, `VirtualDrive::format`, `FormatDisk` | ctest `test_disk_image`, `test_virtual_drive` y uno de la app: casos de §5 en verde | — | AC1–AC4 | 1 |
| F002 |  | SIO: `$21`/`$22` en el despachador con `allowWrites`, búfer de respuesta, banderas de STATUS | ctest `test_sio_dispatcher`, `test_sio_session`, `test_sio_session_allocations`: casos de §5; sin `allowWrites` NAK igual que hoy | F001 | AC1–AC4 | 2 |
| F003 | [P] | Host: `createAtr` (nombre M46-3, `"wbx"`, cabecera y ceros) y `MenuCatalog::insert` | ctest nuevo `test_create_atr`: nombre nuevo, repetido, vacío, largo, no válido; archivo de 720 x 128 y 1040 x 128 con cabecera correcta; lista llena | — | AC5–AC8 | 1 |
| F004 |  | Host: tecla `N` y vista `Create`, posiciones corridas, montaje, `sio_write` con `F`/`FM`, `menu_create` | ctest `test_host_write`/`test_host_format`: AC1–AC8, «sin carpeta local», imagen montada que conserva su nombre; pruebas de menús anteriores en verde | F002, F003 | AC1–AC8, AC10 | 2 |
| F005 |  | Demo en Altirra: crear `NUEVO.ATR` en D2, formatear con DOS 2.5 (simple y media), guardar, relanzar, leer; densidad distinta da error | `D:\rb-e0\m46`: `host.jsonl` con `sio_write cmd=F`/`FM` y `menu_create`, SHA-256 antes/después, capturas; un solo `retrobridge_host` en el hub | F004 | AC1–AC3, AC5, AC9 | 0 |
| F006 | [P] | Docs: ADR-019 enmienda 1 (formateo), ADR-017 enmienda 9 (crear), README del host, `docs/fuentes.md` (manual de Altirra §10.2 y 1050; arreglo de `D:\rb-e0`) | archivos existen y enlazan; sin emoji; `python -I tools/check_notices.py` en verde | F004 | R: ADR-017, ADR-019 | 0 |
- Aprobado por el responsable («Apruebo») el 2026-11-20.
