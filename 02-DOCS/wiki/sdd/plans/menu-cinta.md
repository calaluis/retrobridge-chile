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
   - **T7 (2026-11-23):** `CassetteService` elige sus salidas una vez, en `start()`; todavía no puede tomar DATA IN
     en marcha. Hasta T9 el paso del árbitro recibe `gates = puertas && kTapeOutput` (`kTapeOutput = false` en
     `menu_sio.cpp`): el árbitro queda inerte aunque se firmen las puertas, y el UART del SIO nunca se cierra.
5. **Fallo en el traspaso:** si `take()` falla, la línea vuelve al disco con `begin()`; si `begin()` falla, se
   reintenta cada 100 ms y se registra; D1 nunca queda sin UART en silencio.

Caso «cinta que luego lee disco» (p. ej. un juego que carga niveles desde D1): regla 3, primer guion. El Atari
apaga MOTOR antes de hablar por SIO; al bajar COMMAND el disco recupera la línea. Se pierde, como mucho, la primera
trama: el OS la reintenta.

### 2.4 T9a: traspaso entre tareas y salida en marcha (enmienda del 2026-11-24)

Esta sección cambia el traspaso de §2.3 para T9a. Las reglas 1–5 siguen; cambia **cómo** se ejecuta `ToTape` y
`ToDisk`. Revisión independiente del 2026-11-24.

**Problema.**

- Hoy `DataInArbiter::toTape()` llama a `TapeLine::take()` y espera la respuesta en el mismo paso. Pero el traspaso
  real necesita la tarea de la casetera (`cas_loop`, un tick cada 2 ms): solo ella maneja el reproductor, el UART de
  la cinta y el RMT. Una petición a otra tarea no puede confirmar la propiedad de la línea en el mismo paso.
- Además, `DiskLine::end()` y `DiskLine::begin()` borran e instalan el driver de UART2 (`esp_sio_uart.cpp`). Eso
  reserva memoria dentro de la tarea SIO.

**Decisión 1: un solo driver de UART2, instalado una vez.** El disco instala UART2 al arrancar, como hoy. Nadie lo
borra durante el traspaso. Lo que pasa de una tarea a la otra es el **permiso** de transmitir por GPIO21, no el
driver.

- `DiskLine` en T9a:
  - `park()` deja de transmitir y de leer tramas;
  - `resume()` devuelve `Done` o `Failed` y no declara D1 recuperado hasta comprobarlo (revisión 3, ver «Reanudar
    el disco» abajo);
  - ninguna de las dos instala ni borra nada.
- La casetera usa ese mismo driver con su velocidad (600 baudios como regla). Lo hace con un modo «UART compartido»
  de `EspDataIn`, que no instala ni borra UART2.
- El modo actual de `EspDataIn::configure()` sigue igual para los paquetes `cassette` y `bench-c8`.
- El RMT sigue como está documentado: crea su canal y su codificador en cada pasada FSK, y eso reserva memoria
  (`esp_data_in.cpp`, «The RMT driver allocates»). Ocurre en la tarea de la casetera, nunca en la tarea SIO. T9a no
  lo cambia.

**Decisión 3: política de búfer de UART2 (revisión 2 del PR #99).**

- **El problema.** Hoy el disco instala UART2 sin anillo de TX. Sin anillo, `uart_write_bytes()` bloquea a quien
  llama hasta meter todos los bytes en la FIFO de 128 bytes. Con 512 bytes a 600 baudios, la tarea de la casetera
  quedaría bloqueada unos 6,4 s ((512 − 128) × 16,7 ms). En ese tiempo no confirma, no atiende el RMT y no ve
  peticiones.
- **La decisión.** El disco instala UART2 **una vez al arrancar**, con un anillo de TX de 2048 bytes (el mismo
  tamaño que ya usa el modo propio de `EspDataIn`). Esa reserva ocurre al arrancar, que es lo que permite ADR-011.
- **El disco conserva sus tiempos.** `EspSioUart::write()` sigue llamando a `uart_write_bytes()` y después a
  `uart_wait_tx_done()` con el plazo de la línea. La respuesta termina en el mismo instante que hoy. Lo único que
  cambia es que el relleno de la FIFO pasa a la interrupción del driver.
- **La cinta nunca bloquea, y cuenta todo lo que está en vuelo (revisión 3; corregida en la revisión del PR #101).**
  La interrupción del driver pasa bytes del anillo a la FIFO de hardware (hasta 128), así que «anillo casi vacío» no
  acota lo que falta por salir. Por eso la cinta calcula los bytes en vuelo como la suma de:
  - **anillo:** 0 si está vacío; si está ocupado, o si la lectura falla, cuenta como **lleno** (su tamaño).
    `uart_get_tx_buffer_free_size()` no da el espacio libre: en ESP-IDF 6.1 (`esp_driver_uart/src/uart.c`) es una
    cota conservadora para la siguiente escritura en un anillo NOSPLIT. Cada `uart_write_bytes()` mete dos
    elementos (descripción: 8 + 12; datos: 8 + datos redondeados a 4). Con el anillo vacío devuelve tamaño − 8 − 20
    (2020 con 2048); con un solo elemento dentro ya baja a 996. Por eso solo se usa para saber si está vacío:
    `libre + 28 ≥ tamaño`. Una versión de IDF con otros tamaños se lee como «nunca vacío» y la cinta se para, que
    es seguro;
  - **FIFO:** 128 − huecos libres de la FIFO de TX (`uart_ll_get_txfifo_len()` de la HAL de ESP-IDF);
  - **registro de desplazamiento:** 1 byte, el que sale en ese momento.

  Se lee primero el anillo y después la FIFO. Si la interrupción mueve bytes entre las dos lecturas, se cuentan dos
  veces: otra vez sobrestima, nunca subestima.
- **La regla.** En el modo compartido, `send()` escribe solo si los bytes en vuelo son menos de
  `kSharedInFlight` = 32, y escribe como mucho `kSharedInFlight − en vuelo − 1` bytes. Como el anillo ocupado
  cuenta como lleno, en la práctica solo escribe con el anillo vacío. Tras escribir, la suma solo
  baja. Lo que no cabe se queda en el reproductor, que ya lo tiene en el banco PSRAM, y sale en los ticks
  siguientes. 32 bytes son unos 0,53 s a 600 baudios: mucho más que un tick de 2 ms, así que el flujo no se corta.
- **Consecuencia para los plazos.** Como mucho hay 32 bytes en vuelo **entre anillo, FIFO y registro**. Por eso
  `kReleaseTimeoutUs` = 32 × 16,7 ms (≈ 533 ms) + la parada del RMT (≤ 20 ms) + 20 ms de margen ≈ **575 ms**; se
  fija en 600 ms. Antes podía llegar a 8,6 s.
- **Si la HAL no da la ocupación de la FIFO** en la versión de ESP-IDF del proyecto, se usa la alternativa de abajo
  (escribir solo con la FIFO vacía). Las dos dan la misma cota de 32 bytes.
- **Alternativa, si el banco muestra que el anillo cambia los tiempos del SIO.** Se deja el disco sin anillo. La
  cinta escribe entonces solo con la FIFO vacía y el transmisor parado (`uart_wait_tx_done(UART2, 0) == ESP_OK`), y
  como mucho `kSharedInFlight` = 32 bytes por escritura (revisión 4). Como la FIFO y el registro están vacíos al
  escribir, en vuelo quedan como mucho 32 bytes: la misma cota y el mismo `kReleaseTimeoutUs` = 600 ms. Sin anillo,
  `uart_write_bytes()` copia los 32 bytes directo a la FIFO de 128, así que tampoco bloquea. El coste es un hueco de
  hasta 2 ms (un tick) entre ráfagas de 32 bytes, que el registro de T9b tendría que medir. La prueba de T9a.2
  cubre también este modo: la misma suma ≤ 32 y el mismo `release` ≤ 600 ms.

**Decisión 2: contrato asíncrono con estados explícitos.** Un objeto compartido, `DataInHandover`, lleva el estado
del permiso. Usa solo atómicos, sin cerrojos ni memoria dinámica:

```text
estado:    Disk | TakeRequested | Tape | ReleaseRequested | Fault | Released
secuencia: uint32_t, sube con cada petición; una confirmación vale solo para su secuencia

TapeLine::take()    -> Done | Pending | Failed     // reemplaza al bool actual
TapeLine::release() -> Done | Pending | Failed     // reemplaza al bool actual
```

| Paso | Quién | Qué hace | Confirmación |
|---|---|---|---|
| 1 | Tarea SIO (árbitro) | Quiere la cinta (regla 1). Llama a `DiskLine.park()` solo cuando su TX está vacío (`uart_wait_tx_done(UART2, 0) == ESP_OK`). Si no está vacío, lo intenta en el paso siguiente | — |
| 2 | Tarea SIO | Publica `TakeRequested` con una secuencia nueva. `take()` devuelve `Pending`. El árbitro pasa a un estado nuevo, **`TakePending`**: el disco está en pausa y la cinta todavía no transmite | — |
| 3 | Tarea de la casetera, en su tick | Comprueba que su salida está en «descartar» y que el hardware está libre (abajo). Fija su velocidad. Cambia el reproductor a `EspDataIn` | Publica `Tape` con esa secuencia |
| 4 | Tarea SIO | `take()` ve `Tape` con su secuencia y devuelve `Done`. El árbitro pasa a `Tape` | — |
| 5 | Tarea SIO | Quiere el disco (regla 3). Publica `ReleaseRequested`. `release()` devuelve `Pending`. El árbitro pasa a **`ReleasePending`**: hoy ese estado significa «falló»; ahora significa «esperando» | — |
| 6 | Tarea de la casetera | Pasa el reproductor a «descartar». Llama a `stop()` del RMT (el pin vuelve al UART). Espera a que el hardware esté libre | Publica `Disk` con esa secuencia |
| 7 | Tarea SIO | `release()` ve `Disk` y devuelve `Done`. Llama a `DiskLine.resume()`. El árbitro pasa a `Disk` **solo si** `resume()` devuelve `Done`; si devuelve `Failed`, pasa a `DiskFault` (abajo) | — |

**Hardware libre** es la condición para confirmar en los pasos 3 y 6. Cambiar entre dos ticks no basta: hay que
mirar el hardware. Las tres condiciones son:

- UART2: `uart_wait_tx_done(UART2, 0) == ESP_OK`. Así no quedan bytes en el anillo ni en la FIFO de TX.
- RMT: `!fskActive()`, `!releasePending()` y ninguna transmisión en vuelo (`sending_ == false` y `txDone_` visto).
- `EspDataIn` sin fallo (`!faulted()`).

Si algo sigue ocupado, la tarea de la casetera no confirma y vuelve a mirar en el tick siguiente.

**Plazos.** La tarea SIO los mide con el tiempo que ya recibe `step()`.

- `kTakeTimeoutUs` = 20 ms, es decir, 10 ticks de la casetera. Al vencer:
  - la tarea SIO publica `ReleaseRequested`, y la petición sigue como un `release` normal;
  - el disco reanuda solo cuando la casetera confirma `Disk`;
  - cuenta como `TakeFailed` y se reintenta cada 100 ms, como hoy (regla 5).
- `kReleaseTimeoutUs` es el tiempo de vaciar lo que haya en la cola de TX, más un margen:
  - lo normal es un byte, unos 17 ms a 600 baudios;
  - con la decisión 3, como mucho quedan 32 bytes en vuelo entre anillo, FIFO y registro: `kReleaseTimeoutUs` = 600 ms;
  - el valor exacto se fija y se prueba en T9a.2.

**Recuperación.**

- Si `release` vence su plazo, el estado pasa a **`Fault`**:
  - el disco **no** reanuda, porque dos transmisores en GPIO21 es el fallo que más cuesta (R1);
  - la consola lo dice una vez;
  - cada 100 ms se vuelve a publicar `ReleaseRequested`.
- En `Fault`, la casetera **no** confirma `Disk` hasta que el enrutamiento está restablecido **y verificado**
  (revisión 2 del PR #99):
  1. pasa el reproductor a «descartar» y llama a `stop()` del RMT;
  2. espera a que el RMT suelte el pin (`!releasePending()`) y a que el canal quede borrado;
  3. devuelve GPIO21 a UART2 TX (`uart_set_pin`);
  4. **verifica** la matriz GPIO: la señal de salida de GPIO21 es `U2TXD_OUT_IDX` con la salida habilitada. Es la
     misma lectura que ya hace el banco C8 (`main/bench_c8.cpp`, `route()`), que pasa a una función compartida;
  5. comprueba que el TX de UART2 está vacío;
  6. solo entonces confirma `Disk` con la secuencia de la petición.
- Si falla el paso 3 o el 4:
  - la casetera libera la línea (`gpio_reset_pin(GPIO21)`: sin salida, el 74LS07 la suelta, `faulted()`);
  - publica **`Released`**, un estado nuevo que **no** es `Disk`;
  - el árbitro queda en `None`, y el disco **no** reanuda;
  - cada 100 ms se pide otra vez la recuperación;
  - la consola lo dice una vez por cambio de estado.
- Una confirmación que llega con una secuencia vieja, o después de su plazo, se ignora y no cambia el estado.
- Mientras dura un `Fault` o un `Released`, D1 queda en silencio. Se ve en la consola, y el Atari reintenta.
- Un `Failed` de `take()` o de `release()` (petición rechazada, no vencida) sigue la regla 5 de §2.3.

**`tapeEnded` solo con la salida vacía (revisión 2 del PR #99).** `TapeProgress.ended()` se publica verdadero
solo cuando se cumplen las tres condiciones:

- el reproductor terminó la última parte (`CassettePlayer::atEnd()`);
- el anillo y la FIFO de TX de UART2 están vacíos (`uart_wait_tx_done(UART2, 0) == ESP_OK`);
- no queda ninguna pasada FSK activa ni pendiente (`!fskActive()`, `!releasePending()`, feed vacío).

Así la regla 3 («fin de cinta y MOTOR apagado → disco») nunca pide la línea con bytes o FSK todavía en camino. La
condición «hardware libre» del paso 6 sigue igual: es una segunda barrera, no la única.

**Reanudar el disco (revisión 3 del PR #99).** Que la casetera confirme `Disk` dice que GPIO21 volvió a UART2.
No dice que la línea del disco esté como la necesita D1. `DiskLine.resume()` lo comprueba él mismo:

1. vuelve a poner la velocidad del disco, `SioLineTransport::kBaud` = 19 200 baudios (`uart_set_baudrate`);
2. la **verifica**: `uart_get_baudrate()` devuelve un valor dentro de ±2 % de 19 200 (el divisor del ESP32 no da
   el valor exacto);
3. verifica el formato de trama: 8 bits, sin paridad, 1 bit de parada (`uart_get_word_length`, `uart_get_parity`,
   `uart_get_stop_bits`). La cinta no lo cambia, pero `resume()` no lo supone;
4. vacía la entrada (`uart_flush_input`): lo que entró a 600 baudios es basura para el disco;
5. solo entonces vuelve a atender tramas y devuelve `Done`.

Si falla cualquier paso, `resume()` devuelve `Failed` y:

- el disco **no** atiende tramas: D1 sigue en silencio y el Atari reintenta;
- el árbitro pasa a **`DiskFault`**, un estado nuevo. No es `Disk`: no se emite ningún evento de D1 recuperado;
- la tarea SIO reintenta `resume()` cada 100 ms. Son llamadas sin reservas ni `uart_driver_*`;
- la consola lo dice una vez por cambio de estado;
- `DiskFault` no vuelve a pedir la cinta: el MOTOR la pide solo con el árbitro en `Disk`.

**Sin memoria dinámica en la ruta SIO.**

- En la tarea SIO, `step()`, `park()`, `resume()`, `take()` y `release()` no reservan memoria.
- Tampoco llaman a `uart_driver_install`, `uart_driver_delete`, `rmt_new_*` ni `rmt_del_*`.
- Las reservas del RMT (una por pasada FSK) quedan en la tarea de la casetera, como ya está documentado.

**Sin salida física hasta T9b.**

- `kTapeOutput` sigue en `false` en `menu_sio.cpp`.
- Las puertas del paquete siguen cerradas.
- Todo T9a se ejercita en host. En el ESP32 compila, pero queda inerte.

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

T9a (§2.4):
DataInArbiter.owner()     -> Disk | TakePending | Tape | ReleasePending | Fault | Released | DiskFault | None
TapeLine.take()/release() -> Done | Pending | Failed
DiskLine.park()          -> sin instalar ni borrar UART2
DiskLine.resume()        -> Done | Failed; Done solo con 19 200 baudios ±2 %, 8N1 verificados y la entrada vacía
DataInHandover            -> estado + secuencia, solo atómicos; la tarea SIO escribe las peticiones y la
                             tarea de la casetera las confirmaciones
TapeProgress.ended()      -> bool atómico que publica la casetera: fin de la última parte Y salida vacía; lo lee
                             la tarea SIO
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
| Casetera sin respuesta: no se sabe si tomó la cinta; `C:` sin cambio (revisión 2 del PR #85) | `CASETERA NO RESPONDE, REPITE EN UN RATO`; al arrancar, `C: CASETERA NO RESPONDE` |
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
| T9a.1 | unidad (`test_data_in_arbiter`, `test_data_in_handover`, nuevo) | `take` `Pending` → `TakePending` sin transmisor activo; `Done` solo con la secuencia propia; una confirmación vieja se ignora; `take` vencido a los 20 ms → `release` → el disco reanuda solo tras `Disk`; `release` vencido → `Fault` sin `resume()`, reintento cada 100 ms; nunca `resume()` con la cinta en `Tape` | casetera falsa que confirma en el tick N, nunca, o con otra secuencia |
| T9a.1, R10 | unidad | en la tarea SIO, `step()`, `park()`, `resume()`, `take()` y `release()` hacen **cero** reservas (contador de `operator new`, como `menu_tnfs_rig.hpp`) y **cero** llamadas a `uart_driver_*` y `rmt_new_*`/`rmt_del_*` | dobles de ESP-IDF del host que registran la llamada y la tarea |
| T9a.2, R11 | unidad (`test_esp_data_in`) | modo compartido sin `uart_driver_install`/`delete`; no confirma con TX ocupado, con una pasada FSK activa, con `releasePending()` ni con `faulted()`; confirma cuando todo queda libre; las reservas del RMT ocurren solo en la tarea de la casetera, una por pasada (comportamiento documentado) | dobles de `uart_wait_tx_done` y RMT ocupados o libres |
| T9a.2, R9 | unidad (`test_esp_data_in`, `test_esp_sio_uart`) | con un doble de UART que modela el anillo, la FIFO de 128 bytes y el tiempo por byte: `send()` del modo compartido **nunca** bloquea (0 µs en `uart_write_bytes`), la suma anillo + FIFO + registro nunca pasa de 32 bytes, **también** cuando el doble mueve bytes del anillo a la FIFO entre las dos lecturas y cuando el anillo está vacío pero la FIFO llena; con 512 bytes pendientes, el `release` confirma en ≤ 600 ms; un mutante que mira solo `uart_get_tx_buffer_free_size()` deja hasta 160 bytes en vuelo y la prueba falla; `EspSioUart::write()` termina en el mismo tiempo de línea con anillo que sin él (respuesta de 129 bytes a 19200 baudios incluida) | anillo (con cabeceras por escritura), FIFO, registro de desplazamiento, interrupción que mueve bytes y reloj del doble |
| T9a.1 (`resume`) | unidad (`test_esp_sio_uart`, `test_data_in_handover`) | `resume()` devuelve `Done` solo con la velocidad leída dentro de ±2 % de 19 200, 8N1 y la entrada vaciada; `uart_set_baudrate` fallido, velocidad leída en 600 o formato distinto → `Failed`, árbitro en `DiskFault`, ninguna trama atendida y ningún evento de D1 recuperado; un reintento que funciona a los 100 ms → `Disk` una sola vez; en `DiskFault` el MOTOR no pide la cinta | doble de UART con `set_baudrate`, `get_baudrate` y formato programables |
| T9a.2 (`Fault`) | unidad | `Disk` se confirma solo tras `uart_set_pin` correcto **y** la matriz GPIO leída como `U2TXD_OUT_IDX` con salida habilitada; `uart_set_pin` fallido o lectura distinta → `Released`, ningún `resume()`, reintento cada 100 ms; una recuperación que funciona al segundo intento → un solo `resume()`; confirmación con secuencia vieja o tras el plazo → ignorada | doble de la matriz GPIO (registro de `out_sel` y `enable`) y de `uart_set_pin` con fallo programable |
| T9a.4 | unidad | `ended()` sigue falso con el reproductor al final pero con bytes en el anillo o con una pasada FSK activa; pasa a verdadero solo al vaciarse todo; con MOTOR apagado, el árbitro no pide `release` antes | reproductor al final y doble de UART/RMT ocupados |
| T9a.3 | unidad (`test_cassette_player`) | el cambio de salida espera a que la escritura en curso termine; ningún byte partido; tras soltar, la salida es «descartar» | salidas falsas |
| T9a.4 | unidad | `ended()` pasa a verdadero al terminar la última parte y vuelve a falso al rebobinar o expulsar | reproductor con una cinta de dos partes |
| T9a.5, T9a.6 | unidad (`test_menu_cinta`) | `A` con D1 vacía y cinta en `C:` → arranque; sin cinta → `D1 SIN DISCO: ABRE LA FUENTE 1`; la respuesta de D1 tras el arranque de cinta según la decisión; línea de ayuda literal ≤ 40 columnas | `FakeTapeDeck` |
| AC9–AC12 (lógica) | integración de host | guiones completos: START+OPTION → MOTOR → `TakePending` → `Tape` → pausas sin soltar → fin → `ReleasePending` → `Disk`; turbo `fsk`; COMMAND con MOTOR apagado; apagado del 65XE; **con `tapeOutput = false`, `take()` no se llama nunca** | casetera, disco, MOTOR y COMMAND falsos |

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
| T9a.1 | `DataInHandover` y el árbitro con `TakePending`, `ReleasePending` (esperando) y `Fault`; `take()`/`release()` con `Done`/`Pending`/`Failed`; plazos de §2.4 | AC9–AC12 (lógica) | 1 | T4 | sí | `ctest -R data_in` verde; las pruebas «T9a.1» de §5 vistas fallar antes |
| T9a.2 | `EspDataIn` en modo UART compartido con la política de búfer (decisión 3); condición «hardware libre»; recuperación verificada de `Fault`; `kReleaseTimeoutUs` | requisito 1 | 2 | T9a.1 | no | `ctest -R esp_data_in` verde; ninguna llamada a `uart_driver_install`/`delete` en el modo compartido |
| T9a.3 | Salida en marcha: el reproductor cambia entre «descartar» y `EspDataIn` solo cuando la casetera confirma, nunca a media escritura | requisito 1 | 2 | T9a.2 | no | `ctest -R "cassette_player\|data_in_handover"` verde |
| T9a.4 | `tapeEnded` real: `TapeProgress.ended()` publicado por la casetera solo con la salida vacía, leído sin bloquear | AC9–AC12 (lógica), requisito 2 | 1 | T9a.1 | sí | `ctest -R data_in_handover` verde: la regla 3 («fin + MOTOR apagado → disco») con el valor publicado |
| T9a.5 | `A` sin disco en D1 con una cinta en `C:` arranca (`COLDSV`); D1 tras el arranque de cinta según la pregunta abierta de la spec | requisitos 3 y 4 | 1 | — | sí | `ctest -R menu_cinta` verde |
| T9a.6 | Línea de ayuda con `A`, OPTION, START+OPTION y `C`, dentro de 40 columnas | requisito 5 | 0 | — | sí | `ctest -R menu_cinta` verde; texto literal |
| T9a.7 | Firmware: UART2 con anillo de TX de 2048 bytes al arrancar (decisión 3), `DiskLine` con `park`/`resume`, `EspTapeLine` en lugar de `PendingTapeLine` y `DataInHandover` en `menu_sio.cpp`. **`kTapeOutput` sigue en `false`** | — | 2 | T9a.1–T9a.4 | no | CI verde: ESP-IDF, paquete `menu-sio` y comprobaciones de puertas **sin cambios** |
| T9b | Banco AC9–AC12 con MOTOR activo y salida de casetera | AC9–AC12 | 2 | T8, T9a y los **requisitos previos** (abajo) | no | registro con las tres cintas de `cassette-cas` cargadas desde el menú |

**Requisitos previos de T9b (enmienda del 2026-11-24).** Hacen falta todos. Ninguna aceptación de riesgo
reemplaza a ninguno:

1. **M3a medida** en el Atari solo (`docs/hardware/ficha-medicion-m3a-motor.md`), con su registro.
2. **Una protección de GPIO36 validada** (las etapas V de `docs/hardware/evaluacion-74lvc4245-motor.md`), o una
   medición M3a con el pico ≤ 3,3 V.
3. **H4-C** (`docs/hardware/h4-c-casetera.md`) con C5, C8a, C8b y C9 resueltos. C8b, que se hace con el Atari
   conectado, va **solo después** de cumplir 1 y 2, con el ensayo acotado de `h4-c-casetera.md` §3.1: un paquete
   de banco con las puertas cerradas, como `bench-c8`, que no aprueba H4 ni M3a.
4. **La aprobación de H4 y M3a que exige el empaquetador.** `tools/ci/firmware_package.py` abre la salida solo
   con H4 y M3a aprobadas en `docs/mediciones/puertas.json` (variante `cassette`, `gates=True`). H4-C no la
   reemplaza: es el criterio acotado con que el responsable la decidiría. El cambio de `puertas.json` y del perfil
   es un PR aparte del responsable.
5. Un PR, revisado aparte, que ponga `kTapeOutput = true`.

Hasta entonces, T9a no cambia `puertas.json`, ADR-012 ni las comprobaciones del empaquetador.

Requisitos de código de T9 antes de poner `kTapeOutput = true` (revisión de T7 en el PR #85, 2026-11-23). Desde el
2026-11-24 los cubre T9a: el 1 es T9a.2 y T9a.3; el 2 es T9a.4; el 3 y el 4 son T9a.5; el 5 es T9a.6.

1. **Salidas de la casetera en marcha.** `CassetteService` elige en `start()` si reproduce por `EspDataIn` o
   descarta; tiene que poder pasar a `EspDataIn` cuando el árbitro llama a `TapeLine.take()` y volver a descartar
   con `release()`, sin reiniciar el servicio. Hasta entonces `PendingTapeLine::take()` devuelve `false`.
2. **`tapeEnded` real.** `DataInReadings.tapeEnded` vale siempre `false` en `menu_sio.cpp`; tiene que llegar del
   reproductor (fin de la última parte de la cinta) sin bloquear el bucle SIO, para que la regla 3 («fin de cinta y
   MOTOR apagado → disco») funcione.

Requisitos de arranque de T9 (observación del responsable tras el banco T8, 2026-11-23). `A` ya salta a `COLDSV`,
que lee las teclas de consola: con OPTION arranca D1 sin BASIC, sin nada D1 con BASIC, con START+OPTION la cinta.

3. **`A` sin disco en D1.** Hoy `A` sin disco en D1 se niega con `D1 SIN DISCO: ABRE LA FUENTE 1`. Con una cinta en
   `C:` tiene que dejar arrancar. El menú del Atari no ve START antes de saltar, así que no sabe qué se va a arrancar.
4. **D1 durante un arranque de cinta.** Tras la cinta, el sistema operativo intenta también el arranque de disco.
   Si D1 sigue sirviendo el menú, el menú puede cargarse sobre el programa de la cinta. Hay que decidir y probar
   en el 65XE qué contesta D1 en ese arranque (por ejemplo, «sin disco» cuando D1 está vacía).
5. **Línea de ayuda.** Nombrar las teclas: `A` y las de consola (OPTION sin BASIC, START+OPTION cinta) y `C`
   como destino de una cinta, dentro de 40 columnas. Hoy la ayuda no nombra ni la cinta ni la `C`.

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
| R8 | La casetera no confirma a tiempo | casetera bloqueada o hardware ocupado | D1 en pausa | Plazos de §2.4; un `take` vencido sigue como `release`; el disco reanuda solo con confirmación (T9a.1) |
| R9 | `release` tarda segundos, o la cinta bloquea su tarea | UART2 sin anillo de TX: 512 bytes bloquean unos 6,4 s; o contar solo el anillo y dejar la FIFO llena (160 bytes, ≈ 2,7 s) | casetera sin confirmar; D1 en pausa | Decisión 3: anillo de 2048 al arrancar y como mucho 32 bytes en vuelo contando anillo + FIFO + registro (600 ms); prueba de bloqueo cero (T9a.2); tiempos del SIO comprobados en host y en el banco del disco |
| R10 | Memoria dinámica en la tarea SIO | un `uart_driver_*` o `rmt_*` llamado desde `step()` | viola ADR-011 | Decisión 1 de §2.4; prueba de host que cuenta reservas y llamadas por tarea (§5) |
| R11 | Cambio de salida con bytes o FSK en vuelo | cambiar solo «entre ticks» | bytes partidos o dos transmisores | Condición «hardware libre» de §2.4; prueba con el UART y el RMT ocupados (T9a.2) |
| R12 | D1 «recuperado» con la línea mal configurada | `resume()` sin verificar la velocidad tras la cinta a 600 baudios | D1 no responde o responde basura y nadie lo ve | «Reanudar el disco»: `resume()` verifica 19 200 baudios, 8N1 y vacía la entrada; `Failed` → `DiskFault` visible y reintento (T9a.1) |

**Decisiones abiertas:** ninguna bloquea T1–T8. El tamaño del banco PSRAM solo se revisa si R3 se dispara.

**Aislamiento:** el código va en la rama `feat/menu-cinta`, en su propio worktree, creada desde `main` al empezar
T1 (se pregunta antes, como siempre).
