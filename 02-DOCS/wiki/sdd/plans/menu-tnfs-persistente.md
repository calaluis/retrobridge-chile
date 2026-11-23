---
type: plan
title: Plan — Recordar las unidades TNFS tras apagar el 65XE
description: CÓMO se construye la spec menu-tnfs-persistente: memoria de unidades v3 con origen de red, restauración de red en segundo plano por unidad, plazos y cancelación sin respuestas tardías.
tags: [sdd, plan, menu, tnfs, persistencia, drive-memory]
timestamp: 2026-11-23T08:00:00Z
topic: sdd
slug: menu-tnfs-persistente
spec: 02-DOCS/wiki/sdd/specs/menu-tnfs-persistente.md
branch: plan/menu-tnfs-persistente
status: draft
---

# Plan — Recordar las unidades TNFS tras apagar el 65XE

> Spec aprobada (PR #86, `c221fd4`). Este plan dice **cómo**. No autoriza flashear, abrir COM3 ni activar hardware.
> Etiquetas: [C] leído en el código de `main` `c221fd4`; [I] inferencia; [P] pendiente de comprobar (spike o banco).

## 0. Global constraints (literales, para cada tarea)

- **D1 sirve el menú en el plazo de hoy:** `listo_ms` del registro de arranques no sube más de **50 ms** (spec AC8).
  Nada de red, DNS ni Wi-Fi ocurre antes de `D1 ready`.
- **Plazos:** **30 s** para que el Wi-Fi quede `Ready`, contados desde el arranque; **30 s** para la primera
  respuesta del servidor; **30 s** sin datos durante una descarga. **Sin plazo total** de descarga (una ATR grande
  tarda ~65 s, plan `menu-tnfs`). «Sin datos» se mide con **bytes recibidos** (contador del loader) y un reloj
  **monotónico** del controlador, nunca con el porcentaje entero: el porcentaje puede quedarse igual con datos llegando.
- **Servidor recordado:** el nombre del servidor tal como estaba en la fuente al montar (`MenuSources::kMaxName` =
  32 caracteres) y la ruta absoluta (`kTnfsMaxPath` = 255). El puerto es el de hoy (16384, fijo); no se guarda.
  Editar una fuente con `E` **no** cambia el servidor de una unidad recordada (AC9).
- **Formato:** la memoria de unidades pasa a **v3**. v1 y v2 se siguen leyendo; solo se escribe v3. Nunca se borra
  ni se reescribe en bloque la NVS; la memoria vive en la microSD (`retrobridge-unidades-a.dat` / `-b.dat`) [C].
- **Textos (40 columnas):** `D2: CARGANDO <nombre>`, `D2: SIN RED`, `D2: FALTA <nombre>`, `D2: INVALIDA <motivo>`,
  `D2: NO CABE`, `D2: SERVIDOR NO ENCONTRADO`, `D2: NO SE PUDO ABRIR`. `A` con D1 en carga: `D1 CARGANDO, ESPERA`. Ninguno muestra la clave Wi-Fi ni el servidor completo
  si no cabe (se recorta por la derecha).
- **Sin asignaciones en el hilo SIO** (ADR-011). `DriveMemory` nunca va en la pila de la tarea SIO.
- **Sin código de FujiNet ni de terceros** (ADR-012). `main` cerrada: rama y PR propios.
- **Banco:** flashear y abrir COM3 necesitan aprobación por separado. El montaje USB + SIO a la vez necesita una
  **autorización específica** para este banco: la excepción aceptada en T8 de `menu-cinta` valía solo para ese banco
  y no se extiende a este PR. El banco **solo por SIO** de AC8 está
  **bloqueado** hasta T1a/T1b (`menu-sio-wifi-tnfs`) o una autorización escrita del responsable.

## 1. Context & constraints

- Spec §Qué debe respetar: `menu-unidades-persistentes` (D1 < 1 s, `GUARDANDO`/`GUARDADO`, un corte pierde como mucho
  el último cambio), `menu-sio-wifi-tnfs` (Wi-Fi en segundo plano, sin escrituras en la NVS), `menu-cinta` (v2 con
  `C:`).
- [C] Hoy `MenuController::snapshot()` guarda solo `Shown::Kind::Local`; una unidad TNFS se recuerda vacía
  (`menu_controller.hpp`, comentario «TNFS images are remembered empty»).
- [C] `DriveMemory` guarda 9 nombres de hasta 127 caracteres (`ImageLocation::kMaxLength`); `SlotFileStore` escribe
  dos ranuras de tamaño fijo en la microSD.
- [C] `MenuLoader` hace un trabajo a la vez («a job in progress is cancelled») y direcciona por **número de fuente**,
  no por servidor. `changeServer()` cambia el servidor de una fuente.
- [C] Durante `restoring_` toda tecla responde «espera» y `A` no hace nada. Hoy eso dura lo que tarda la microSD.
- [C] `LoaderError::NoSePudoAbrir` mezcla «el servidor se negó», «sin memoria», «sin ranura» y «ruta larga». Debajo,
  `TnfsResult::NotOpened` mezcla «ruta inexistente», «no es un archivo», «otro estado de error» y «el destino se
  negó» (`tnfs_client.hpp`); el código TNFS de la respuesta queda en `TnfsClient::lastStatus()`.
- [C] El loader informa solo `percent` (entero, 0..99) mientras descarga (`tnfs_loader.cpp`, `percentOf`).

## 2. Architecture

```text
 microSD ──SlotFileStore──> DriveMemory v3 ──restore()──> MenuController
                                                    │  locales: como hoy (restoring_, bloqueante, < 1 s)
                                                    └─ de red: NetRestoreQueue (por unidad: Pendiente → Cargando
                                                       → Montada | Fallida(motivo)), fuera de restoring_
 MenuController.service() ──> NetRestoreQueue.next() ──> MenuLoader.startFetchAt(server, path, xex)
                                                              │ (hilo del loader, sesión TNFS propia)
                                                              └─> LoaderStatus ─> place() o fallo por unidad
```

### 2.1 Componentes

| Componente | Responsabilidad única | Nuevo / cambia |
|---|---|---|
| `DriveMemory` | Qué guarda cada unidad: vacía, local (nombre), red ATR o red XEX (servidor + ruta + nombre visible) | Cambia |
| `drive_memory_codec` | Ranura v3; lee v1/v2/v3 | Cambia |
| `NetRestoreQueue` | Estado de restauración de red por unidad, plazos y orden; sin E/S | Nuevo (`rb_menu`) |
| `MenuController` | Recuerda el origen de red al montar; reparte local/red al restaurar; muestra estados; cancela | Cambia |
| `MenuLoader` / `TnfsLoader` | `startFetchAt` y `startXexAt` por servidor, con sesión propia; errores separados | Cambia |
| `menu_sio.cpp` | Sin cambios de cableado salvo el reloj de plazos | Cambia poco |

### 2.2 La decisión que más pesa: restauración de red fuera de `restoring_`

Hoy `restoring_` congela el menú hasta terminar. Meter la red ahí rompería AC3 (D1 y `A` operativos aunque D2 cargue)
y el plazo de `listo_ms`. **Decisión:** `restoring_` cubre solo lo local, como hoy. Al terminarlo (`finishRestore()`),
las unidades de red pasan a una cola en segundo plano que `service()` avanza. Alternativa descartada: una tarea
nueva que restaure en paralelo; duplicaría la lógica de `place()` y competiría con el loader, que hace un trabajo a
la vez.

### 2.3 Un loader, un trabajo: prioridad al usuario

El loader hace un solo trabajo. Regla: **el trabajo del usuario gana**. Si el usuario abre una carpeta TNFS o monta
una imagen mientras una restauración descarga, esa restauración se cancela y vuelve a la cola como `Pendiente`
(salvo que el usuario haya cambiado esa unidad: entonces se descarta, AC6). La cola reanuda cuando el loader queda
libre. [I] Coste: la descarga interrumpida empieza de cero; aceptable frente a bloquear al usuario.

### 2.4 Direccionar por servidor (AC9)

`startFetchAt(server, path)` usa una **sesión TNFS interna de restauración**, separada de las fuentes 2..8, con el
servidor recordado. Así, editar la fuente 2 con `E` no afecta a una restauración. [P] Spike en T4: confirmar que
`TnfsLoader` admite una sesión más sin superar su memoria (una sesión extra = un enlace UDP y un MOUNT).

## 3. Interfaces & contracts

```text
DriveMemory (v3)
  origin(drive) -> Empty | Local | NetAtr | NetXex
  name(drive)   -> nombre local (Local) o nombre visible (Net*)
  server(drive) -> "" | nombre del servidor (Net*), <= 32
  path(drive)   -> "" | ruta absoluta (Net*), <= 255
  setLocal(drive, name) / setNet(drive, kind, server, path, display) -> bool (false y sin cambios si no cabe)
  tape()/setTape() como en v2

drive_memory_codec
  encode -> siempre v3; decode -> acepta v1, v2 y v3 del tamaño exacto; v1/v2 leen todo como Local

MenuLoader (añade)
  startFetchAt(server, path, length) -> job     // ATR por servidor recordado
  startXexAt(server, path, length)   -> job     // .xex por servidor recordado
  LoaderError añade:
    NoExiste         solo si la respuesta TNFS a OPEN o STAT trae el estado ENOENT
    NoCabe           el destino se negó (sin memoria, sin ranura, demasiado grande)
  NoEncontrado ya existe: el nombre del servidor no resuelve (DNS)
  NoSePudoAbrir queda para lo ambiguo: otro estado TNFS (permisos, no es archivo...), ruta larga
  LoaderStatus.bytes, en InProgress: bytes recibidos hasta ahora (crece con cada bloque)

NetRestoreQueue (puro, sin E/S, reloj inyectado)
  begin(memory, nowMs)            // toma las unidades Net*
  next(networkReady, nowMs) -> unidad a pedir | ninguna
  started(drive, job) / progressed(drive, bytes, nowMs) / done(drive) / failed(drive, motivo)
                                  // progressed: si bytes crece, nowMs pasa a ser el último dato
  cancel(drive)                   // el usuario cambió la unidad: se descarta, también su resultado tardío
  requeue(drive)                  // un trabajo del usuario la interrumpió
  state(drive) -> Pendiente | Cargando | Montada | Fallida(motivo)
  expired(nowMs) -> unidades cuyo plazo venció (Wi-Fi 30 s, primera respuesta 30 s, 30 s desde el último dato)
  el reloj es monotónico y lo inyecta el llamador (esp_timer en el FujiNet, falso en host)
```

Reglas del resultado por motivo:

| Resultado | Pantalla | ¿Sigue recordada? |
|---|---|---|
| Imagen llega y vale | Nombre, como cualquier unidad | Sí |
| Wi-Fi no `Ready` en 30 s, `NoResponde`, 30 s sin datos | `SIN RED` | Sí (reintenta al encender) |
| `NoEncontrado` (DNS) | `SERVIDOR NO ENCONTRADO` | Sí (reintenta al encender) |
| `NoExiste` (estado ENOENT confirmado) | `FALTA <nombre>` | No: se guarda una versión nueva sin ella (AC5) |
| `NoSePudoAbrir` (estado ambiguo: permisos, no es archivo, otro error) | `NO SE PUDO ABRIR` | Sí: un error ambiguo nunca borra la asignación |
| Imagen o `.xex` inválidos | `INVALIDA <motivo>` | No (AC5) |
| `NoCabe` | `NO CABE` | Sí (decisión de la revisión: puede caber en otro encendido) |

Contrato de cancelación (AC6): cuando el usuario monta otra imagen en una unidad `Cargando` o la desmonta, el
controlador cancela el trabajo, marca la unidad `cancel` y guarda el cambio del usuario. Un `LoaderStatus` que
llegue después para ese trabajo se descarta y su ranura se devuelve con `recycle()`. Nunca toca `shown_` ni la
memoria.

## 4. Data model & flow

- **Ranura v3:** cabecera (12) + 8 × (origen 1 + nombre 128 + servidor 33 + ruta 256) + cinta 128 + CRC (4) =
  **3 488 B**. Dos ranuras ≈ 7 KB en la microSD. [I] `DriveMemory` crece de ~1.1 KB a ~3.5 KB: sigue siendo
  miembro (nunca en pila); `snapshot()` ya copia en un objeto miembro.
- **Montar desde TNFS:** al aplicar el montaje, `shown_` guarda además servidor y ruta; `snapshot()` los escribe como
  `NetAtr`/`NetXex`.
- **Unidad fallida con `SIN RED`:** en la sesión queda vacía, pero el controlador conserva su registro recordado, así
  que `snapshot()` la sigue escribiendo hasta que el usuario la cambie.
- **Encender:** `restore()` monta lo local como hoy → `finishRestore()` → `NetRestoreQueue.begin()` → `service()` pide
  una unidad a la vez cuando el loader está libre y la red `Ready`.
- **D1 de red:** D1 sigue sirviendo el menú; su imagen de red se asigna al llegar. `A` antes de eso responde
  `D1 CARGANDO, ESPERA`.

## 5. Testing strategy

| AC | Nivel | Qué afirma | Qué se finge |
|---|---|---|---|
| AC1 | Host (`menu_tnfs_rig`) | Encender con D2 `NetAtr` → `CARGANDO` → nombre; el Atari lee D2 | `FakeTnfsServer`, reloj |
| AC2 | Host | D1 `NetXex` → al llegar, `A` arranca el `.xex` | Servidor falso |
| AC3 | Host | D1 local lista sin esperar la red; `A` funciona con D2 `Cargando` | Loader lento |
| AC4 | Host | Wi-Fi no `Ready` 30 s / servidor mudo 30 s → `SIN RED`; DNS falla → `SERVIDOR NO ENCONTRADO`; las dos siguen recordadas | `NetworkGate` falso, reloj |
| AC4 | Host | Inactividad: (a) progreso lento, un bloque cada 20 s durante 2 min, sin que cambie el porcentaje → **no** vence; (b) pausa de 31 s a mitad → `SIN RED`; (c) servidor detenido tras el primer bloque → `SIN RED` a los 30 s | Servidor falso con ritmo controlado, reloj |
| AC5 | Host | Estado ENOENT → `FALTA`, versión nueva sin la unidad; `.xex` inválido → `INVALIDA`. Con EACCES, otro estado, respuesta mala, sin red o sin memoria la unidad **sigue recordada** | Servidor falso con cada estado |
| — | Host | `NO CABE` y recuperación: primer encendido sin memoria → `NO CABE`, sigue recordada; encendido siguiente con memoria libre → montada | `MemoryImageStore` con límite |
| AC6 | Host, **determinista** | El usuario cambia o desmonta D2 en `Cargando`; luego el loader entrega la imagen; D2 y la memoria no cambian; la ranura se recicla | Loader falso controlado paso a paso |
| AC7 | Unidad (`drive_memory`) | Ranuras v1 y v2 de prueba se leen sin pérdida; v3 ida y vuelta | — |
| AC7 | Integración (`SlotFileStore` real en un directorio temporal) | Ranura A v2 (1 168 B) y B v3 (3 488 B), de tamaños distintos: gana la de mayor secuencia; CRC malo y escritura cortada (archivo truncado) se rechazan y gana la otra; el primer guardado v3 tras leer v2 escribe la ranura que no tiene la más nueva y la v2 sigue válida; D1–D8 y `C:` se conservan en cada paso | Archivos en disco del PC |
| AC8 | Banco con USB (autorización específica, §0) | `listo_ms` ≤ hoy + 50 ms con unidades de red recordadas | — |
| AC9 | Host | D2 recordada de `srv-a`; `E` cambia la fuente 2 a `srv-b`; al encender se pide a `srv-a` | Dos servidores falsos |

`NetRestoreQueue` lleva además pruebas unitarias propias (plazos, `requeue`, `cancel`). Los textos de §0 se comprueban
literales en la pantalla renderizada.

## 6. Sequencing & dependencies

| # | Tarea | AC | Riesgo | Depende | Paralela | Done-check |
|---|---|---|---|---|---|---|
| T1 | `DriveMemory` v3 + codec; lectura v1/v2; integración con `SlotFileStore` | AC7 | 1 | — | sí | `ctest -R "drive_memory\|drive_slot_store"` verde con los casos de integración de §5 |
| T2 | `NetRestoreQueue` (pura) con plazos por bytes y reloj monotónico, `cancel`, `requeue` | AC4, AC6 | 1 | — | sí | `ctest -R net_restore` verde con progreso lento, pausa de 31 s y servidor detenido |
| T3 | `NoExiste` solo por estado ENOENT; `NoCabe`; `bytes` en `InProgress` | AC4, AC5 | 1 | — | sí | `ctest -R "tnfs_client\|tnfs_loader"` verde con ENOENT, EACCES, otro estado, sin memoria y `bytes` creciente |
| T4 | Spike + `startFetchAt`/`startXexAt` con sesión interna | AC9 | 2 | T3 | no | `ctest -R tnfs_loader` verde; nota del spike en el plan |
| T5 | Controlador: recordar el origen al montar; `snapshot()` v3 | AC1, AC9 | 1 | T1 | no | `ctest -R menu_unidades` verde |
| T6 | Controlador: restauración de red en segundo plano, textos, `A` con D1 en carga | AC1–AC5 | 2 | T2, T4, T5 | no | `ctest -R "menu_tnfs\|menu_unidades"` verde |
| T7 | Cancelación y respuesta tardía; prioridad del usuario | AC6 | 2 | T6 | no | prueba determinista de AC6 verde |
| T8 | Paquete `menu-sio` + banco con USB (autorización específica del montaje USB + SIO, más flasheo y COM3) | AC1–AC5, AC8 (USB), AC9 | 2 | T7 | no | registro en `02-DOCS/raw/` con consola y fotos |
| T9 | Banco solo por SIO | AC8 | — | T8, T1a/T1b | no | **bloqueado** hasta T1a/T1b o autorización escrita |

## 7. Risks & open decisions

| # | Riesgo | Disparo | Impacto | Mitigación |
|---|---|---|---|---|
| R1 | La sesión TNFS extra no cabe en el loader | T4 | AC9 sin servidor propio | Spike en T4; plan B: reutilizar la sesión de una fuente cuyo servidor coincida y, si ninguna coincide, abrir la interna bajo demanda y cerrarla al terminar |
| R2 | Una restauración larga compite con el usuario | Usuario navega TNFS al encender | Descargas repetidas | Prioridad al usuario + `requeue` (§2.3); medir en T8 |
| R3 | `DriveMemory` v3 (~3.5 KB) presiona pilas | Una copia en pila | Desbordamiento | Siempre miembro; revisar `snapshot()` y el guardado; prueba ASan |
| R4 | Los plazos del cliente TNFS no coinciden con 30 s | T6 | `SIN RED` antes o después de lo pactado | `NetRestoreQueue` mide sus 30 s con su reloj y cancela; no depende de los reintentos del cliente |
| R5 | Una v3 escrita no se puede leer con un firmware anterior | Volver a un paquete viejo | Unidades vacías | Aceptado: el firmware viejo ve la ranura como no válida y arranca vacío (no rompe) |
| R6 | `NoCabe` recordada para siempre | Imagen demasiado grande | `NO CABE` en cada encendido | Aceptado en la revisión: se mantiene; el usuario la cambia. Prueba de recuperación en §5 |
| R7 | El valor del estado ENOENT de TNFS | T3 | `FALTA` en el caso equivocado borraría una asignación | [P] Confirmar el código en la documentación pública del protocolo TNFS (no en código de FujiNet); prueba con el servidor falso. Ante cualquier duda, `NoSePudoAbrir`, que no borra |

Decisiones abiertas: ninguna. Revisión del PR #87 (2026-11-23): `NO CABE` sigue recordada, con prueba de
recuperación; un fallo de DNS se muestra como `SERVIDOR NO ENCONTRADO`, distinto de `SIN RED`, y la unidad sigue
recordada; solo ENOENT confirmado borra una asignación.

No se consultó código de FujiNet (ADR-012).
