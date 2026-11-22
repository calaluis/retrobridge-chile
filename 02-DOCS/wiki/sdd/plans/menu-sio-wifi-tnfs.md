---
type: plan
title: Plan — Wi-Fi y fuentes TNFS en la FujiNet (menu-sio E2)
description: CÓMO llevar el menú TNFS del PC al firmware menu-sio, solo con el SIO, sin tocar la NVS ni filtrar la clave.
tags: [sdd, plan, firmware, sio, menu, tnfs, wifi]
timestamp: 2026-11-22T00:30:00Z
topic: sdd
slug: menu-sio-wifi-tnfs
branch: docs/spec-menu-sio-wifi-tnfs
status: approved
---

# Plan — Wi-Fi y fuentes TNFS en la FujiNet (menu-sio E2)

> Spec: `02-DOCS/wiki/sdd/specs/menu-sio-wifi-tnfs.md` (aprobada, `50752bf`). Estado del plan: **aprobado** por el responsable el 2026-11-22, con T1 dividida en T1a y T1b.

## §0. Restricciones globales (literal, para cada tarea)

- E2 funciona con el USB **físicamente desconectado** y la FujiNet alimentada por el Atari. Pasar a USB es decisión
  del responsable.
- Presupuesto del pin 10 del SIO: **50 mA**. Si el consumo medido lo supera, **se detiene** el trabajo y se presentan
  opciones, todas solo por SIO.
- D1 sirve el menú en **< 1000 ms desde EN** (`listo_ms` del registro de arranques). Nada de Wi-Fi antes de eso.
- La clave del Wi-Fi vive solo en RAM. **Nunca** en el repositorio, la consola, `retrobridge-arranques.txt` ni otro
  registro. Pruebas con una clave centinela.
- **NVS conservada**: ni se borra ni se escribe. Igualdad antes del primer arranque del candidato y después del
  ensayo, registrando solo hash o resultado.
- Archivos en la raíz de la microSD: `retrobridge-wifi.txt` (red y clave) y `retrobridge-fuentes.txt` (formato de
  `--sources`, 7 servidores como máximo).
- Sin código de FujiNet ni de terceros. Flags: `-std=gnu++17 -fno-exceptions -fno-rtti -Wall -Wextra -Werror`.
- Cada commit, push, PR, flash, escritura o lectura por COM3 y salida de GPIO necesita aprobación del responsable.

## §1. Contexto que fija el diseño

- Spec, criterios 1–12; el criterio 1 manda el orden: **medir antes de construir**.
- El menú TNFS ya existe y está probado en el PC: `rb_net_tnfs` (cliente y códec, ya es componente ESP),
  `MenuController` y `MenuSources` (en `rb_menu`), y `TnfsLoader`, `TnfsLink`, `UdpTnfsPort` y `SystemNameResolver`
  **solo en `host/src/`**. El ESP32 no puede usar estos cuatro sin moverlos a un componente.
- `UdpTnfsPort` ya tiene una rama POSIX (sockets BSD, `getaddrinfo`), que es la que ofrece lwIP en ESP-IDF.
  `TnfsLoader` usa `std::thread`; en ESP-IDF eso es un pthread sobre FreeRTOS.
- `MenuStationParts` ya acepta `loader` y el controlador ya acepta `setSourcesFile`. Hoy `main/menu_sio.cpp` no pasa
  ninguno: por eso `E` dice `SIN ARCHIVO DE FUENTES`.
- La placa tiene PSRAM (PR #77 quitó su test de memoria del arranque). Las imágenes TNFS viven en
  `MemoryImageStore` (9 huecos en RAM).
- El firmware lee el +5 V del SIO con su ADC (`logSio5v`). Sirve como **indicio** de caída de tensión, no como medida
  de corriente.
- No hay tabla de particiones propia: el firmware usa la de ESP-IDF por defecto. La pila Wi-Fi añade varios cientos
  de KB de código.
- Otra rama en curso (`feat/menu-unidades-persistentes`) también toca `main/menu_sio.cpp` y escribe en la microSD.

## §2. Arquitectura

```text
                         ┌─────────────── ESP32 (menu-sio) ────────────────────────────────┐
 microSD ── retrobridge-wifi.txt ──▶ WifiConfig (RAM) ──▶ WifiStation ──(lwIP)──▶ router
         ── retrobridge-fuentes.txt ◀──▶ MenuSources ◀── MenuController (E) ◀── SIO task
                                                         │ key()/service()
                                         MenuLoader ◀────┘
                                  (TnfsLoader, hilo propio) ── TnfsLink ── UdpTnfsPort ──▶ servidor TNFS
                                         │
                                  MemoryImageStore (PSRAM) ──▶ D1..D8
                         └─────────────────────────────────────────────────────────────────┘
```

| Componente | Responsabilidad única | Dónde |
| --- | --- | --- |
| `WifiConfig` (nuevo) | Leer y validar `retrobridge-wifi.txt`; guardar red y clave en RAM; nunca imprimir la clave | `rb_menu` (portable, con pruebas de host) |
| `WifiStation` (nuevo) | Conectar en segundo plano como estación, reintentar, decir su estado; sin NVS | componente ESP nuevo `rb_esp_wifi` |
| `NetworkGate` (nuevo, interfaz) | Decirle al cargador si hay red, y si no, el motivo | `rb_menu`; la implementa `WifiStation` en ESP y una clase «siempre hay red» en el PC |
| `TnfsLoader`, `TnfsLink`, `UdpTnfsPort`, `SystemNameResolver` (se mueven) | Lo mismo que hoy, compartido por PC y ESP | componente nuevo `rb_menu_tnfs` |
| `MenuController` (cambia poco) | Motivos nuevos de error (`SIN WI-FI`, `SIN ARCHIVO WI-FI`, ...) | `rb_menu` |
| `menu_sio.cpp` (cambia) | En `finish()`: leer archivos, arrancar Wi-Fi, crear tienda y cargador, publicar fuentes y cargador a la tarea SIO | `main/` |

**Decisión que más pesa: el Wi-Fi arranca en `finish()`, después de servir D1 y de montar la microSD.** Así el plazo
de D1 (< 1 s) no depende de la radio, y la configuración viene de la tarjeta. La alternativa, arrancar el Wi-Fi en
`start()`, adelantaría la conexión ~1,5 s pero pondría el pico de corriente de la radio en el mismo instante en que
el Atari pide el primer sector: es el peor momento para una caída de tensión.

**Segunda decisión: mover el cargador a un componente compartido**, no copiarlo. Una copia para el ESP divergiría
del PC, y el PC es la referencia aprobada de comportamiento (spec, «Qué resultado obtiene»).

## §3. Interfaces y contratos

```text
WifiConfig
  read(FILE*) -> ok | motivo        línea 1 = red (1..32 bytes), línea 2 = clave (vacía o 8..63 bytes);
                                    solo se quitan CR/LF finales: los espacios cuentan; nada más tras la línea 2
  ssid() -> texto; hasKey() -> bool; key() solo la consume WifiStation
  wipe()                             pone la clave a cero en RAM tras entregarla
  ningún método formatea la clave; el motivo de error nunca la incluye

WifiStation (ESP)
  start(config) -> ok | motivo       no bloquea; conecta en segundo plano; reintenta con espera creciente (máx. 30 s)
  state() -> SinArchivo | Buscando | Conectado | Fallo(motivo)
  almacenamiento de la configuración: solo RAM (WIFI_STORAGE_RAM)

NetworkGate (interfaz en rb_menu)
  ready() -> nullopt | motivo        el cargador la consulta antes de cada trabajo TNFS

TnfsLoader (sin cambios de contrato; recibe un NetworkGate opcional)
  si gate y no ready(): el trabajo termina al momento con LoaderError::SinRed(motivo)

MenuController
  loaderText(SinRed) -> " SIN WI-FI: <motivo>" (texto exacto fijado en T5)
  E sin archivo de fuentes: crea el archivo al guardar (hoy ya lo reescribe entero)
```

## §4. Datos y flujo

**Arranque.** `start()` sirve D1 como hoy (sin cambios, < 1 s) → se monta la microSD → `finish()`:

1. Lee `retrobridge-wifi.txt` a `WifiConfig`. Falta o es inválido → `WifiStation` en `SinArchivo`/`Fallo` con motivo.
2. Lee `retrobridge-fuentes.txt` a `MenuSources` (falta → lista vacía, sin error).
3. Crea `MemoryImageStore` (PSRAM), los `TnfsLink`, el `TnfsLoader` con el `NetworkGate`.
4. Publica fuentes, ruta del archivo y cargador a la tarea SIO con la misma bandera atómica que el catálogo.
5. `WifiStation::start()`; `WifiConfig::wipe()`.

**Abrir una fuente.** Como en el PC. Si `NetworkGate` dice que no hay red, el trabajo termina al instante y la línea
de mensajes dice el motivo.

**Guardar con `E`.** Como en el PC: reescribe `retrobridge-fuentes.txt` entero. Lo escribe la tarea SIO, como hoy en
el PC; si en la otra rama `rb_drive_save` también escribe en la tarjeta, son archivos distintos y FatFs los serializa.

**NVS.** Con `CONFIG_ESP_WIFI_NVS_ENABLED=n` y `CONFIG_ESP_PHY_CALIBRATION_AND_DATA_STORAGE=n` la pila Wi-Fi no lee ni
escribe NVS (la calibración de la radio se hace completa en cada arranque). El firmware no llama a `nvs_flash_init`
ni a `nvs_flash_erase`.

## §5. Estrategia de pruebas (por criterio)

| Criterio | Nivel | Qué afirma | Qué se finge |
| --- | --- | --- | --- |
| C1 eléctrico | Banco (T1a, luego T1b) | T1a: demanda de la FujiNet **sin conexión eléctrica al Atari**, en reposo (E1 actual), búsqueda, conexión y transferencia, ≤ 50 mA. T1b, solo si T1a cumple: lo mismo alimentada por el SIO del 65XE. Método y cifras en `02-DOCS/raw/` | Nada |
| C2 arranque | Banco | 10 encendidos solo SIO: menú, `listo_ms` < 1000, `brownout` = 0 | Nada |
| C3 navegar | Host + banco | Listado de carpetas en el 65XE; en host, `test_menu_tnfs` y `test_menu_navegacion` siguen verdes tras mover el cargador | Servidor TNFS falso en host |
| C4 arrancar | Banco | ATR del servidor en D1 arranca con `A` en el 65XE | Nada |
| C5 D2–D8 | Host + Altirra | Sectores de una imagen TNFS en D2 | Servidor falso |
| C6 editar | Host + banco | `E` reescribe `retrobridge-fuentes.txt`, lo crea si falta; tras apagar sigue | Carpeta temporal |
| C7 sin red | Host | Sin archivo, red inexistente o clave mala → motivo; fuente 1 operativa | `NetworkGate` falso |
| C8 servidor caído | Host | Motivo en el plazo del menú TNFS del PC; pantalla principal operativa | Servidor que no responde |
| C9 clave | Host + banco | La clave centinela no aparece en la consola, el registro de arranques ni el repo; `WifiConfig` no la formatea | Captura de salida |
| C10 NVS | Banco | Hash SHA-256 de la partición NVS antes del primer arranque del candidato = hash tras el ensayo; solo se registran los hashes | Nada |
| C11 imagen grande | Host | Imagen mayor que el límite → aviso, ninguna unidad cambia | Servidor falso |
| C12 regresión | Host + CI + Altirra | Todas las pruebas de host y la regresión NetSIO en verde | — |

**Lectura de la NVS (C10).** Un script lee la partición por COM3 con `esptool read_flash` a un archivo temporal fuera
del repositorio, calcula su SHA-256, borra el archivo y escribe solo el hash. Desplazamiento y tamaño de la
partición se toman de la tabla de particiones del candidato. Cada lectura por COM3 pide aprobación.

**Medida eléctrica (C1), en dos pasos.**

- **T1a, sin el Atari.** La FujiNet no se conecta eléctricamente al 65XE. Se alimenta a 5 V desde una fuente externa
  con el amperímetro en serie (fuente de laboratorio o medidor USB en línea). Se mide cada fase del firmware de banco.
  Si alguna fase supera 50 mA, T1b no se hace: parada y opciones al responsable.
- **T1b, con el 65XE.** Solo si T1a cumple. Cable SIO de paso con el pin 10 abierto y el amperímetro en serie, USB
  desconectado; mismas fases.

En los dos pasos, la media por fase sale del multímetro. Si hay osciloscopio, una resistencia de 1 Ω en serie da los picos. Sin osciloscopio, los picos se
registran como **no medidos** y el ADC propio (`logSio5v`) solo aporta la caída del +5 V como indicio.

## §6. Secuencia

```text
T1a (sin Atari, gate) ──▶ STOP si > 50 mA ──▶ T1b (SIO del 65XE, gate) ──▶ STOP si > 50 mA: opciones
   │ ok
   ├─▶ T2 WifiConfig ─┐
   ├─▶ T3 mover cargador ─┼─▶ T5 motivos en el menú ─▶ T6 pegamento ESP ─▶ T7 CI y Altirra ─▶ T8 banco 65XE
   └─▶ T4 WifiStation ─┘
```

T2, T3 y T4 son independientes entre sí y pueden ir en paralelo. T1 usa un firmware de banco mínimo que no se
publica.

## §7. Riesgos

| # | Riesgo | Disparador | Impacto | Mitigación |
| --- | --- | --- | --- | --- |
| R1 | **El consumo supera 50 mA** (probable: un ESP32 con radio activa recibe a ~100 mA y transmite con picos de cientos de mA) | T1 | Detiene E2 | T1 mide también el reposo de E1 actual; si **ya** supera 50 mA sin Wi-Fi, se informa antes de seguir. Opciones preparadas, todas solo SIO: CPU a 80 MHz; ahorro de módem; potencia de transmisión reducida; Wi-Fi solo mientras se usa una fuente 2–8 (cambia «segundo plano» de la spec: decisión del responsable); condensador de reserva (cambio de hardware) |
| R2 | ESP-IDF escribe en NVS (calibración, configuración Wi-Fi) | Opciones por defecto | Rompe C10 | Opciones de §4 en `sdkconfig.menu-sio`; una comprobación del empaquetador falla si están activas o si el binario enlaza `nvs_flash_erase`/`nvs_set_*` |
| R3 | La clave sale por un volcado | Pánico con volcado de núcleo a flash, o registros de ESP-IDF | Rompe C9 | Volcado de núcleo desactivado en el paquete; nivel de registro de `wifi` en WARN; prueba con clave centinela en la captura completa |
| R4 | No cabe en la partición de la aplicación | La pila Wi-Fi + lwIP | No compila | Tabla de particiones propia sin tocar la NVS (mismo desplazamiento y tamaño); se comprueba en T6 |
| R5 | La radio retrasa la tarea SIO | Interrupciones Wi-Fi en el mismo núcleo | Tramas perdidas | Wi-Fi en el núcleo 0, tarea SIO fija en el otro núcleo; contador de tramas cortas del registro de arranques en T8 |
| R6 | Imágenes grandes agotan la PSRAM | ATR de muchos MB | Fallo de reserva | Límite por imagen y total en `MemoryImageStore`, criterio 11 |
| R7 | Conflicto con `feat/menu-unidades-persistentes` | Ambas tocan `menu_sio.cpp` | Fusión manual | La segunda en llegar a `main` se rebasa; T6 empieza tras ver cuál va primero |

## §8. Tareas

| T | Qué | Criterios | Riesgo (tier) | Comprobación de hecho | Depende de | Paralelo |
| --- | --- | --- | --- | --- | --- | --- |
| T1a | **Demanda sin el Atari.** Firmware de banco (no publicado) que, por fases separadas y anunciadas en la consola, queda en reposo, busca red, conecta y descarga por TNFS la ATR de ensayo, con red y clave leídas de la microSD; medir la demanda de cada fase con alimentación externa, sin conexión eléctrica al 65XE. Incluye el reposo del firmware E1 actual | C1 | 2 | `02-DOCS/raw/<fecha>-consumo-e2-sin-atari.md` con método, foto, media (y pico si hay osciloscopio) por fase y veredicto ≤ 50 mA; si no, opciones presentadas y parada | Instrumentos confirmados | No |
| T1b | **Demanda con el SIO del 65XE.** Las mismas fases con la FujiNet alimentada por el pin 10 y el USB desconectado | C1 | 2 | `02-DOCS/raw/<fecha>-consumo-pin10-e2.md` con las mismas cifras y veredicto ≤ 50 mA | T1a ≤ 50 mA | No |
| T2 | `WifiConfig` + pruebas de host (formato, espacios, límites, archivo ausente, clave centinela nunca en motivos ni salidas) | C7, C9 | 1 | ctest verde con `test_wifi_config`; un mutante que imprime la clave es detectado | T1b ok | Sí |
| T3 | Mover `TnfsLoader`, `TnfsLink`, `UdpTnfsPort`, `SystemNameResolver` a `rb_menu_tnfs`; `NetworkGate` opcional; el PC usa «siempre hay red» | C3, C5, C8, C11, C12 | 1 | ctest verde con todas las pruebas TNFS de host sin cambios de comportamiento; `rb_menu_tnfs` compila como componente ESP en CI | T1b ok | Sí |
| T4 | `rb_esp_wifi::WifiStation`: estación en segundo plano, reintentos, estado, sin NVS; opciones de `sdkconfig.menu-sio` | C7, C9, C10 | 2 | CI verde con el paquete `menu-sio`; comprobación del empaquetador falla con `CONFIG_ESP_WIFI_NVS_ENABLED=y` (test rojo primero) | T1b ok | Sí |
| T5 | Motivos de red en `MenuController` (`SIN WI-FI: …`) y `E` crea el archivo si falta | C6, C7, C8 | 1 | ctest verde con pruebas nuevas de motivos y de creación del archivo; pruebas de menú existentes verdes | T3 | No |
| T6 | Pegamento en `main/menu_sio.cpp` (`finish()`), tabla de particiones propia, publicación atómica de fuentes y cargador | C2–C7, C11 | 2 | CI verde; tamaño de la aplicación dentro de su partición; NVS en el mismo desplazamiento y tamaño que hoy | T2, T4, T5 | No |
| T7 | Regresión: host, CI, Altirra con servidor TNFS | C12 | 1 | CI verde; registro de Altirra con navegación y arranque | T6 | No |
| T8 | **Banco 65XE** (aprobación de flash y COM3): hash NVS antes, flash, 10 encendidos solo SIO, navegar, `A`, captura de consola, hash NVS después | C2, C4, C9, C10 + criterio final | 2 | `02-DOCS/raw/<fecha>-banco-e2.md` con registros de arranque, fotos, hashes iguales y búsqueda de la centinela en la captura con 0 apariciones | T7 | No |

**Orden:** T1a → T1b → (T2 ∥ T3 ∥ T4) → T5 → T6 → T7 → T8. Cada tarea pasa su revisión según `rsc risk` antes de la
siguiente. Ninguna tarea después de T1b empieza si T1a y T1b no dan ≤ 50 mA o si el responsable no ha decidido una opción.

## Ensayo de transferencia

- Servidor: `tnfs.calaluis.cl:16384`, alcanzable desde la red del banco (comprobado el 2026-11-22 desde el PC: monta,
  lista carpetas y sirve la ATR entera).
- ATR de ensayo: `/Aportes_Eduardo_Antonio (Chalo)/Demoscene/NumenPAL.atr`, 196 496 bytes, cabecera ATR válida,
  sectores de 256 bytes, SHA-256 `7a47b8e0ecb6e53d…`. El firmware de banco compara el SHA-256 de lo descargado.

## Preguntas para el responsable

- **¿Qué instrumentos hay en el banco?** Para T1a: fuente de 5 V o medidor USB en línea, y multímetro con escala de
  mA. Para T1b: cable SIO de paso que se pueda abrir en el pin 10. Con osciloscopio y una resistencia de 1 Ω se miden
  los picos; sin él, quedan como «no medidos». **La medición queda pendiente hasta confirmarlo.**
