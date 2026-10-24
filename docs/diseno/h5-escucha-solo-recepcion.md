# H5: diseño preliminar de la escucha SIO en modo solo recepción

> Estado: **PROPUESTA, NO APROBADA.** Planificación técnica, sin implementación. H5 empieza solo después de aprobar
> H4 (`docs/hardware/h4-puerta-sio.md`, checklist §7) y de una autorización explícita. Ningún punto de este
> documento autoriza a configurar GPIO, cambiar `sdkconfig` ni flashear.

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
    la errata GPIO-3.6 para los pads RTC [D].
  - Esa decisión se toma en H5, con las mediciones.
- **TX sin asignar:**
  - [P] Hay que verificar en la fuente de ESP-IDF v6.1 que dejar TX sin asignar (`UART_PIN_NO_CHANGE`) no enruta
    U2TXD a ningún pad.
  - En el equipo, `gpio_dump_io_configuration` debe mostrar que **GPIO21 no está conectado al TX de la UART
    elegida**, ni a ninguna otra salida, y que los pines por defecto de esa UART (17 para UART2, 10 para UART1) no se
    usaron.
- **ISR del UART en IRAM:**
  - ESP-IDF v6.1 tiene `CONFIG_UART_ISR_IN_IRAM` (desactivado por defecto) [D], y la arquitectura pide la ISR del UART
    en IRAM (§9.1) [A].
  - Activarlo es un cambio de `sdkconfig` que **se aprueba aparte** en H5.
  - `uart_driver_install` no admite `ESP_INTR_FLAG_IRAM` en sus parámetros [D]; la ubicación depende solo de ese
    Kconfig.
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

[P] Hay que confirmar en ESP-IDF v6.1 que `esp_timer_get_time`, el cambio de tipo de interrupción y la notificación
son seguros desde una ISR en IRAM, incluso con la caché deshabilitada.

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
  - no conoce el dominio.
- **Monitor de escucha en `rb_sio`:**
  - decodifica con `CommandFrame` (checksum, ID y comando) y registra las marcas de tiempo en un **búfer circular
    estático**;
  - no responde: **no usa `SioSession`** en H5, porque la sesión transmite ACK, NAK y COMPLETE;
  - respeta la regla de dependencias (ADR-002): `rb_sio` depende de los puertos de `rb_application`, no al revés.
- **`rb_board`:** se agrega `SioPins` al `BoardProfile` y reglas de validación. Es el entregable de código de H4,
  en un PR aparte:
  - COMMAND puede ser un pin 34–39;
  - RX debe admitir entrada;
  - TX debe admitir salida y no puede ser 34–39;
  - en modo solo recepción, TX debe quedar sin usar.
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
| P2 | `esp_timer_get_time`, el cambio de tipo de interrupción y la notificación son seguros desde la ISR en IRAM | Fuente y documentación de ESP-IDF v6.1 |
| P3 | `UART_PIN_NO_CHANGE` no enruta U2TXD; efecto de `uart_set_pin` sobre el pull-up de RX | Fuente de ESP-IDF v6.1 y `gpio_dump_io_configuration` |
| P4 | Nivel en reposo y flanco de subida de IO33 sin configurar; necesidad y tipo de pull-up | Osciloscopio (M4, M5); decisión en la revisión de H5 |
| P8 | UART definitiva (UART2 propuesta, o UART1) | Revisión de H5 |
| P5 | Velocidad real NTSC/PAL y su tolerancia | Analizador (M5) |
| P6 | Umbral del filtro de COMMAND | Medición de t0 y de la duración de COMMAND (M5) |
| P7 | Aprobación del cambio `CONFIG_UART_ISR_IN_IRAM=y` | Revisión de H5 |
