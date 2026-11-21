---
type: plan
title: Plan — Menú de RetroBridge por SIO físico (E1 microSD, E2 TNFS)
description: CÓMO llevar el menú de retrobridge_host al firmware y servirlo como D1 por el SIO físico de la FujiNet 1.3.
tags: [sdd, plan, firmware, sio, menu]
timestamp: 2026-11-21T21:00:00Z
topic: sdd
slug: menu-sio-fisico
branch: feat/menu-sio-fisico
status: draft
---

# Plan — menu-sio-fisico

Spec: `02-DOCS/wiki/sdd/specs/menu-sio-fisico.md` (aprobada 2026-11-21). Este plan detalla **E1**; E2 se
planifica al cerrar E1.

## 0. Restricciones globales (literales)

- SIO a **19200 baudios**, 8N1. UART2. COMMAND = **GPIO39** (entrada), DATA OUT del Atari = **GPIO33**
  (RX), DATA IN al Atari = **GPIO21** (TX). Botón A = **GPIO0** (solo entrada). +5 V del SIO = **GPIO35**
  (ADC, solo lectura).
- Protocolo del menú sin cambios (ADR-017): dispositivo `$5A`, órdenes `K` y `T`, disco del menú 720×128,
  17 sectores de programa, respuesta `K` de 962 bytes.
- `SioSession`: `kPollTimeoutUs` = 20000, `kCompleteDelayUs` = 300 (sin cambios).
- DATA IN liberada (UART en reposo, línea en marca) en reposo, tras cada respuesta y tras cualquier error.
- Sin reservas dinámicas en la ruta SIO (ADR-011). Sin código de FujiNet ni de terceros (ADR-012).
- La transmisión por SIO físico solo existe en el paquete `menu-sio`; las demás variantes la rechazan.
- H4 y M3a siguen sin aprobar. Ningún archivo los marca aprobados.

## 1. Contexto

- El menú vive en `host/src` y ya es casi portable: lo específico de Windows está bajo `#ifdef _WIN32`
  (`menu_catalog.cpp`, `menu_sources.cpp`), y el resto usa `stdio`, `dirent` y `std::mutex`, que ESP-IDF
  ofrece.
- La composición del host (`host/src/main.cpp:419-488`) arma `MenuImage` + `HostImageRepository` +
  `DriveBay{8}` + `InlineDriveMailbox` + `MenuController` + `MenuSioTransport` + `SioCommandDispatcher`
  + `SioSession`, llama `onColdReset()` + `service()` al arrancar y `service()` tras cada paso.
- El firmware ya tiene la microSD (`rb_esp_storage`), la consola (`rb_console`) y el driver de UART2 en
  GPIO21 de la casetera (`rb_esp_cassette`, C8 superada).
- No hay transporte SIO físico ni aviso de reinicio en frío: el botón A y la orden `menu` hacen de
  `COLD_RESET` (spec §Comportamiento de D1).

## 2. Arquitectura

```text
            ┌──────────────── rb_menu (C++17 puro, host + ESP-IDF) ────────────────┐
 consola ──►│ MenuStation: MenuImage · ImageRepository · DriveBay · mailbox ·      │
 botón A ──►│   MenuController · MenuSioTransport(inner) · dispatcher · SioSession │
            └───────────────────────────────▲──────────────────────────────────────┘
                                            │ SioTransport (inner)
             host: NetSioSioTransport ──────┤
             firmware: SioLineTransport ────┘ (rb_esp_sio) ── SioUartPort ── UART2 / GPIO39
```

- **`rb_menu`** (componente nuevo): los archivos del menú que hoy están en `host/src` (`menu_*`,
  `create_atr`, `tnfs_folder`, `sha256`, `xex_image`, `memory_image_store.hpp`,
  `host_image_repository.hpp`, `menu_program.inc`, `xex_loader.inc`) se **mueven** con `git mv`, sin
  cambiar su código ni su espacio de nombres (`rb::host`) en este paso. El host los enlaza desde ahí.
- **`MenuStation`** (nuevo, en `rb_menu`): la composición de `main.cpp:419-488` como clase, con el
  transporte interno inyectado. La usan el host y el firmware; así el firmware no duplica el cableado.
- **`rb_esp_sio`** (componente nuevo): `SioLineTransport`, lógica pura del `SioTransport` sobre un puerto
  `SioUartPort` (leer COMMAND, leer bytes con plazo, escribir y esperar fin, vaciar RX). `EspSioUart`
  implementa ese puerto con UART2 y GPIO39 (probado en host con `fake_idf`, como `EspDataIn`).
- **`main`**: con `CONFIG_RB_MENU_SIO`, arranca la microSD, lee el catálogo de su raíz, crea la
  `MenuStation` con `SioLineTransport` y una tarea que hace `step()` + `service()`. Órdenes de consola
  `menu` y `sio status`; el botón A se sondea en la misma tarea.

**Decisión principal:** mover el menú tal cual a un componente común en lugar de reescribirlo para el
ESP32. *Por qué:* el objetivo 4 de la spec (mismo código) y el criterio 7 (las pruebas del host pasan
igual). Reescribirlo duplicaría 2700 líneas y sus 15 pruebas. *Coste:* `MenuCatalog` ocupa ~129 KB
(999 × 129 B); en el firmware va en PSRAM (§7 R2).

## 3. Interfaces y contratos

```text
SioUartPort (rb_esp_sio, puro)
  commandAsserted() -> bool                  // COMMAND en bajo
  read(buffer, n, timeoutUs) -> size_t       // bytes recibidos antes del plazo
  write(bytes) -> bool                       // encola y espera fin de transmisión
  flushInput()                               // descarta lo recibido
  idleLine()                                 // TX en marca (DATA IN liberada)
  nowUs() -> uint64_t

SioLineTransport : rb::app::SioTransport
  waitCommandFrame(timeoutUs): espera COMMAND bajo (o plazo -> Timeout); lee 5 bytes con
    plazo; COMMAND debe seguir bajo al recibirlos; error -> flushInput + idleLine
  send(bytes): write + idleLine; registra latencia COMMAND->primer byte
  receiveAfterAck(frame): read(n+1) con plazo
  releaseDataLine(): idleLine (siempre, también tras error)
  stats(): órdenes, errores, última y peor latencia (sin reservas)

MenuStation (rb_menu)
  MenuStation(inner SioTransport&, clock, catalog, sources, repository roots, events, loader?)
  start()       // onColdReset + service: D1 = menú
  step()        // session.step() + controller.service()
  returnToMenu()// onColdReset: D1 vuelve al menú antes de la siguiente orden
  d1() -> const char*  // "menu" o nombre de la imagen
```

## 4. Datos y flujo

- **Encendido de la FujiNet:** `start()` monta el menú en D1. El 65XE, al encender, lee D1 → menú.
- **A en el menú:** `MenuController::boot()` monta la ATR en D1 (como en Altirra).
- **Botón A / `menu`:** `returnToMenu()` → `onColdReset()`; el siguiente `service()` vuelve a montar el
  menú en D1. La consola muestra `D1: menu`.
- **Imágenes:** la raíz de la microSD (`/sdcard`) hace de `--images`. Las fuentes TNFS quedan para E2.

## 5. Estrategia de pruebas

| Criterio | Nivel | Qué afirma | Qué simula |
|---|---|---|---|
| 1, 2, 3 | integración host | `MenuStation` con un transporte simulado arranca el menú desde D1 (sectores 1–17), responde `K` y arranca una ATR con A | transporte en memoria, carpeta temporal |
| 4 | integración host | tras A, `returnToMenu()` devuelve el menú a D1 antes de la siguiente orden | idem |
| 5 | CI empaquetador | `menu-sio` contiene las llamadas UART/GPIO del transporte; las demás variantes lo rechazan | — |
| 6 | unidad host | `SioLineTransport` deja DATA IN liberada tras respuesta, NAK, plazo, trama mala, COMMAND que sube antes de tiempo | `SioUartPort` simulado |
| 6 | unidad host | `EspSioUart` configura UART2 en 19200 con GPIO21/33 y lee GPIO39 | `fake_idf` |
| 7 | regresión host | las pruebas actuales del menú pasan sin cambios desde `rb_menu` | — |
| 1–4 | físico | 65XE real (fuera de CI, con aceptación escrita) | — |

## 6. Secuencia

1. Mover el menú a `rb_menu` (host sigue igual).
2. `MenuStation` y el host usándola.
3. `SioLineTransport` + `SioUartPort` con pruebas.
4. `EspSioUart` con `fake_idf`.
5. `main` con `CONFIG_RB_MENU_SIO`, órdenes `menu`/`sio status`, botón A, GPIO35.
6. Variante `menu-sio` del empaquetador y CI.
7. Registro de aceptación de riesgo y procedimiento de la demo.

## 7. Riesgos

| # | Riesgo | Disparador | Mitigación |
|---|---|---|---|
| R1 | Latencia física fuera de la ventana del SO | el menú no carga o hay reintentos | la consola registra latencia; tarea SIO con prioridad alta y núcleo fijo |
| R2 | `MenuCatalog` (~129 KB) no cabe en DRAM | fallo de enlace o de arranque | instancia en PSRAM (`heap_caps_malloc` una vez al arrancar, fuera de la ruta SIO) |
| R3 | GPIO0 es pin de arranque | pulsar A durante un reinicio | solo se lee tras el arranque; documentado en el procedimiento |
| R4 | DATA OUT sin pull-up | bytes falsos en reposo | pull-up interno en GPIO33; la consola registra el reposo al arrancar |
| R5 | Corriente del pin 10 | Atari con +5 V mayor que el USB | se registra la tensión (GPIO35); la corriente queda como riesgo aceptado |
| R6 | Mover archivos rompe el host | fallo de compilación o de pruebas | paso 1 aislado; toda la suite del host antes de seguir |

## Tareas (E1)

| ID | Tarea | Criterio | Riesgo | Hecho cuando | Depende | Paralelo |
|---|---|---|---|---|---|---|
| T1 | Mover el menú a `components/rb_menu` | 7 | 1 | `ctest` del host pasa completo | — | no |
| T2 | `MenuStation` + host la usa | 1–4, 7 | 1 | prueba de integración nueva + suite completa | T1 | no |
| T3 | `SioLineTransport` + `SioUartPort` | 6 | 1 | pruebas de unidad de liberación y plazos | — | sí (con T1) |
| T4 | `EspSioUart` sobre `fake_idf` | 6 | 1 | prueba de configuración UART2/GPIO | T3 | no |
| T5 | `main` `CONFIG_RB_MENU_SIO`, `menu`, botón A, GPIO35 | 1–4 | 2 | compila en CI; banco USB | T2, T4 | no |
| T6 | Variante `menu-sio` del empaquetador | 5 | 1 | pruebas Python del empaquetador | T5 | no |
| T7 | Procedimiento de demo y registro de aceptación | 1–4 | 0 | documento revisado por el responsable | T6 | sí |

## Avance (2026-11-21)

| ID | Estado | Evidencia |
|---|---|---|
| T1 | hecho | `ctest` 77/77 tras mover; `retrobridge_host` compila; `make_menu.py --check` y `make_xex_loader.py --check` al día; `check_layering` OK; 243 pruebas Python OK |
| T2 | hecho | `test_menu_station` 5/5 (menú en D1 tras `start()`, A arranca la ATR, reencendido repite la ATR, `returnToMenu()` devuelve el menú y descarta tramas intermedias, escrituras rechazadas por defecto); host arranca y cierra limpio sin hub |
| T3 | hecho | `test_sio_line_transport` 12/12; dos mutantes (sin `idleLine` en la recuperación, sin vaciado de entrada) fallan |
| T4 | hecho | `test_esp_sio_uart` 9/9 sobre `fake_idf`; mutante «vaciar tras la caída» falla en `test_a_late_fall_still_keeps_the_first_bytes`; `test_esp_data_in` sigue pasando |
| T5 | hecho (compila en CI) | `a42ec84`; regresión en Altirra con el `retrobridge_host` de la rama: menú → 1, A, 1, A → «DEMO DOS OK» → Shift+F5 → menú (`D:b-e0\m50-sio-fisico`, host.jsonl sin reintentos ni NAK). Primera compilación en IDF v6.1 falló: `REQUIRES` no puede depender de `CONFIG_*` (ESP-IDF expande requisitos antes de leer sdkconfig); corregido dejando los componentes siempre requeridos y solo `menu_sio.cpp` condicional |
| T6 | hecho | variante `menu-sio` del empaquetador: llamadas exactas de `esp_sio_uart.cpp` y `menu_sio.cpp`, UART2 solo en DATA IN/DATA OUT, COMMAND solo entrada, botón A solo GPIO0 entrada; las demás variantes excluyen esas dos fuentes pero rechazan `CONFIG_RB_MENU_SIO` y sus literales/símbolos. 264 pruebas Python OK; 4 mutantes muertos. Job de CI `firmware-package-menu-sio` (PR o manual). En CI (`e65c2bd`, ejecución 44941501900) el paquete se generó y verificó; en local, zip `b1b7455d…a902` y `verify` OK. Fusionado en PR #74 (`b535dcf`) |
| T7 | preparado; demo pendiente | procedimiento `docs/hardware/procedimiento-demo-menu-sio.md` (adaptador sin pin 8, instalación, banco por USB, demo en el 65XE hasta `DEMO DOS OK`, restauración de `f405106`) y registro `docs/mediciones/menu-sio-fisico-registro.md` con la aceptación sin firmar y todos los resultados PENDIENTE |

Decisión tomada en T4: COMMAND se sondea cada tick (1 ms) y la entrada se vacía **antes** de esperar la caída,
no después; así una caída vista tarde no pierde bytes de la trama (el UART los guarda). Si la latencia
registrada no basta, el paso siguiente es una interrupción en GPIO39 (R1).
