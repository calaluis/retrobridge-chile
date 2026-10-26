# H5: diseño preliminar de la escucha SIO en modo solo recepción

> Estado: **PROPUESTA, NO APROBADA.** Planificación técnica, sin implementación. H5 empieza solo después de aprobar
> H4 (`docs/hardware/h4-puerta-sio.md`, checklist §7) y de una autorización explícita. Ningún punto de este
> documento autoriza a configurar GPIO, cambiar `sdkconfig` ni flashear.
>
> **Revisión técnica del 2026-10-25 (P2, P3 y P7, §8):** contrastada con la fuente de ESP-IDF v6.1 (etiqueta
> `v6.1`). P7 confirmado; P2 y P3 condicionados. Es investigación documental: no inicia H5 ni aprueba H4.

**Etiquetas:**
- **[D]** documentación de Espressif (ESP-IDF v6.1, hoja de datos, erratas);
- **[A]** arquitectura y ADR de RetroBridge;
- **[I]** propuesta o inferencia;
- **[P]** pendiente de verificar.

## 1. Alcance

H5 solo **escucha**: decodifica las tramas de comando del Atari y mide los tiempos [A] (§11, H5).

| GPIO | Señal | En H5 |
|---|---|---|
| 39 | COMMAND (entrada, R11 externa) | **Entrada con interrupción** |
| 33 | DATA OUT del Atari (entrada) | **RX de UART.** Pull-up interno: **opción**, sujeta a las mediciones de H4 (riesgo R3) |
| 21 | DATA IN hacia el Atari | **Nunca se configura** (DATA IN se maneja recién en H6) |
| 22, 26, 27 | PROCEED, INTERRUPT, CLOCK IN | Nunca se configuran |
| 25 | AUDIO (DAC) | Nunca se configura |
| 32 | CLOCK OUT | Sin configurar (modo asíncrono) |
| 36 | MOTOR | Sin configurar hasta resolver el riesgo R1 |
| 35 | SIO_5V (ADC) | **Sin ADC durante la escucha** (errata GPIO-3.11) |

## 2. UART para DATA OUT: propuesta UART2 (revisable)

**Periféricos que usa RetroBridge** (firmware `1b1527d` y alcance de la v1, arquitectura §0.1) [A]:

| Recurso | Uso |
|---|---|
| UART0 (GPIO1/3, CP2102) | Consola de diagnóstico por USB (`esp_console`) |
| SPI3 / VSPI | microSD (GPIO 5, 18, 19, 23) |
| `esp_timer` | Marcas de tiempo y benchmarks |
| Wi-Fi y Bluetooth | **No se usan** en la v1. TNFS por red queda para después (§5.3) y no usa una UART |
| PSRAM | Desactivada (fragmento de placa); GPIO16/17 reservados por el perfil |

**Por qué UART2:**
- Con esos periféricos, **UART1 y UART2 están libres y son técnicamente equivalentes** para recibir a 19 200
  baudios: las dos tienen FIFO por hardware y se enrutan a cualquier pin por la matriz GPIO [D]. Ninguna razón
  técnica obliga a elegir una.
- **La propuesta de UART2 es un criterio de desempate,** no una decisión cerrada:
  - Las dos tienen pines por defecto que no sirven en esta placa: UART1 en GPIO9/10 (flash) y UART2 en GPIO16/17
    (PSRAM del WROVER). En los dos casos hay que llamar a `uart_set_pin` **explícitamente**. El validador de
    `rb_board` ya rechaza 6–11 y 16–17. Este punto no favorece a ninguna.
  - Si en el futuro se habilitara el Bluetooth con HCI por UART, su UART por defecto sería UART1
    (`BTDM_CTRL_HCI_UART_NO`, valor 1) [D]. RetroBridge no lo prevé, así que el argumento es menor [I].
- **Se puede cambiar a UART1 en la revisión de H5** sin afectar al diseño: el número de UART es un parámetro del
  adaptador y del perfil de placa, no del dominio ni de los puertos.

**Configuración propuesta (para revisar en H5):**
- 19 200 baudios nominales, 8N1, sin control de flujo y sin inversión (`h4-puerta-sio.md` §3.2).
- `uart_set_pin(<UART elegida>, <sin TX>, 33, <sin RTS>, <sin CTS>)`.
- **Pull-up de GPIO33: decisión pendiente.**
  - El esquema no muestra pull-up y el comportamiento real no está medido (`h4-puerta-sio.md` §3.3).
  - Si M4/M5 confirman un nivel alto indefinido o un flanco lento, la opción es el pull-up interno. ESP-IDF resuelve
    la errata GPIO-3.6 para los pads RTC [D]: `gpio_pullup_en()` usa `rtc_gpio_pullup_en()` en ellos
    (`components/esp_driver_gpio/src/gpio.c`).
  - **En v6.1, `uart_set_pin` ya no activa el pull-up de RX** [D] (P3): el camino de RX de `_uart_set_pin6`
    (`components/esp_driver_uart/src/uart.c`) solo conecta la señal por la matriz GPIO o el IOMUX. ESP-IDF 5.4 sí
    llamaba a `gpio_set_pull_mode(rx_io_num, GPIO_PULLUP_ONLY)`. Si se decide usar el pull-up, H5 debe llamar
    **explícitamente** a `gpio_pullup_en(33)`; sin esa llamada, GPIO33 queda sin pull.
  - Esa decisión se toma en H5, con las mediciones.
- **TX sin asignar:**
  - **Verificado en la fuente de ESP-IDF v6.1** [D] (P3): `_uart_set_pin6` solo configura el TX si
    `tx_io_num >= 0`, y con `UART_PIN_NO_CHANGE` (−1) tampoco libera ni toca un TX anterior. El controlador no
    enruta pines por su cuenta: la única otra llamada interna a `_uart_set_pin6` es la autodetección de velocidad,
    que H5 no usa.
  - El TX llega de `validate_for_sio()` (`SioAssignment`): en `ReceiveOnly` vale `kNoPin` (−1), el mismo valor que
    `UART_PIN_NO_CHANGE`. La conversión debe escribirse de forma explícita en el adaptador [I].
  - En el equipo, `gpio_dump_io_configuration` debe mostrar que **GPIO21 no está conectado al TX de la UART
    elegida**, ni a ninguna otra salida, y que los pines por defecto de esa UART (17 para UART2, 10 para UART1) no se
    usaron.
- **ISR del UART en IRAM:**
  - ESP-IDF v6.1 tiene `CONFIG_UART_ISR_IN_IRAM` (desactivado por defecto) [D], y la arquitectura pide la ISR del UART
    en IRAM (§9.1) [A].
  - Alcance (P7) [D]: mueve a IRAM el manejador `uart_rx_intr_handler_default` y sus auxiliares (`UART_ISR_ATTR`,
    `uart.c`) y `uart_hal_iram` (`components/esp_driver_uart/linker.lf`). Depende de
    `!RINGBUF_PLACE_ISR_FUNCTIONS_INTO_FLASH` y activa `VFS_SELECT_IN_RAM` si hay `VFS_SUPPORT_SELECT`
    (`components/esp_driver_uart/Kconfig`).
  - Activarlo es un cambio de `sdkconfig` que **se aprueba aparte** en H5.
  - `uart_driver_install` **acepta** `ESP_INTR_FLAG_IRAM`, pero lo ajusta al Kconfig [D]: lo añade si la opción está
    activa y lo quita, con un aviso, si no lo está (`uart.c`, `uart_driver_install`). La ubicación depende solo de
    ese Kconfig. *(Corrige la versión anterior de este documento, que decía que no lo admitía.)*
- **Memoria:** los búferes del controlador se reservan una sola vez, en la inicialización, como permite el ADR-011.
  En la ruta de recepción no hay heap.

## 3. COMMAND en GPIO39: interrupción por nivel

**Restricciones del ESP32 v1.0 [D]:**
- GPIO39 es solo entrada y no tiene pull internos; R11 de 10 k a 3,3 V lo mantiene en alto.
- **GPIO-3.14:** una interrupción por flanco en el grupo GPIO32–39 impide cualquier otra interrupción del grupo.
- **GPIO-3.11:** con ADC1, ADC2 o el AMP encendidos, GPIO39 recibe pulsos a bajo de unos 80 ns.

**Propuesta [I]: emular los flancos con niveles, como recomienda la errata GPIO-3.14.**

1. Se arma `GPIO_INTR_LOW_LEVEL`. Cuando el Atari baja COMMAND, la ISR:
   - registra `t_fall = esp_timer_get_time()`;
   - cambia a `GPIO_INTR_HIGH_LEVEL`;
   - notifica a la tarea SIO con `xTaskNotifyFromISR`.
2. Cuando COMMAND sube, la ISR registra `t_rise`, vuelve a `GPIO_INTR_LOW_LEVEL` y notifica.
3. **ISR:**
   - escrita en C con `IRAM_ATTR`, datos en DRAM y registrada con `ESP_INTR_FLAG_IRAM`;
   - sin llamadas virtuales y sin `ESP_LOG`;
   - solo marca el tiempo, cambia el nivel y notifica [A] (§9.1, ADR-006, ADR-011).
4. **Filtro en la tarea**, no en la ISR:
   - se descarta un bajo de COMMAND más corto que un umbral (orden de 100 µs) [I], que separa los pulsos de 80 ns
     de la errata de una trama real de varios milisegundos (5 bytes a 19 200 baudios ≈ 2,6 ms);
   - un bajo **permanente** (Atari apagado, riesgo R12) se informa como estado y no como tramas.

**Resultado de P2 (ESP-IDF v6.1, 2026-10-25): condicionado.**

| Operación en la ISR | Seguridad con la caché deshabilitada | Evidencia |
|---|---|---|
| `esp_timer_get_time()` | **Segura** [D] | Alias de `esp_timer_impl_get_time`, marcada `ESP_TIMER_IRAM_ATTR` (`components/esp_timer/src/esp_timer_impl_lac.c`). `ESP_TIMER_IN_IRAM` vale `y` y solo es configurable con `SPI_FLASH_AUTO_SUSPEND` (`components/esp_timer/Kconfig`), que el ESP32 no tiene [I] |
| `xTaskNotifyFromISR` | **Segura** [D] | `xTaskGenericNotifyFromISR` y `vTaskGenericNotifyGiveFromISR` en IRAM (`components/freertos/linker.lf`) |
| `gpio_set_intr_type()` | **No segura** [D] | Queda en flash: `CONFIG_GPIO_CTRL_FUNC_IN_IRAM` solo mueve `gpio_set_level`, `gpio_intr_disable`, `gpio_get_level` y `gpio_hal_intr_disable` (`components/esp_driver_gpio/linker.lf`) |
| Despacho con `ESP_INTR_FLAG_IRAM` | La ISR puede ejecutarse con la caché deshabilitada [D] | `esp_intr_alloc.h`; `gpio_intr_service` es `IRAM_ATTR` (`gpio.c`) |

El paso 1 («cambia a `GPIO_INTR_HIGH_LEVEL`») **no puede usar `gpio_set_intr_type()`** desde una ISR registrada
con `ESP_INTR_FLAG_IRAM`. Con interrupción por nivel, la ISR **debe** desarmarla o cambiar el nivel, o se dispara
sin parar. Alternativas, a decidir en la revisión de H5 [I]:

- **(a)** `gpio_ll_set_intr_type()` de la HAL (`static inline`, queda dentro de la ISR); acopla el adaptador a la HAL.
- **(b)** La ISR llama a `gpio_intr_disable()` (exige `CONFIG_GPIO_CTRL_FUNC_IN_IRAM=y`, cambio de `sdkconfig` a
  aprobar) y la tarea rearma el nivel contrario; la latencia del rearme debe caber en la duración de COMMAND.
- **(c)** No usar `ESP_INTR_FLAG_IRAM`: contradice la arquitectura (§9.1) [A].

## 4. Delimitación de la trama y contrato de `SioTransport`

El puerto ya existe (`rb/app/ports/sio_transport.hpp`) [A]: `waitCommandFrame(timeoutUs)` devuelve `CommandBytes`
(`bytes[5]`, `count`, `commandReleasedAtUs`).

| Evento | Acción en `EspSioTransport` (H5) |
|---|---|
| COMMAND↓ (`t_fall`) | Vaciar el FIFO de RX y empezar la trama |
| COMMAND↑ (`t_rise`) | Leer los bytes recibidos; guardar los 5 primeros, contar el total y poner `commandReleasedAtUs = t_rise` |
| Error de trama u overrun en el UART | `TransportError` |
| Sin COMMAND dentro de `timeoutUs` | `Timeout` |
| `send()` | En H5 **no transmite**: devuelve error sin tocar GPIO21 |
| `releaseDataLine()` | Sin efecto (DATA IN nunca se configuró) |

Según el manual, el último bit llega entre 650 y 950 µs (t1) antes de COMMAND↑ [A] (§1), así que al subir COMMAND
los bytes ya están en el FIFO.

## 5. Integración hexagonal

```
rb_console (consola: "sio listen", "sio stats")       infraestructura
   └── SioListener / CommandFrameMonitor (rb_sio)     usa: SioTransport, Clock, CommandFrame
         └── puertos rb_app::SioTransport, rb_app::Clock
               └── EspSioTransport (rb_esp_sio, nuevo)  C: ISR de COMMAND + UART RX (UART2 propuesta); C++: adaptador
```

- **`rb_esp_sio` (nuevo, infraestructura):**
  - una parte en C con la ISR y el UART;
  - el adaptador C++ `EspSioTransport`, que implementa `SioTransport`;
  - **recibe solo el `SioAssignment` devuelto por `validate_for_sio()`**, nunca el `BoardProfile`: con la puerta
    H4 sin firmar no se puede construir, y en `ReceiveOnly` no recibe el TX;
  - no conoce el dominio.
- **Monitor de escucha en `rb_sio`:**
  - decodifica con `CommandFrame` (checksum, ID y comando) y registra las marcas de tiempo en un **búfer circular
    estático**;
  - no responde: **no usa `SioSession`** en H5, porque la sesión transmite ACK, NAK y COMPLETE;
  - respeta la regla de dependencias (ADR-002): `rb_sio` depende de los puertos de `rb_application`, no al revés.
- **`rb_board`:** entregable de código de H4, **ya integrado** (PR #18 y #19):
  - `SioPins` en `BoardProfile` y `sioGateApproved` (falso mientras H4 no esté firmado);
  - `check_sio_pins()`: solo reglas (COMMAND y RX como entradas, 34–39 admitidos; TX declarado como salida fuera
    de 34–39, obligatorio en `ReceiveTransmit`);
  - `validate_for_sio()`: autoriza el uso físico y devuelve `SioAssignment`, con `tx = kNoPin` en `ReceiveOnly`;
  - perfil FN32ROV-1.3: COMMAND 39, RX 33 y TX 21.
- **Kconfig:**
  - `RB_SIO_LISTEN` (nuevo, desactivado por defecto) habilita la escucha;
  - **no existe ninguna opción de TX hasta H6**.
- **Comprobación del artefacto (`tools/ci/firmware_package.py`):**
  - hoy rechaza `uart_driver_install`, `uart_set_pin` y `gpio_*`;
  - H5 deberá permitirlos **solo** en `components/rb_esp_sio/` y agregar una comprobación de que GPIO21 no aparece
    como salida mientras no exista la opción de TX.

## 6. Pruebas, en orden

1. **Host** (CMake + CTest + Unity, sin ESP32):
   - lógica pura del seguimiento de COMMAND, a partir de marcas de tiempo simuladas: filtro de pulsos de 80 ns, bajo
     permanente, trama incompleta y trama demasiado larga;
   - el monitor con `ScriptedSioTransport` y `FakeClock`: checksum válido o inválido, `Timeout` y `TransportError`.
2. **ESP32 sin Atari**, con autorización:
   - el firmware arranca;
   - `gpio_dump_io_configuration` muestra solo GPIO33 (entrada, con o sin pull-up según lo decidido) y GPIO39
     (entrada con interrupción) entre los pines del SIO, y **GPIO21 sin configurar**;
   - el nivel de IO33 en reposo se compara con la línea base de M5.
3. **Atari falso (I-3), con autorización:**
   - adaptador USB-serie de **3,3 V** en J1: pin 5 (DATA OUT), pin 7 (COMMAND) y GND;
   - las entradas del 74LS07 aceptan 3,3 V (VIH ≥ 2,0 V) y el adaptador solo debe absorber 0,2 mA en bajo;
   - placa por USB y Atari **desconectado**;
   - se verifican la decodificación y la tolerancia de ±5 % en la velocidad.
4. **Atari real, pasivo** (después de aprobar H4):
   - arranque del Atari con la consulta a D1;
   - el analizador mide velocidad, t0 y t1, que se registran en `docs/mediciones/sio-tiempos.md` [A] (§9.3);
   - RetroBridge **no responde**.

## 7. Lo que H5 no hace

- No configura GPIO21, GPIO22, GPIO26, GPIO27 ni GPIO25.
- No transmite ACK, NAK, COMPLETE ni datos.
- No usa ADC durante la escucha.
- No usa `SioSession` ni lee imágenes en respuesta a comandos.

## 8. Pendientes de verificación antes de implementar

| # | Pendiente | Cómo |
|---|---|---|
| P1 | H4 aprobado (checklist) | `h4-puerta-sio.md` §7 |
| P2 | `esp_timer_get_time`, el cambio de tipo de interrupción y la notificación son seguros desde la ISR en IRAM | **Condicionado (2026-10-25):** `esp_timer_get_time` y `xTaskNotifyFromISR` sí; `gpio_set_intr_type` no. Elegir (a), (b) o (c) de §3 en la revisión de H5 |
| P3 | `UART_PIN_NO_CHANGE` no enruta U2TXD; efecto de `uart_set_pin` sobre el pull-up de RX | **Condicionado (2026-10-25):** TX sin asignar no se enruta [D]; v6.1 no activa el pull-up de RX, así que hace falta `gpio_pullup_en(33)` explícito si se decide (P4). Pendiente en el equipo: `gpio_dump_io_configuration` |
| P4 | Nivel en reposo y flanco de subida de IO33 sin configurar; necesidad y tipo de pull-up | Osciloscopio (M4, M5); decisión en la revisión de H5 |
| P8 | UART definitiva (UART2 propuesta, o UART1) | Revisión de H5 |
| P5 | Velocidad real NTSC/PAL y su tolerancia | Analizador (M5) |
| P6 | Umbral del filtro de COMMAND | Medición de t0 y de la duración de COMMAND (M5) |
| P7 | Aprobación del cambio `CONFIG_UART_ISR_IN_IRAM=y` | **Confirmado técnicamente (2026-10-25)**: la opción existe y su alcance está en §2. Falta la aprobación del cambio de `sdkconfig` en la revisión de H5 |
