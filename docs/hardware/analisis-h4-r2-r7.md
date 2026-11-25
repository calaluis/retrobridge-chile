# Análisis de R2 y R7 (H4): diagnóstico y matriz de evidencia

> **Análisis. No es una validación eléctrica, no acepta ningún riesgo y no aprueba H4.**
>
> - Estado vigente, sin cambios: R1 aceptado (sin validación eléctrica); **R2 no aceptado**; R6 aceptado solo con
>   la FujiNet por USB; **R7 no aceptado**; M3a aprobada por aceptación de riesgo; **H4 sin aprobar**;
>   `kTapeOutput = false`; T9b bloqueado (`docs/mediciones/riesgos-h4-2026-11-25.md`).
> - Este documento no autoriza conexiones, flasheos ni mediciones. Las propuestas de §4 y §5 son solo propuestas.
> - Rama `analisis/h4-r2-r7`, desde `main` `36c4d43` (PR #109, #110, #111 y #112 fusionados).

Marcas de evidencia:

| Marca | Significa |
|---|---|
| **Verificado** | Comprobado en este trabajo: prueba de host ejecutada, chequeo estático o lectura directa del código o del esquema ya documentado |
| **Inferido** | Razonamiento o cálculo a partir de hojas de datos o del esquema. No es una medición |
| **Pendiente** | Solo se resuelve en hardware (C8a, C8b, M1, C9…), con autorización aparte |

## 1. R2: DATA IN (GPIO21) bloqueado en bajo

**El riesgo.** El 74LS07 no invierte. GPIO21 alto deja la línea libre; GPIO21 en bajo lleva DATA IN a bajo y
bloquea el bus para todos los periféricos (`h4-puerta-sio.md` §3.2).

### 1.1 Qué hace el software (revisión de `EspDataIn` y del arbitraje)

| Situación | Comportamiento del código | Evidencia |
|---|---|---|
| Reposo entre transmisiones | GPIO21 es UART2 TX, que reposa en alto (marca) | Verificado: `test_configure_gives_the_pin_to_the_uart` |
| Tramo FSK por el RMT | `init_level = 1` y `eot_level = 1`: el RMT empieza y termina en marca | Verificado: `test_a_run_starts_and_ends_at_mark` |
| Fin del tramo | El RMT avisa por `on_trans_done`; `pump()` devuelve el pin a UART2 | Verificado: `test_normal_end_comes_from_the_rmt_and_disables_once` |
| Cancelación (`stop()`, bajada de MOTOR) | `rmt_disable` una sola vez, borrado del canal, pin a UART2 | Verificado: `test_cancel_disables_once_and_gives_the_pin_back` |
| El RMT no suelta el pin | El canal se conserva, UART2 no se monta encima, `pump()` y `release()` reintentan | Verificado: 4 pruebas `test_a_failed_disable…` y `test_a_failed_delete…` |
| UART2 no recupera el pin | Intenta `gpio_reset_pin(21)` (salida apagada). El driver queda en fallo y rechaza todo hasta `recover()`. **Si el reset también falla, el pin puede seguir sin liberar**: el driver lo sabe (fallo), pero la línea no se libera | Verificado en el modelo: `test_release_failure_*`, `test_a_failed_safe_state_is_still_a_fault` |
| Modo compartido con el disco | El pin vuelve al disco solo si la matriz GPIO lee U2TXD con la salida activa | Verificado: `test_give_back_routes_the_pin_to_uart2_and_checks_the_matrix` y 2 más |
| Arbitraje disco/casetera | Una toma fallida, un plazo vencido o perder las puertas devuelven la línea al disco o la sueltan | Verificado: 37 pruebas en `test_data_in_arbiter.cpp` y `test_data_in_handover.cpp` |
| Llamadas que podrían fijar el pin | `gpio_set_level`, `gpio_set_direction`, `uart_set_line_inverse` y `break` no se usan sobre GPIO21. El empaquetador cuenta cada llamada GPIO, UART y RMT por archivo | Verificado: `git grep` y `CASSETTE_SIO_CALLS` / `FORBIDDEN_CALLS` en `tools/ci/firmware_package.py` |

### 1.2 Brecha encontrada y corregida

**`release()` en modo propio informaba «liberado» aunque `gpio_reset_pin()` fallara.**

- Antes: borraba UART2, ignoraba el resultado del reset, devolvía `true` y escribía «DATA IN released». La matriz GPIO
  podía seguir con U2TXD en GPIO21 y la salida activa.
- Alcance: el modo propio solo lo usa el banco C8 (`main/bench_c8.cpp`). El banco comprueba la matriz desde fuera
  (paso 8), así que el fallo se habría visto allí. El defecto era del contrato del método, no de la casetera en uso.
- Ahora: si el reset falla, `release()` devuelve `false`, el driver sigue activo y en fallo (rechaza todo) y un
  `release()` posterior lo reintenta.
- Prueba: `test_release_with_a_failed_reset_is_not_released`. RED observado antes del cambio (línea 274,
  «Expected FALSE Was TRUE»); GREEN después. El número de llamadas `gpio_reset_pin` no cambia (sigue en 2).
- **Revisión del PR #113, H1.** Tras ese `release()` incompleto, UART2 ya está borrado. Antes de la segunda
  corrección, tres llamadas daban la línea por disponible sin UART2: `configure()` devolvía `true` sin reinstalarlo,
  y `recover()` y `giveBack()` limpiaban el fallo porque `uart_set_pin()` funciona sin el driver. Ahora un estado
  propio (`uartDeleted_`) lo impide: `configure()` devuelve `false`, `recover()` mantiene el fallo, `giveBack()`
  devuelve `Lost`, y solo un `release()` completo cierra el estado, sin borrar UART2 otra vez. Después,
  `configure()` lo instala de nuevo. Pruebas: 4 nuevas (`test_configure_after_a_failed_release_…`,
  `test_recover_after_a_failed_release_…`, `test_give_back_after_a_failed_release_…`,
  `test_a_completed_release_then_configure_…`), RED antes y GREEN después.
- **Límite:** esto corrige lo que el **software** cree del pin. Si `gpio_reset_pin()` falla en la placa, la línea
  puede seguir sin liberar hasta que un `release()` funcione. El nivel eléctrico real no lo prueba el host.

### 1.3 Lo que las pruebas de host no pueden demostrar

El modelo de ESP-IDF del host (`test/host/fakes/idf/fake_idf.hpp`) registra **quién tiene el pin**, no niveles
eléctricos ni tiempos. Por eso quedan fuera:

| Escenario | Estado | Qué lo resolvería |
|---|---|---|
| Nivel del pin durante el instante entre `rmt_disable` y `uart_set_pin` | Inferido: el RMT reposa en su nivel inicial (1). No hay prueba | C8a (matriz) y C8b (nivel) |
| `panic` o reinicio a mitad de un tramo FSK | Inferido: tras el reinicio, GPIO21 es entrada y el 74LS07 ve su entrada sin conducir. El nivel durante el propio `panic` no se conoce | Pendiente: C8b, con un criterio de parada |
| Entrada del 74LS07 (U4) sin conducir | Inferido: una entrada TTL abierta suele leerse en alto. El esquema no muestra pull-up en ese nodo | Pendiente: medición |
| Nivel real del pin 3 con el Atari | Pendiente | C8b |
| `recover()` en modo propio | Verificado: no lee la matriz (decisión de diseño; el banco la lee desde fuera) | — |

**R2 no queda resuelto con pruebas de host.** Estas pruebas reducen el riesgo de un fallo de software. La
comprobación eléctrica de DATA IN (C8b) sigue pendiente.

## 2. R7: alimentación del 74LS07 (VCC_BUF)

### 2.1 Recorrido de la alimentación (`h4-puerta-sio.md` §4)

```text
USB_5V ──D4 (PMEG2010)──┐
                        ├── VCC ──Q2 (DMP2035U, canal P)── VCC_BUF ── U3, U4 (74LS07)
SIO_5V ──D5 (PMEG2010)──┘            ▲
 (pin 10 del Atari)            Q3 (DMG2302U) conduce con +3V3 (S5 encendido)
```

| Dato | Valor | Marca |
|---|---|---|
| VCC recomendado del 74LS07 | 4,75–5,25 V | Verificado: hoja de datos de TI, ya citada en `docs/fuentes.md` |
| Comportamiento del 74LS07 bajo 4,75 V | **No especificado** por la hoja de datos. No se infiere ninguna forma de fallo | — |
| VCC_BUF estimado | ≈ 4,6–4,7 V (5 V menos unos 0,3–0,4 V del Schottky y de Q2) | Inferido: estimación previa de `h4-puerta-sio.md` §4. No es una medición |
| Tensión de USB_5V en esta unidad | Desconocida: depende del puerto y del cable | Pendiente |
| Caída real de D4 | Depende de la corriente. El repositorio no cita la curva del PMEG2010 | Pendiente: hoja de datos y medición |
| Caída en Q2 | Depende de su resistencia en conducción y de la corriente. No citada en el repositorio | Pendiente |

### 2.2 USB frente a SIO

- **Solo USB (el caso aceptado para R6):** VCC = USB_5V menos la caída de D4. R7 depende del puerto USB, del cable y
  de la corriente total de la placa. **Inferido.**
- **USB y SIO a la vez:** alimenta la fuente de mayor tensión; los diodos impiden la retroalimentación. **Inferido**
  del esquema; M6 lo mediría.
- **Solo SIO:** no aceptado para R6 y pendiente de evaluación. Este análisis no lo trata.

### 2.3 Qué podría dejar VCC_BUF bajo 4,75 V (hipótesis, sin medir)

1. Un puerto o un cable USB que entregue menos de 5 V. **Inferido.**
2. Picos de corriente del ESP32 (arranque, Wi-Fi transmitiendo) que aumentan la caída en D4 durante el pico.
   **Inferido.** Un multímetro promedia y puede no verlos.
3. La corriente propia de los 74LS07 cuando sus salidas están en bajo (ICCL hasta 45 mA por circuito, peor caso de la
   hoja de datos). **Verificado** el límite; **pendiente** el consumo real.

### 2.4 Qué evidencia aporta cada medición

| Medición | Qué demuestra | Qué **no** demuestra |
|---|---|---|
| **M1** (multímetro, solo USB, sin Atari, en reposo) | La tensión estática de VCC y VCC_BUF en esta unidad, con este puerto y este cable | El comportamiento con carga ni durante los picos |
| **C9** (VCC_BUF y pin 10 en la configuración de prueba) | Que VCC_BUF se mantiene en el montaje real del ensayo | Por sí solo, nada de R2 |

**R7 sigue abierto.** Ningún cálculo de este documento sustituye a M1 ni a C9.

## 3. Matriz de evidencia

«Verificado» en las filas de software significa el **estado del driver** en el modelo del host. Ninguna fila
verifica el **nivel eléctrico** de GPIO21 ni del pin 3.

| # | Afirmación | Verificado | Inferido | Pendiente |
|---|---|---|---|---|
| 1 | Ninguna ruta de software deja GPIO21 como salida GPIO en bajo | ✔ (código, empaquetador, pruebas) | | |
| 2 | Toda cancelación o fallo **intenta** devolver el pin a UART2 o resetearlo, y si los dos fallan el driver queda en fallo y rechaza todo | ✔ (modelo del host: estado del driver) | | |
| 2b | Si fallan a la vez la vuelta a UART2 y el reset, el pin queda liberado | | | **No**: puede seguir sin liberar. Nivel real: C8a/C8b |
| 3 | `release()` solo dice «liberado» con el pin reseteado, y nada da la línea por disponible sin UART2 | ✔ (5 pruebas nuevas, RED→GREEN) | | |
| 4 | El RMT deja el pin en alto al terminar y al cancelar | ✔ (configuración) | ✔ (nivel tras la cancelación) | C8a, C8b |
| 5 | La matriz GPIO en la placa sigue el modelo del host | | | C8a |
| 6 | El pin 3 del SIO queda en alto tras transmitir | | | C8b |
| 7 | El 74LS07 suelta la línea con el ESP32 en reinicio | | ✔ | Medición |
| 8 | VCC_BUF ≥ 4,75 V solo con USB, en reposo | | ✔ (≈ 4,6–4,7 V: posiblemente **no**) | M1 |
| 9 | VCC_BUF ≥ 4,75 V con carga y picos | | | C9 |

## 4. Propuesta de verificación futura de R7 (no ejecutar)

**Propuesta. No está autorizada y no pide aprobación.** Cada etapa requiere la autorización escrita del responsable.

| Etapa | Montaje | Instrumento | Registro | Criterio |
|---|---|---|---|---|
| R7-a (= M1) | Solo USB, S5 encendido, **sin Atari**, cable SIO desconectado, firmware ya instalado sin cambios | Multímetro en VCC (cátodo de D4) y VCC_BUF (U3/U4, pin 14) | Valor, puerto USB y cable usados | Comparar con 4,75 V. Un valor menor confirma R7 en reposo |
| R7-b | Igual que R7-a, durante el arranque y, si el firmware instalado usa Wi-Fi, con Wi-Fi activo | Osciloscopio en VCC_BUF, captura del mínimo | Mínimo y duración de cada caída | Ninguna caída bajo 4,75 V; si la hay, registrar cuánto dura |
| R7-c (= C9) | Configuración del ensayo C8b, con su propia autorización | Igual que R7-b | Igual que R7-b | Igual que R7-b |

**Criterios de parada:** cualquier tensión fuera de 0–5,25 V, `brownout`, olor o calor anormal en U3, U4, D4 o Q2.

**Si R7-a o R7-b dan menos de 4,75 V:** la decisión (aceptar, cambiar de puerto o cable, o modificar la placa)
es del responsable. Este documento no la toma.

## 5. Riesgos residuales y condiciones para seguir

| Riesgo residual | Por qué sigue | Condición para seguir |
|---|---|---|
| Nivel eléctrico del pin 3 (R2) | El host no modela niveles | C8a hecho; luego C8b con autorización |
| Comportamiento durante `panic` o reinicio | No reproducible en el host | Criterio de parada en C8b; registro de cualquier reinicio |
| VCC_BUF bajo 4,75 V (R7) | Estimación ≈ 4,6–4,7 V, sin medir | R7-a; si pasa, R7-b |
| Requisito previo de C8b | `h4-c-casetera.md` §3.1 pide «C5 resuelto» (pico de MOTOR ≤ 3,3 V medido o protección validada). M3a se aprobó por **aceptación de R1**, no por medición | **Decisión del responsable:** si la aceptación de R1 basta para C5. Este análisis no lo interpreta |
| Paquete de banco | El banco C8 no tiene variante ni job desde el PR #110 | Especificación en `banco-c8a-aislado.md`; código y CI en PR aparte, con decisión |

## 6. Trazabilidad

- PR #109 (`8ca5d59`), #110 (`f6d3056`), #111 (`69204a0`) y #112 (`36c4d43`), todos en `main`.
- Esta rama: `analisis/h4-r2-r7`. Los commits se listan en el PR.
