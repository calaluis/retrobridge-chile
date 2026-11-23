---
type: progress
title: Progreso — menu-tnfs-persistente
description: Registro por tarea de menu-tnfs-persistente (T1–T3): qué se hizo, con qué pruebas y qué queda.
tags: [sdd, progress, menu, tnfs]
topic: sdd
slug: menu-tnfs-persistente
---

# Progreso — menu-tnfs-persistente

Plan: [`plans/menu-tnfs-persistente.md`](../plans/menu-tnfs-persistente.md). Rama `feat/menu-tnfs-persistente`.
Solo pruebas de host. Sin flasheo, sin COM3, sin banco físico (T8/T9 siguen sujetos a su autorización; M3a/H4 sin
aprobar).

## T1 — DriveMemory v3 (commit 9ee8f45)

- Ranura v3 de 3 488 B con origen, servidor y ruta por unidad; lee v1 y v2 como `Local`; solo escribe v3.
- Pruebas: `test_drive_memory`, `test_drive_slot_store` (migración v2→v3, ranura v3 dañada, retroceso R5),
  `test_drive_memory_link`.
- Mutación: quitar la comprobación «origen vacío con nombre» hace fallar su prueba.

## T2 — NetRestoreQueue (2026-11-23)

- `components/rb_menu/{include/rb/menu,src}/net_restore_queue.*`: pura, sin E/S; reloj monotónico en ms inyectado.
- Plazos: Wi-Fi 30 s desde `begin()` (solo mientras la red nunca estuvo `Ready`); 30 s desde `started()` hasta el
  primer byte; 30 s desde el último aumento de bytes. Sin plazo total. Una unidad en espera tras otra, o tras el
  usuario, no vence.
- Desviación del texto del plan: `started(drive, job, nowMs)` lleva el reloj, porque el plazo de primera respuesta
  cuenta desde ahí. `expired()` devuelve una máscara de bits y deja el trabajo en `jobOf()` para que el
  controlador lo cancele.
- Pruebas: `test_net_restore_queue`, 13 casos: progreso lento (un bloque cada 20 s durante 2 min, 0 % todo el
  rato), pausa de 31 s, servidor detenido tras el primer bloque, sin Wi-Fi 30 s, reloj que retrocede, `cancel`
  (resultado tardío sin dueño), `requeue` (plazos de nuevo).
- Mutación: medir actividad con `bytes >=` en vez de `bytes >` hace fallar 2 pruebas (sin primera respuesta,
  pausa de 31 s).

## T3 — NoExiste, NoCabe y bytes en curso (2026-11-23)

- R7 resuelto: *The TNFS Protocol* («Updated July 15, 2020», ya en `docs/fuentes.md`), tabla «List of Valid Return
  Codes»: ENOENT = 0x02, EACCES = 0x09. El documento no dice qué estado da OPEN o STAT ante un archivo ausente;
  por eso solo un 0x02 explícito a STAT u OPEN cuenta.
- `TnfsClient`: cada operación pública olvida el estado anterior; guarda el estado y la orden que lo recibió;
  `lastNotFound()` = ENOENT a STAT u OPEN.
- `LoaderError` añade `NoExiste` y `NoCabe` al final (los valores anteriores no cambian). `NoCabe`: sin memoria,
  sin hueco en el almacén, `.xex` cuyo disco no cabe. Un listado de carpeta ausente sigue `NoSePudoAbrir`.
- `LoaderStatus.bytes` en `InProgress`: bytes recibidos por el trabajo; solo crece, también al releer tras una
  reconexión.
- Pantalla al navegar: sin cambios (`NoExiste` y `NoCabe` muestran `ERROR: NO SE PUDO ABRIR`); `FALTA` y
  `NO CABE` son textos de la restauración (T5). El registro del host dice `missing` / `no room`.
- Pruebas: `test_tnfs_client` (ENOENT a STAT y OPEN, EACCES, EIO, ENOENT a READ, estado olvidado entre
  operaciones) y `test_tnfs_loader` (los mismos casos de extremo a extremo, sin memoria, sin hueco, bytes
  crecientes y tras reconexión).
- Mutación: aceptar ENOENT de cualquier orden hace fallar una prueba en cada suite.

## Suite de host (2026-11-23, árbol sin commit sobre 9ee8f45)

- `ctest --test-dir build/host`: **92/92** verdes (MSYS2 UCRT64, Ninja, `-DRB_SANITIZE=OFF`).
- No ejecutado: compilación ESP-IDF (la dirá la CI; riesgo R3 de memoria), ASan, pruebas físicas.

## Revisión de af5974c (PR #89, 2026-11-23)

- **`decode()`:** para v1/v2 se formaba un puntero a los campos de red, fuera del búfer (sin leerlo, pero es
  comportamiento indefinido). Ahora solo se forma para v3. Prueba: v1 y v2 decodificadas desde un bloque del
  montón de su tamaño exacto. Límite: sin lectura, ninguna prueba ni sanitizador ve el puntero; la prueba vigila
  que nunca llegue a leerse (ASan de la CI).
- **`next()`:** con la red `Ready` después de 30 s, `next()` ya no la da por vista; los dos órdenes de
  `next()`/`expired()` terminan en `SIN RED`. Prueba nueva; la mutación que quita la guarda la hace fallar.
- Host: **92/92**.

## T4 — sesión TNFS de restauración, `startFetchAt` y `startXexAt` (2026-11-23)

- Rama `feat/menu-tnfs-persistente-t4` desde `main` 51833f8 (PR #89 fusionado).
- `MenuLoader` añade `startFetchAt(server, path, length)` y `startXexAt(...)`. `TnfsLoader`: sesión `kRestore` con
  enlace propio, renombrado al servidor de cada trabajo; editar una fuente con `E` no la toca (AC9).
- Cableado: `main/menu_sio.cpp` y `host/src/main.cpp` crean el enlace de restauración. Inerte hasta T6: nada llama
  aún a `startFetchAt`, así que no se abre ningún socket nuevo.
- Spike: nota en el plan §2.4 (cabe; ~1,4 KB más en el loader; 8 sockets como máximo).
- Pruebas (`test_tnfs_loader`, 37): AC9 con dos servidores falsos (srv-b no recibe nada; la fuente 2 sigue a `E`
  sin usarse), sesión conservada para el mismo servidor y movida con un UMOUNT, `.xex` por servidor, servidor nulo,
  vacío, largo o que no resuelve y loader sin enlace → `NoEncontrado`, ENOENT → `NoExiste`.
- Mutaciones: mandar la restauración por la fuente 2 (3 fallos) y no renombrar el enlace (4 fallos).
- Host: **92/92**; `retrobridge_host` compila.

## T5 — el controlador recuerda el origen; `snapshot()` v3 (2026-11-23)

- Rama `feat/menu-tnfs-persistente-t5` desde `main` d016a5c (PR #90 y #91 fusionados).
- `Shown` guarda, para una imagen TNFS, el servidor que nombraba su fuente al elegirla, la ruta absoluta y si es
  `.xex`. El servidor se copia al empezar el trabajo (`jobServer_`), así que `E` después no lo cambia (AC9).
- `snapshot()` escribe `NetAtr`/`NetXex` con `setNet`; si lo rechaza, la unidad queda vacía, como un nombre local.
  Un `.xex` local (fuente 1) sigue sin recordarse.
- `restore()` no busca una unidad de red en el catálogo local (una imagen local con el mismo nombre es otra): la
  deja vacía y anota `D<n> net not restored`. Restaurarla por la red es T6. **Hasta T6**, el siguiente guardado
  la olvida; no se publica firmware entre T5 y T6.
- `Shown` crece ~290 B (servidor 33 + ruta 256); diez copias miembro ≈ 2,9 KB más en el controlador. Las copias
  en pila (`fetched`, `assignD1`) son de un solo `Shown` (R3).
- Pruebas: `test_menu_unidades_red` (7): ATR TNFS en D2 y en D1, `.xex` TNFS en D1 (`NetXex`), servidor fijado al
  elegir aunque `E` cambie la fuente 2, regresión local (`Local`, `.xex` local vacío, local reemplaza a TNFS),
  ida y vuelta por el codec v3, y `restore()` que no confunde una unidad de red con una local del mismo nombre.
- Mutaciones: sin la rama `NetAtr`/`NetXex` de `snapshot()` (5 fallos); `restore()` sin el filtro de red (1 fallo).
- Host: **93/93**; `retrobridge_host` compila.

## Siguiente

T6 tras revisar T5 (restauración de red en segundo plano con la cola), luego T7.
