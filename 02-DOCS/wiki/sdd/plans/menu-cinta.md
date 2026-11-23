---
type: plan
title: Plan — Elegir una cinta .cas desde el menú del 65XE y cargarla
description: CÓMO se construye la spec menu-cinta: listar .cas, montarlos en C:, recordarlos, arrancar la casetera dentro de menu-sio y traspasar DATA IN entre disco y cinta, inerte sin M3a/H4.
tags: [sdd, plan, menu, cassette, cas, sio, data-in]
timestamp: 2026-11-23T01:30:00Z
topic: sdd
slug: menu-cinta
spec: 02-DOCS/wiki/sdd/specs/menu-cinta.md
branch: feat/menu-cinta
status: draft
---

# Plan — Elegir una cinta .cas desde el menú del 65XE y cargarla

## 0. Global constraints (literales, para cada tarea)

- C++17 con `-fno-exceptions -fno-rtti -Werror` (ADR-004). Sin reservas en la ruta SIO (ADR-011).
- Código propio (ADR-012 y su enmienda 1). Sin código de FujiNet ni de terceros. La reproducción FSK de
  `cassette-cas` no cambia.
- `check_layering` en verde (ADR-002): la lógica nueva (catálogo, controlador, memoria, árbitro) vive en
  `rb_menu`/host y se prueba en el PC; lo que toca ESP-IDF vive en `main/` y `rb_esp_*`.
- **Puertas:** sin M3a aprobada, nada lee MOTOR (GPIO36); sin H4 aprobada, nada escribe en DATA IN como casetera.
  Este cambio no edita `docs/mediciones/puertas.json`. Las comprobaciones del paquete sobre puertas siguen verdes.
- **D1 primero:** el arranque temprano de D1 no empeora (hoy `listo_ms` ≈ 230 ms, < 1 s desde EN). La casetera
  arranca después de que D1 está servido.
- **NVS:** nunca se borra ni se escribe. `C:` se guarda en la microSD, en el mismo par de archivos que D1–D8.
- **Avisos:** en mayúsculas, ASCII, ≤ 40 columnas. Textos exactos en §3.
- Un solo `retrobridge_host` por hub NetSIO. El SIO va desconectado al flashear.

## 1. Context & constraints

- Spec: 12 criterios; AC1–AC8 se prueban ya, AC9–AC12 tras M3a y H4. Seis suposiciones tomadas, una decisión
  diferida (TNFS) y un área no formulable (DATA IN compartida), que este plan precisa en §2.3.
- Hoy (rama base `b3bd374`):
  - `MenuCatalog` filtra con `hasAtrExtension`/`hasXexExtension` (`menu_catalog.cpp`).
  - `MenuController` tiene las vistas Main, List, Pick, Unmount, Folder, FolderPick, Edit, Create; la línea
    `C: SIN CINTA` es fija (`kMainTape`, ~línea 877). Tiene un mecanismo de trabajos (`startJob`/`advance()`) que
    usa TNFS y que no bloquea la ruta SIO.
  - `DriveMemory` guarda 8 nombres; códec «RBMU» versión 1, 12 + 8 × `kNameField` + 4 bytes, CRC-32, dos ranuras
    (`retrobridge-unidades-a.dat`/`-b.dat`) con secuencia.
  - `CassetteService("/sd/", motor, dataIn)`: `start()` reserva el banco PSRAM y dos tablas una vez, crea
    `cas_load` (prioridad 4) y `cas_loop` (prioridad 5); `request("cas ...")` pasa por su cola.
  - `EspDataIn` instala UART2 con TX en DATA IN (GPIO21) y comparte el pin con RMT; solo toca hardware vía
    `configure()`, que exige H4 y M3a; `release()` deja la línea en marca y borra el driver.
  - `EspSioUart` instala UART2 (19200, TX GPIO21, RX GPIO33, COMMAND GPIO39) con `begin()`/`end()`.
  - En `app_main.cpp`, bajo `CONFIG_RB_MENU_SIO`, la casetera no arranca y se sale con `return` antes de
    `g_motor.configure`.
- **Conflicto central:** los dos dueños instalan el mismo UART2 sobre el mismo GPIO21. Nunca pueden estar
  instalados a la vez.

## 2. Architecture

```text
             Atari (SIO)                          microSD /sd/
                 │                                     │
   COMMAND ──────┼──────────┐                          │
   MOTOR(36)*────┼───────┐  │                          │
   DATA IN(21) ◄─┼──┐    │  │                          │
                 │  │    ▼  ▼                          │
                 │  │  DataInArbiter (rb_menu, puro) ◄─┤ gates
                 │  │    │ owner = Disk | Tape          │
                 │  │    ├──► DiskLine  (EspSioUart begin/end)
                 │  │    └──► TapeLine  (EspDataIn configure/release)
                 │  │                                  │
   sioTask ──────┴──┴─► MenuController ──► TapeDeck ───► CassetteService (cas_load, cas_loop)
                         │   ▲                              (lee /sd/*.cas al banco PSRAM)
                         ▼   │
                     DriveMemory v2 (D1–D8 + C:) ──► retrobridge-unidades-a/b.dat
   * MOTOR solo con M3a; DATA IN como casetera solo con H4.
```

### 2.1 Componentes

| Componente | Capa | Responsabilidad única |
|---|---|---|
| `MenuCatalog` | rb_menu | Lista también `.cas` y marca cada entrada como disco, XEX o cinta. |
| `MenuController` | rb_menu | Acepta `C` como destino solo para cintas, rechaza los cruces, muestra y desmonta `C:`. |
| `TapeDeck` (puerto) | rb_menu | Pide montar o sacar una cinta sin bloquear; informa Pendiente/Lista/No válida/Sin memoria. |
| `DriveMemory` v2 | rb_menu | Recuerda D1–D8 y `C:`; lee también ranuras v1. |
| `DataInArbiter` | rb_menu | Decide quién tiene DATA IN (disco o cinta) a partir de MOTOR, COMMAND, encendido y fin de cinta. |
| `EspTapeDeck` | main | Adaptador de `TapeDeck` sobre `CassetteService::request`. |
| `menu_sio.cpp` | main | Arranca la casetera tras servir D1, enchufa el árbitro al bucle SIO, restaura `C:` al arrancar. |

### 2.2 La decisión que más pesa: quién arbitra DATA IN

**Decisión:** un árbitro único, en el bucle SIO (`sioTask`), con lógica pura probada en el PC. El disco es el
dueño por defecto. La casetera nunca instala UART2 por su cuenta: pide la línea y espera a que el árbitro se la dé.

- **Por qué:** el SIO es lo que tiene plazo duro (D1 < 1 s, respuestas a tiempo). Si el bucle SIO decide, el
  traspaso ocurre siempre entre dos tramas, nunca en medio de una respuesta. Una cerradura compartida entre dos
  tareas daría el mismo resultado con más riesgo de interbloqueo y sin pruebas de host.
- **Alternativa descartada:** dos UART (UART1 para la cinta) con el mismo GPIO21. La matriz GPIO permite un solo
  TX por pin; seguiría haciendo falta conmutar, y `cassette-cas` ya está validado sobre UART2.

### 2.3 Reglas del traspaso (precisa el área no formulable de la spec)

1. **Disco → cinta:** se decide por **estado**, no por flanco: en cada paso, si MOTOR está activo **y** hay cinta
   lista en `C:` **y** las dos puertas están aprobadas → tras la trama en curso, `DiskLine.end()`, luego
   `TapeLine.take()`. La casetera reproduce desde el principio.
   - **MOTOR activo antes de que la cinta quede lista** (la cinta recordada se restaura en segundo plano al
     encender con START, y el usuario pulsa la tecla antes): el árbitro cede DATA IN en el paso en que la cinta
     pasa a `Ready`, sin esperar otro flanco de MOTOR. Revisión del PR #85, 2026-11-23.
2. **La cinta conserva la línea mientras MOTOR se apaga y enciende** (pausas entre bloques, `cassette-cas`
   «reanudar con MOTOR»). Soltarla en cada pausa haría saltar el UART sin necesidad.
3. **Cinta → disco** cuando ocurre lo primero de:
   - el Atari baja COMMAND (GPIO39, solo entrada, legible sin UART) con MOTOR apagado: quiere el disco;
   - la cinta llega al final y MOTOR se apaga;
   - el 65XE se apaga (`SioPowerWatch::PoweredOff`);
   - `cas eject` o `cas rewind` por consola.
   Secuencia: `TapeLine.release()` (línea en marca, driver borrado), luego `DiskLine.begin()`.
4. **Sin puertas:** el árbitro nunca deja de dar la línea al disco. MOTOR ni se lee. La consola dice por qué
   (`cas: sin M3a/H4, la cinta no se reproduce`).
5. **Fallo en el traspaso:** si `take()` falla, la línea vuelve al disco con `begin()`; si `begin()` falla, se
   reintenta cada 100 ms y se registra; D1 nunca queda sin UART en silencio.

Caso «cinta que luego lee disco» (p. ej. un juego que carga niveles desde D1): regla 3, primer guion. El Atari
apaga MOTOR antes de hablar por SIO; al bajar COMMAND el disco recupera la línea. Se pierde, como mucho, la primera
trama: el OS la reintenta.

## 3. Interfaces & contracts

```text
MenuCatalog
  Entry.kind : Disk | Xex | Tape        // Tape ⇔ extensión .cas (mayúsculas o minúsculas)
  orden y letras: como hoy, sin distinguir tipo

TapeDeck                                 // puerto; todas las llamadas vuelven sin esperar
  mount(name)  -> Accepted | Busy
  eject()      -> void
  poll()       -> Idle | Pending | Ready | Invalid | NoMemory
  gatesOpen()  -> bool                   // M3a y H4 aprobadas en el paquete

MenuController (vista Pick con una entrada elegida)
  tecla 'C', entrada Tape        -> startJob(tape mount) ; al Ready: C: = nombre, guardar memoria
  tecla 'C', entrada Disk/Xex    -> aviso DISCO_EN_C ; nada cambia
  tecla 1..8, entrada Tape       -> aviso CINTA_EN_DISCO ; nada cambia
  vista Unmount, tecla 'C'       -> deck.eject() ; C: vacía ; guardar memoria
  línea principal C:             -> "C: " + nombre recortado a 40 columnas | "C: SIN CINTA"

DriveMemory v2
  tape : nombre (0..kMaxName) ; vacío = sin cinta
  encode -> siempre versión 2 (12 + 9 × kNameField + 4 bytes)
  decode -> acepta v1 (tape vacía) y v2 ; cualquier otro tamaño o versión: no válida (como hoy)

DataInArbiter.step(motorOn, commandLow, tapeReady, tapeEnded, poweredOff, gates) -> Keep | ToTape | ToDisk
  - puro, sin reloj propio; quien llama ejecuta end/take o release/begin
  - con gates == false devuelve siempre Keep con owner == Disk
```

**Textos exactos de la línea de mensajes** (≤ 40 columnas, cierran la suposición tomada de la spec):

| Caso | Texto |
|---|---|
| Leyendo la cinta al elegirla | `LEYENDO CINTA...` |
| Cinta montada, puertas aprobadas | `CINTA EN C:, ENCIENDE CON START` |
| Cinta montada, sin puertas (AC8) | `CINTA EN C:, AUN NO PUEDE CARGARSE` |
| Número 1–8 con una cinta (AC3) | `UNA CINTA VA EN C:` |
| `C` con un disco o XEX (AC3) | `UN DISCO NO VA EN C:` |
| Cinta no válida (AC6) | `CINTA NO VALIDA` |
| Sin memoria (AC6) | `SIN MEMORIA PARA LA CINTA` |
| Casetera ocupada | `CASETERA OCUPADA, REPITE EN UN RATO` |
| Paquete sin casetera (T2) | `CASETERA NO DISPONIBLE` |
| Desmontar (vista U) | `DESMONTAR: PULSA 1-8 O C, ESC CANCELA` |
| Desmontada (AC4) | línea `C: SIN CINTA` |

## 4. Data model & flow

- **Elegir:** List → letra → Pick (`UNIDAD PARA: <NOMBRE>`) → `C` → `TapeDeck.mount(name)` →
  `CassetteService` lee el `.cas` al banco PSRAM en `cas_load` → `advance()` ve `Ready` → `C:` = nombre →
  `DriveMemoryLink` guarda la ranura nueva (misma cola y tarea `rb_drive_save` que D1–D8).
- **Recordar:** al arrancar, `finish()` carga la memoria; si trae `C:`, pide `mount(name)` en segundo plano después
  de servir D1. Si el archivo ya no está o no es válido, `C:` queda vacía y la consola lo dice.
- **Cargar (con puertas):** encender con START(+OPTION) → el OS pita → tecla → MOTOR activo → árbitro `ToTape`
  → la casetera reproduce desde el bloque 0 → fin / COMMAND / apagado → `ToDisk`.
- **Memoria PSRAM:** el banco de la casetera se reserva una vez al arrancar el servicio. `menu-sio` ya pide
  `MemoryImageStore` en PSRAM. T6 mide la PSRAM libre con ambos; si no caben, la casetera no arranca, la consola
  lo dice y `mount` responde `NoMemory` (AC6, segundo caso).
- **Datos en la microSD:** las ranuras crecen de 12 + 8 × `kNameField` + 4 a 12 + 9 × `kNameField` + 4 bytes.
  Un firmware anterior lee una ranura v2 como no válida y arranca sin unidades recordadas (riesgo R4).

## 5. Testing strategy

| AC | Nivel | Qué afirma | Qué se finge |
|---|---|---|---|
| AC1 | unidad (`test_menu_catalog`) | `.cas`/`.CAS` aparecen con letra, en orden, con `kind == Tape` | carpeta en disco temporal |
| AC2 | unidad (`test_menu_cinta`, nuevo) | `C` con cinta → `C: INTERNATIONAL_KARATE.CAS`; D1–D8 iguales; nombre largo recortado a 40 | `FakeTapeDeck` que responde `Ready` |
| AC3 | unidad | `1`..`8` con cinta → `UNA CINTA VA EN C:`, unidades iguales; `C` con `.ATR`/`.XEX` → `UN DISCO NO VA EN C:` | — |
| AC4 | unidad | Unmount + `C` → `C: SIN CINTA`, `eject()` llamado, memoria guardada | `FakeTapeDeck` |
| AC5 | unidad (`test_drive_memory`) | ida y vuelta v2 con cinta; v1 se lee con cinta vacía; CRC mala rechazada; tamaño raro rechazado | bytes en memoria |
| AC5 | integración (`test_menu_unidades`) | guardar con `C:`, «apagar» (nuevo controlador), restaurar → `C:` igual | archivos temporales |
| AC6 | unidad | `Invalid` → `CINTA NO VALIDA`; `NoMemory` → `SIN MEMORIA PARA LA CINTA`; `C:` sin cambio | `FakeTapeDeck` |
| AC7 | CI + banco | paquete `menu-sio` compila con la casetera; `listo_ms` del registro de arranque ≤ valor de hoy + 50 ms; discos, XEX y TNFS como antes | — |
| AC8 | unidad + CI + banco | `gatesOpen() == false` → `CINTA EN C:, AUN NO PUEDE CARGARSE`; árbitro siempre `Keep`/Disk; las comprobaciones de puertas del paquete siguen verdes | `FakeTapeDeck`, árbitro con `gates=false` |
| AC9–AC12 | unidad del árbitro ahora; banco tras M3a/H4 | tabla de transiciones de §2.3 completa (MOTOR on/off, pausa, COMMAND, fin, apagado, fallo de `take`/`begin`), incluido **MOTOR ya activo cuando la cinta pasa a `Ready` → `ToTape` en ese paso** | líneas falsas que registran el orden de `end/take/release/begin` |

Prueba RED obligatoria del árbitro: el orden `end` antes de `take` y `release` antes de `begin` (dos UART2
instalados a la vez es el fallo que más cuesta en el equipo).

## 6. Sequencing & dependencies

| # | Tarea | AC | Riesgo | Depende | Paralela | Done-check |
|---|---|---|---|---|---|---|
| T1 | `.cas` en el catálogo, `Entry.kind` | AC1 | 0 | — | sí | `ctest -R menu_catalog` verde con casos `.cas`/`.CAS` |
| T2 | Puerto `TapeDeck` + `FakeTapeDeck`; `C` como destino, cruces rechazados, línea `C:`, desmontar | AC2–AC4, AC6, AC8 | 1 | T1 | no | `ctest -R menu_cinta` verde, textos de §3 literales |
| T3 | `DriveMemory` v2 con `C:`; lectura v1 | AC5 | 1 | — | sí | `ctest -R "drive_memory\|menu_unidades"` verde; ranura v1 de prueba se lee |
| T4 | `DataInArbiter` puro + tabla de transiciones | AC8, AC9–AC12 (lógica) | 1 | — | sí | `ctest -R data_in_arbiter` verde; prueba RED del orden vista fallar antes; caso «MOTOR activo antes de `Ready`» cubierto |
| T5 | `EspTapeDeck` sobre `CassetteService`; `gatesOpen()` desde `validate_for_cassette_output` y M3a | AC6, AC8 | 1 | T2 | no | compila en CI (ESP-IDF build) |
| T6 | `menu_sio`: arrancar la casetera tras D1, medir PSRAM libre, restaurar `C:` al arrancar | AC5, AC7 | 2 | T3, T5 | no | paquete `menu-sio` verde en CI; consola muestra PSRAM libre y `cas: listo` |
| T7 | Enchufar el árbitro al bucle SIO (inerte sin puertas): MOTOR solo con M3a, COMMAND por GPIO39 | AC7, AC8 | 2 | T4, T6 | no | CI verde; comprobaciones de puertas del paquete verdes; sin puertas, ningún `uart_driver_delete` del SIO en el registro |
| T8 | Banco AC1–AC8 en el 65XE; registro en `02-DOCS/raw/` | AC1–AC8 | — | T1–T7 | no | registro con SHA, ZIP, NVS sin cambios y cada AC visto en pantalla |
| T9 | *(tras M3a y H4, PR aparte del responsable)* banco AC9–AC12 | AC9–AC12 | 2 | T8 + puertas | no | registro con las tres cintas de `cassette-cas` cargadas desde el menú |

Orden: T1, T3 y T4 pueden ir en paralelo; T2 tras T1; T5 tras T2; T6 tras T3 y T5; T7 al final del código.
Cada tarea de riesgo 2 pide flasheo y banco con aprobación.

## 7. Risks & open decisions

| # | Riesgo | Disparador | Impacto | Mitigación |
|---|---|---|---|---|
| R1 | Dos UART2 instalados a la vez | traspaso fuera de orden o desde dos tareas | SIO muerto o DATA IN en conflicto | Un solo árbitro en `sioTask`; prueba RED del orden; sin puertas el traspaso no existe (T7) |
| R2 | La casetera retrasa D1 | `cas_loop` (prioridad 5) o `start()` compiten con `rb_sio_menu` al arrancar | AC7 falla | Arrancar el servicio en `finish()`, tras servir D1; comparar `listo_ms` con el valor de hoy (T6) |
| R3 | No cabe el banco PSRAM junto a `MemoryImageStore` | PSRAM fragmentada | casetera sin arrancar | Medir en T6; si falta, la casetera no arranca y `mount` da `NoMemory`; decisión del tamaño en un PR aparte |
| R4 | Volver a un firmware anterior borra la memoria de unidades | ranura v2 leída por código v1 | D1–D8 vacías tras bajar de versión | Aceptado y documentado en el registro de T8; no se pierde ningún archivo |
| R5 | La cinta tarda en leerse y el usuario pulsa la tecla antes | `.cas` grande, `mount` en segundo plano al arrancar | la carga no empieza | El árbitro decide por estado: MOTOR activo + `Ready` cede la línea aunque MOTOR subiera antes (§2.3 regla 1, prueba en T4); el OS espera el tono guía. Se mide el tiempo de lectura en T8 |
| R6 | El juego lee disco tras la cinta y pierde una trama | regla 3 de §2.3 | un reintento del OS | El OS reintenta; se verifica en T9 con un caso real si existe |
| R7 | AC9–AC12 dependen de puertas no aprobadas | M3a/H4 en `false` | el objetivo final queda pendiente | T9 fuera de este PR; T4 y T7 dejan la lógica probada e inerte |

**Decisiones abiertas:** ninguna bloquea T1–T8. El tamaño del banco PSRAM solo se revisa si R3 se dispara.

**Aislamiento:** el código va en la rama `feat/menu-cinta`, en su propio worktree, creada desde `main` al empezar
T1 (se pregunta antes, como siempre).
