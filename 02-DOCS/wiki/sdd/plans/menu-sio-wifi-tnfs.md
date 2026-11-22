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

> Spec: `02-DOCS/wiki/sdd/specs/menu-sio-wifi-tnfs.md` (aprobada, `50752bf`). Estado del plan: **aprobado** por el responsable el 2026-11-22, con T1 dividida en T1a y T1b. Actualización del
> 2026-11-22 (ATR y XEX desde TNFS, banco T8 con un `.XEX` real): **aprobada** por el responsable el 2026-11-22.

## §0. Restricciones globales (literal, para cada tarea)

- E2 funciona con el USB **físicamente desconectado** y la FujiNet alimentada por el Atari. Pasar a USB es decisión
  del responsable.
- Presupuesto del pin 10 del SIO: **50 mA**, para la media y para el pico de cada fase, medidos y registrados por
  separado según la guía de banco vigente, `docs/hardware/banco-wifi-e2.md` (rama `test/banco-wifi-e2`). Sin picos medidos el resultado es **preliminar**: no confirma el
  presupuesto y T1b no se hace. Si alguna cifra lo supera, **se detiene** el trabajo y se presentan opciones, todas
  solo por SIO.
- D1 sirve el menú en **< 1000 ms desde EN**: `listo_ms` (dato interno del ESP32, registro de arranques) más el
  desfase de arranque medido con USB (§5, T8). Nada de Wi-Fi antes de eso.
- La clave del Wi-Fi vive solo en RAM. **Nunca** en el repositorio, la consola, `retrobridge-arranques.txt` ni otro
  registro. Pruebas con una clave centinela.
- **NVS conservada**: ni se borra ni se escribe. Igualdad antes del primer arranque del candidato y después del
  ensayo, registrando solo hash o resultado.
- Archivos en la raíz de la microSD: `retrobridge-wifi.txt` (red y clave) y `retrobridge-fuentes.txt` (formato de
  `--sources`, 7 servidores como máximo).
- Sin código de FujiNet ni de terceros. Flags: `-std=gnu++17 -fno-exceptions -fno-rtti -Wall -Wextra -Werror`.
- Cada commit, push, PR, flash, escritura o lectura por COM3 y salida de GPIO necesita aprobación del responsable.

## §1. Contexto que fija el diseño

- Spec, criterios 1–13. Desde la reordenación del 2026-11-22 se construye sin esperar la medición: el criterio 1
  (T1a y T1b) es requisito de T8, la prueba con el USB desconectado, no de la integración.
- El menú TNFS ya existe y está probado en el PC: `rb_net_tnfs` (cliente y códec, ya es componente ESP),
  `MenuController` y `MenuSources` (en `rb_menu`), y `TnfsLoader`, `TnfsLink`, `UdpTnfsPort` y `SystemNameResolver`
  **solo en `host/src/`**. El ESP32 no puede usar estos cuatro sin moverlos a un componente.
- `UdpTnfsPort` ya tiene una rama POSIX (sockets BSD, `getaddrinfo`), que es la que ofrece lwIP en ESP-IDF.
  `TnfsLoader` usa `std::thread`; en ESP-IDF eso es un pthread sobre FreeRTOS.
- El arranque de `.XEX` desde TNFS ya funciona en el PC (`menu-xex`, verificado el 2026-11-20):
  `TnfsLoader::startXex` trae el archivo, `XexImage` y el cargador 6502 propio están en `rb_menu`, y el controlador
  asigna el `.XEX` a D1 y lo arranca con `A`. E2 reutiliza ese flujo: se mueve con `TnfsLoader` (T3) y no se escribe
  otro cargador.
- `MenuStationParts` ya acepta `loader` y el controlador ya acepta `setSourcesFile`. Hoy `main/menu_sio.cpp` no pasa
  ninguno: por eso `E` dice `SIN ARCHIVO DE FUENTES`.
- La placa tiene PSRAM (PR #77 quitó su test de memoria del arranque). Las imágenes TNFS viven en
  `MemoryImageStore` (9 huecos en RAM).
- El firmware lee el +5 V del SIO con su ADC (`logSio5v`). Sirve como **indicio** de caída de tensión, no como medida
  de corriente.
- No hay tabla de particiones propia: el firmware usa la de ESP-IDF por defecto. La pila Wi-Fi añade varios cientos
  de KB de código.
- `menu-unidades-persistentes` ya está en `main` (`b3bd374`): `main/menu_sio.cpp` guarda D1–D8 en la microSD y
  devuelve D1 al menú al apagar el 65XE (PR #84). E2 parte de ese `main` y no cambia esos comportamientos.

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
| C1 eléctrico | Banco (T1a, luego T1b) | T1a: demanda de la FujiNet **sin conexión eléctrica al Atari**, en reposo (E1 actual), búsqueda, conexión y transferencia, media y pico de cada fase ≤ 50 mA, registrados por separado; sin picos, preliminar. T1b, solo si T1a confirma con medias y picos: lo mismo alimentada por el SIO del 65XE. Método y cifras en `02-DOCS/raw/` | Nada |
| C2 arranque | Banco | 10 encendidos solo SIO: menú, `brownout` = 0 y `listo_ms` + desfase USB < 1000 ms en cada uno (método en «Arranque desde EN») | Nada |
| C3 navegar | Host + banco | Listado de carpetas en el 65XE; en host, `test_menu_tnfs` y `test_menu_navegacion` siguen verdes tras mover el cargador | Servidor TNFS falso en host |
| C4 arrancar | Host + Altirra + banco | a: la ATR de ensayo del servidor en D1 arranca con `A`. b: un `.XEX` real de `tnfs.calaluis.cl:16384`, elegido navegando una carpeta, asignado a D1 y arrancado con `A`, queda corriendo. En host, las pruebas de `menu-xex` con TNFS siguen verdes tras mover el cargador; en Altirra, el mismo `.XEX` corre antes del banco | Servidor TNFS falso en host; nada en el banco |
| C5 D2–D8 | Host + Altirra | Sectores de una imagen TNFS en D2 | Servidor falso |
| C6 editar | Host + banco | `E` reescribe `retrobridge-fuentes.txt`, lo crea si falta; tras apagar sigue | Carpeta temporal |
| C7 sin red | Host | Sin archivo, red inexistente o clave mala → motivo; fuente 1 operativa | `NetworkGate` falso |
| C8 servidor caído | Host | Motivo en el plazo del menú TNFS del PC; pantalla principal operativa | Servidor que no responde |
| C9 clave | Host + banco | La clave centinela no aparece en la consola, el registro de arranques ni el repo; `WifiConfig` no la formatea | Captura de salida |
| C10 NVS | Banco | SHA-256 de la partición NVS antes del primer arranque del candidato = SHA-256 tras el ensayo, con la lectura en memoria; solo se registran los SHA-256 | Nada |
| C11 imagen grande | Host | Imagen mayor que el límite → aviso, ninguna unidad cambia | Servidor falso |
| C12 regresión | Host + CI + Altirra | Todas las pruebas de host y la regresión NetSIO en verde | — |
| C13 XEX rechazado | Host | Un `.XEX` inválido o no compatible del servidor da `XEX NO VALIDO` / `XEX NO COMPATIBLE` y ninguna unidad cambia, con el cargador ya movido | Servidor falso |

**Lectura de la NVS (C10).** Se reutiliza la lectura en memoria que ya funcionó en el banco USB de
`menu-unidades-persistentes` (T6, del 2026-11-21 al 2026-11-22): `esptool` lee la partición NVS por COM3 a la RAM del
PC, calcula su SHA-256 y no escribe nada en disco. Se registra **solo el SHA-256**; el contenido no se guarda ni se
muestra. Partición en 0x9000, tamaño 0x6000; T6 comprueba que la tabla de particiones del candidato la deja igual.
La FujiNet va por USB para leerla y el SIO se desconecta. Cada lectura por COM3 pide aprobación.

**Arranque desde EN hasta D1 lista (C2, T8).** Con el USB desconectado no hay RTS ni consola: EN sube cuando el 65XE
alimenta la placa. `listo_ms` es el dato interno del ESP32 (`esp_timer`): no incluye la ROM, el cargador de arranque
ni el primer arranque que reinicia el watchdog. Por eso se mide en dos partes:

1. **Desfase, con USB y el mismo candidato y microSD**, antes de los encendidos solo SIO: 3 arranques en frío con el
   script de captura que suelta EN por RTS y marca t0. Desfase = hora del PC de la línea «D1 ready at» − el valor que
   esa línea imprime. Se toma el mayor de los 3. Es una cota superior (115200 baudios y retardo USB). Referencia:
   en el banco de `menu-unidades-persistentes` la línea llegó a 825,6 ms desde EN con D1 lista a 145 ms (desfase
   ≈ 681 ms, con el reinicio por watchdog conocido).
2. **10 encendidos solo con el SIO**, USB desconectado: cada uno añade una línea a `retrobridge-arranques.txt` en la
   microSD (`listo_ms`, `razon`, `brownout`, `primera_trama_ms`, `sio5v_arranque_mv`). El tiempo desde EN hasta D1
   lista es `listo_ms` + desfase. Pasa si es < 1000 ms en los 10 y `brownout` = 0.

**Recogida de la evidencia solo con el SIO.** La consola no existe en esa parte. La evidencia es:

- `retrobridge-arranques.txt`: tras el ensayo se saca la microSD y se copia el archivo, sin borrarlo, a
  `D:\GitHub\Respaldos\` (fuera del repositorio);
- fotos de la pantalla: el menú tras cada encendido, la carpeta del servidor, la ATR arrancada y el `.XEX` corriendo;
- la búsqueda de la clave centinela en `retrobridge-arranques.txt` y en la captura USB del desfase: 0 apariciones;
- el SHA-256 de la NVS, antes y después, con la FujiNet por USB y el SIO desconectado.

**Medida eléctrica (C1), en dos pasos.**

- **T1a, sin el Atari.** La FujiNet no se conecta eléctricamente al 65XE. Se alimenta a 5 V desde una fuente externa
  con el amperímetro en serie (fuente de laboratorio o medidor USB en línea). Se mide cada fase del firmware de banco.
  Se registran por separado la media y el pico de cada fase, más el reposo del firmware `menu-sio` actual en su
  propia fila. Si alguna cifra supera 50 mA, T1b no se hace: parada y opciones al responsable. Con todas las medias
  en 50 mA o menos y los picos sin medir, el resultado es **preliminar** y T1b tampoco se hace.
- **T1b, con el 65XE.** Solo si T1a confirma el presupuesto con medias y picos medidos. Cable SIO de paso con el pin 10 abierto y el amperímetro en serie, USB
  desconectado; mismas fases.

En los dos pasos, la media por fase sale del multímetro o del medidor en línea, y el pico de un osciloscopio con una
resistencia de 1 Ω en serie. Sin osciloscopio, el pico queda **sin medir**, el resultado es preliminar y el ADC
propio (`logSio5v`) solo aporta la caída del +5 V como indicio. Procedimiento y tabla: `docs/hardware/banco-wifi-e2.md` (rama `test/banco-wifi-e2`).

## §6. Secuencia

```text
├─▶ T2 WifiConfig ─┐
├─▶ T3 mover cargador ─┼─▶ T5 motivos en el menú ─▶ T6 pegamento ESP ─▶ T7 CI y Altirra ─▶ T7b demo USB ─┐
└─▶ T4 WifiStation ─┘                                                                                    ├─▶ T8 banco solo SIO
T1a (sin Atari, gate) ──▶ STOP si > 50 mA o preliminar ──▶ T1b (SIO del 65XE, gate) ──▶ STOP si > 50 mA ──┘
```

Reordenado el 2026-11-22 por el responsable: T2–T7b no esperan a T1. T1a y T1b siguen siendo la condición de T8, el
único paso que prueba y acepta la alimentación solo por SIO. T2, T3 y T4 son independientes entre sí. T1 usa un
firmware de banco mínimo que no se publica.

**Montaje de la demo T7b (USB, pin 10 aislado).** La FujiNet se alimenta solo por USB. El +5 V del pin 10 del SIO
queda abierto entre el 65XE y la FujiNet (cable de paso con el pin 10 cortado, o el contacto del pin 10 aislado con
cinta Kapton); el resto de las líneas, GND incluida, siguen conectadas. Antes de encender, con todo apagado: el
multímetro da circuito abierto entre el pin 10 del lado del 65XE y J1 pin 10 de la FujiNet, y continuidad en GND
(pin 4 o 6). Con el 65XE encendido y el USB desconectado, la FujiNet queda apagada. Con el pin 10 aislado, el ADC del
+5 V del SIO lee ~0 mV: el aviso de apagado del 65XE (`sio_power_watch`) no actúa en la demo y no forma parte de
ella.

## §7. Riesgos

| # | Riesgo | Disparador | Impacto | Mitigación |
| --- | --- | --- | --- | --- |
| R1 | **El consumo supera 50 mA** (probable: un ESP32 con radio activa recibe a ~100 mA y transmite con picos de cientos de mA) | T1 | Detiene E2 | T1 mide también el reposo de E1 actual; si **ya** supera 50 mA sin Wi-Fi, se informa antes de seguir. Opciones preparadas, todas solo SIO: CPU a 80 MHz; ahorro de módem; potencia de transmisión reducida; Wi-Fi solo mientras se usa una fuente 2–8 (cambia «segundo plano» de la spec: decisión del responsable); condensador de reserva (cambio de hardware) |
| R2 | ESP-IDF escribe en NVS (calibración, configuración Wi-Fi) | Opciones por defecto | Rompe C10 | Opciones de §4 en `sdkconfig.menu-sio`; una comprobación del empaquetador falla si están activas o si el binario enlaza `nvs_flash_erase`/`nvs_set_*` |
| R3 | La clave sale por un volcado | Pánico con volcado de núcleo a flash, o registros de ESP-IDF | Rompe C9 | Volcado de núcleo desactivado en el paquete; nivel de registro de `wifi` en WARN; prueba con clave centinela en la captura completa |
| R4 | No cabe en la partición de la aplicación | La pila Wi-Fi + lwIP | No compila | Tabla de particiones propia sin tocar la NVS (mismo desplazamiento y tamaño); se comprueba en T6 |
| R5 | La radio retrasa la tarea SIO | Interrupciones Wi-Fi en el mismo núcleo | Tramas perdidas | Wi-Fi en el núcleo 0, tarea SIO fija en el otro núcleo; contador de tramas cortas del registro de arranques en T8 |
| R6 | Imágenes grandes agotan la PSRAM | ATR de muchos MB | Fallo de reserva | Límite por imagen y total en `MemoryImageStore`, criterio 11 |
| R7 | Romper las unidades persistentes o la vuelta al menú al apagar | T6 toca `menu_sio.cpp`, que ya las tiene (`b3bd374`) | Regresión de T6 de `menu-unidades-persistentes` | T6 parte del `main` actual; sus pruebas de host (`test_drive_memory`, `test_sio_power_watch`) y las guardas de pila y NVS del empaquetador siguen verdes |

## §8. Tareas

| T | Qué | Criterios | Riesgo (tier) | Comprobación de hecho | Depende de | Paralelo |
| --- | --- | --- | --- | --- | --- | --- |
| T1a | **Demanda sin el Atari.** Firmware de banco (no publicado) que, por fases separadas y anunciadas en la consola, queda en reposo, busca red, conecta y descarga por TNFS la ATR de ensayo, con red y clave leídas de la microSD; medir la demanda de cada fase con alimentación externa, sin conexión eléctrica al 65XE. Incluye el reposo del firmware E1 actual | C1 | 2 | `02-DOCS/raw/<fecha>-consumo-e2-sin-atari.md` con método, foto, media y pico por fase registrados por separado y veredicto: confirmado (todo ≤ 50 mA), preliminar (picos sin medir) o superado; si no, opciones presentadas y parada | Instrumentos confirmados | No |
| T1b | **Demanda con el SIO del 65XE.** Las mismas fases con la FujiNet alimentada por el pin 10 y el USB desconectado | C1 | 2 | `02-DOCS/raw/<fecha>-consumo-pin10-e2.md` con las mismas cifras y veredicto con media y pico ≤ 50 mA | T1a confirmado con medias y picos | No |
| T2 | `WifiConfig` + pruebas de host (formato, espacios, límites, archivo ausente, clave centinela nunca en motivos ni salidas) | C7, C9 | 1 | ctest verde con `test_wifi_config`; un mutante que imprime la clave es detectado | — | Sí |
| T3 | Mover `TnfsLoader` (trabajos ATR y XEX), `TnfsLink`, `UdpTnfsPort`, `SystemNameResolver` a `rb_menu_tnfs`; `NetworkGate` opcional; el PC usa «siempre hay red» | C3, C4, C5, C8, C11, C12, C13 | 1 | ctest verde con todas las pruebas TNFS y `menu-xex` de host sin cambios de comportamiento; `rb_menu_tnfs` compila como componente ESP en CI | — | Sí |
| T4 | `rb_esp_wifi::WifiStation`: estación en segundo plano, reintentos, estado, sin NVS; opciones de `sdkconfig.menu-sio` | C7, C9, C10 | 2 | CI verde con el paquete `menu-sio`; comprobación del empaquetador falla con `CONFIG_ESP_WIFI_NVS_ENABLED=y` (test rojo primero) | — | Sí |
| T5 | Motivos de red en `MenuController` (`SIN WI-FI: …`) y `E` crea el archivo si falta | C6, C7, C8 | 1 | ctest verde con pruebas nuevas de motivos y de creación del archivo; pruebas de menú existentes verdes | T3 | No |
| T6 | Pegamento en `main/menu_sio.cpp` (`finish()`), tabla de particiones propia, publicación atómica de fuentes y cargador | C2–C7, C11 | 2 | CI verde; tamaño de la aplicación dentro de su partición; NVS en el mismo desplazamiento y tamaño que hoy | T2, T4, T5 | No |
| T7 | Regresión: host, CI, Altirra con servidor TNFS | C4, C12, C13 | 1 | CI verde; registro de Altirra con navegación, la ATR de ensayo arrancada y el `.XEX` del banco corriendo | T6 | No |
| T7b | **Demo en el 65XE con USB y el pin 10 aislado** (aprobación de flash y COM3): montaje comprobado según §6; SHA-256 de la NVS en memoria antes; flash del paquete `menu-sio` de T6/T7; abrir `tnfs.calaluis.cl:16384` desde RetroBridge, entrar en una carpeta, elegir un `.XEX` real, asignarlo a D1 y pulsar `A` hasta ver el programa corriendo; captura de consola USB; SHA-256 de la NVS después. No acepta la alimentación solo por SIO | C3, C4b, C9, C10 (con USB) | 2 | `02-DOCS/raw/<fecha>-demo-e2-usb.md` con el montaje medido, paquete y SHA-256 del ZIP, foto de la carpeta, foto del `.XEX` corriendo, SHA-256 de la NVS iguales y la centinela con 0 apariciones en la captura | T7 | No |
| T8 | **Banco 65XE, USB desconectado** (aprobación de flash y COM3): SHA-256 de la NVS en memoria antes, flash, desfase EN–D1 con USB (3 arranques), 10 encendidos solo SIO; abrir `tnfs.calaluis.cl:16384` desde RetroBridge, navegar una carpeta, elegir el `.XEX` real, asignarlo a D1 y pulsar `A` hasta ver el programa corriendo; lo mismo con la ATR de ensayo; captura de consola del desfase; SHA-256 de la NVS en memoria después | C1, C2, C4a, C4b, C9, C10 + criterio final | 2 | `02-DOCS/raw/<fecha>-banco-e2.md` con `retrobridge-arranques.txt` copiado, desfase y `listo_ms` + desfase < 1000 ms en los 10, foto de la carpeta, foto del `.XEX` corriendo y de la ATR arrancada, SHA-256 de la NVS iguales y búsqueda de la centinela en la captura con 0 apariciones | T7b, T1a y T1b confirmados | No |

**Orden:** (T2 ∥ T3 ∥ T4) → T5 → T6 → T7 → T7b (demo con USB y el pin 10 aislado); T1a → T1b en paralelo; T8
(USB desconectado, solo SIO) cuando T7b y T1a/T1b estén cumplidos. Cada tarea pasa su revisión según `rsc risk` antes de la
siguiente. T8 no empieza si T1a y T1b no confirman media y pico ≤ 50 mA en cada fase o si el responsable no ha decidido una opción. T2–T7b no esperan a T1 (reordenación del 2026-11-22).

## Ensayo de transferencia

- Servidor: `tnfs.calaluis.cl:16384`, alcanzable desde la red del banco (comprobado el 2026-11-22 desde el PC: monta,
  lista carpetas y sirve la ATR entera).
- ATR de ensayo: `/Aportes_Eduardo_Antonio (Chalo)/Demoscene/NumenPAL.atr`, 196 496 bytes, cabecera ATR válida,
  sectores de 256 bytes, SHA-256 `7a47b8e0ecb6e53d…`. El firmware de banco compara el SHA-256 de lo descargado.

- `.XEX` de ensayo: uno real de la biblioteca del responsable en `tnfs.calaluis.cl:16384`, que el flujo del PC ya
  arranca en Altirra. Su ruta, tamaño y SHA-256 se fijan antes de T7; T7 y T8 usan el mismo archivo.

## Preguntas para el responsable

- **¿Qué `.XEX` de tu biblioteca usamos en T7 y T8?** Ruta en `tnfs.calaluis.cl:16384`. Antes de T7 se comprueba en
  el PC que el menú lo arranca en Altirra.

- **¿Qué instrumentos hay en el banco?** Para T1a: fuente de 5 V o medidor USB en línea, y multímetro con escala de
  mA. Para T1b: cable SIO de paso que se pueda abrir en el pin 10. Con osciloscopio y una resistencia de 1 Ω se miden
  los picos; sin él, T1a solo da un resultado preliminar. **La medición queda pendiente hasta confirmarlo.**
