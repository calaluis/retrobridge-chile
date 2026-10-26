# H4: separación de la puerta en H4-L (escucha) y H4-T (transmisión)

> **PROPUESTA NO APROBADA.** Revisión documental de los criterios de H4 para permitir una validación progresiva del
> firmware RetroBridge en la FujiNet FN32ROV-1.3-Q24 existente.
>
> - **Ninguna de las dos puertas queda aprobada** por este documento.
> - **`sioGateApproved = false`** se mantiene, y **no se modifica código** (`BoardProfile`, `SioPins`,
>   `validate_for_sio()`) ni el procedimiento oficial M0–M6 (`procedimiento-medicion-pasiva-sio.md`).
> - Mientras no se apruebe, sigue vigente `h4-puerta-sio.md` §7 completo.

**Etiquetas:**
- **[H]** evidencia histórica del hardware (uso anterior con el firmware FujiNet, proyecto WSS);
- **[F]** prueba nueva del firmware RetroBridge (pendiente de ejecutar);
- **[P]** riesgo eléctrico o medición pendiente;
- **[E]** esquema oficial de la FN32ROV-1.3-Q24 (`fujinet-hardware`, commit `0e539b8`);
- **[I]** inferencia o propuesta.

## 1. Motivo

El checklist actual de H4 (`h4-puerta-sio.md` §7) es una única puerta: exige todas las mediciones M0–M6 antes de
**cualquier** uso del SIO. Mezcla dos cosas distintas:

1. **La condición del hardware al conectar la placa al Atari:** existe con cualquier firmware, y esta placa tiene
   años de uso con el 65XE y el 800XL [H].
2. **Lo que el firmware nuevo hace con los pines:** es lo que RetroBridge añade y lo que hay que demostrar.

Para la **escucha pasiva**, el firmware solo configura **entradas**. Manejar DATA IN, que puede bloquear el bus
(riesgo R2), solo ocurre al **transmitir**. Separar las puertas permite avanzar con la escucha sin rebajar lo que exige
la transmisión.

**Argumento central [I]:** en escucha pasiva, RetroBridge configura un **subconjunto estricto** de lo que el firmware
FujiNet original ya configuraba en esta misma placa: COMMAND (GPIO39) y DATA OUT (GPIO33) como entradas; **sin** la
salida DATA IN (GPIO21), sin MOTOR (GPIO36) y sin ADC. La exposición eléctrica que introduce el firmware no supera la
del uso histórico. **No** demuestra que el hardware cumpla sus especificaciones (§4).

## 2. Evidencia, separada por tipo

| Tipo | Contenido | Qué demuestra | Qué no demuestra |
|---|---|---|---|
| **[H] Histórica** | Uso de la FN32ROV-1.3 con el 65XE NTSC (128 KB) y el 800XL, también alimentada desde el SIO; cargas de CAS con **MOTOR activo durante 15 min 27 s** (G5-2 T5); funcionamiento con el firmware FujiNet, que maneja DATA IN | Compatibilidad funcional en esas condiciones | Cumplimiento eléctrico (niveles, corrientes, márgenes) ni ausencia de degradación |
| **[F] Firmware nuevo** | Configuración solo de entrada, ausencia de transmisión, diagnósticos internos y pruebas por USB sin Atari (§7) | Lo que RetroBridge hace con los pines | Nada sobre tensiones analógicas ni tiempos exactos del bus |
| **[P] Riesgos pendientes** | R1 (MOTOR en GPIO36), R3 (pull-up de IO33), R6 (corriente del pin 10), R7 (VCC_BUF), R12 (COMMAND bajo con el Atari apagado), y M0–M6 sin medir | — | — |

## 3. H4-L: escucha pasiva (requisitos propuestos)

H4-L autorizaría **solo** a configurar GPIO39 y GPIO33 como entradas y a recibir tramas, sin responder. Cada
requisito es verificable y se registra en `docs/mediciones/`.

| # | Requisito | Cómo se verifica | Sin Atari |
|---|---|---|---|
| L1 | **Solo entradas:** entre los pines del SIO, el firmware configura únicamente GPIO39 (entrada con interrupción) y GPIO33 (RX de la UART). GPIO21, 22, 25, 26, 27, 32, 35 y 36 quedan sin configurar | Revisión de código; `gpio_dump_io_configuration` en el equipo; comprobación del artefacto en CI | Sí |
| L2 | **Sin transmisión, por construcción** (§5): ningún camino del firmware puede configurar GPIO21 como salida, enrutar un TX de la UART ni ejecutar comandos de envío | Pruebas de host, `static_assert`, comprobación del artefacto y revisión | Sí |
| L3 | **La puerta se respeta en el equipo:** con la puerta de escucha cerrada, el firmware **rechaza** activar la escucha y no configura ningún pin del SIO | Prueba en el equipo (U5 en §7) | Sí |
| L4 | **Diagnósticos internos estables:** `rb_hw_diag` sin reinicios inesperados, brownouts ni pánicos durante las pruebas por USB | `diag status` y `diag report` | Sí |
| L5 | **Pruebas por USB sin Atari superadas** (§7, U1–U6) | Registro de cada prueba | Sí |
| L6 | **Decisión explícita sobre R1** (§4): aceptación documentada del riesgo preexistente **o** mitigación C aprobada y validada (`mitigacion-c-motor-pin8.md`) | Decisión firmada por el equipo | — |
| L7 | **Condiciones de conexión:** placa alimentada siempre por USB (R6), PC y Atari en la misma toma, FujiNet al final de la cadena si se usa la mitigación C, y orden de conexión definido | Procedimiento de la primera conexión, actualizado y aprobado | — |
| L8 | **Criterios de detención** para la primera conexión: cualquier salida en un GPIO del SIO, brownout o reinicio, calor u olor, comportamiento anómalo del Atari, o tramas recibidas con el Atari apagado | Registro | — |

**Lo que H4-L no autoriza:** transmitir, H6, la Meta B, ni considerar realizadas las mediciones M0–M6.

**Ajustes documentales que exigiría aprobar H4-L** (no se hacen ahora): `h4-puerta-sio.md` §7, un procedimiento de la
primera conexión de escucha y, en `procedimiento-medicion-pasiva-sio.md`, aclarar qué etapas dependen de H4-T.

## 4. R1 (MOTOR) en H4-L

- **La exposición de R1 no depende del firmware:** MOTOR llega directo a GPIO36 en el hardware [E]. RetroBridge no la
  aumenta ni la reduce: H4-L **no configura** GPIO36.
- **El funcionamiento histórico [H] no demuestra que GPIO36 cumpla** sus límites eléctricos (tensión prevista ≈ 4,7–4,9 V
  frente a ≈ 3,6 V, sin medir; `h4-puerta-sio.md` §3.4).
- Por eso L6 exige una **decisión explícita**: aceptar R1 como riesgo preexistente, con esa constancia por escrito,
  **o** usar la mitigación C. No es una conclusión automática.

## 5. Impedir la transmisión por construcción (propuesta para la implementación de H5)

| Capa | Medida propuesta [I] |
|---|---|
| Asignación | En `ReceiveOnly`, `validate_for_sio()` ya entrega `tx = kNoPin`. Además, el adaptador de escucha recibiría un tipo **sin campo de TX** (por ejemplo, una asignación de escucha con solo COMMAND y RX), para que TX no se pueda representar |
| Adaptador | El componente de escucha implementa **solo** `SioCommandSource` (sin `send()` ni `releaseDataLine()`) y llama a `uart_set_pin` con TX **`UART_PIN_NO_CHANGE`** de forma explícita |
| Compilación | `RB_SIO_LISTEN` desactivado por defecto y **ninguna opción de TX** en Kconfig hasta H4-T |
| Artefacto (CI) | `tools/ci/firmware_package.py`: `uart_*` y `gpio_*` permitidos solo en el componente de escucha; ningún símbolo de escritura de la UART del SIO (`uart_write_bytes`, `uart_tx_chars`) ni configuración de GPIO21 como salida |
| Consola | Sin comandos que envíen al bus; solo `sio listen` y `sio stats` |
| Pruebas | Pruebas de host de pasividad (ya existen: `static_assert` sobre `SioCommandSource` y 0 envíos) y una prueba nueva del tipo de asignación sin TX |

## 6. Representación de las dos puertas en el software (propuesta, sin implementar)

**Opción recomendada [I]:** sustituir el booleano por un **nivel de puerta** con tres valores, para que no existan
estados incoherentes (por ejemplo, transmisión aprobada sin escucha):

```
enum class SioGate : uint8_t { Closed, ListenApproved, TransmitApproved };
```

| `SioMode` pedido | Exige | Entrega |
|---|---|---|
| `ReceiveOnly` | `ListenApproved` o `TransmitApproved` | Asignación sin TX |
| `ReceiveTransmit` | `TransmitApproved` | Asignación con TX |

- **No debilita la autorización actual:** el valor por defecto de todos los perfiles sería `Closed`, equivalente al
  `sioGateApproved = false` de hoy, y `TransmitApproved` equivaldría a la puerta actual completa.
- Cualquier cambio de nivel en un perfil requeriría un PR específico, revisado y con la puerta correspondiente
  aprobada.
- **Alternativa:** dos booleanos (`sioListenApproved` y `sioTransmitApproved`) con una regla de validación que impida
  `transmit` sin `listen`. Es más simple de introducir, pero admite estados incoherentes que habría que rechazar.

## 7. Pruebas por USB y sin Atari, antes de cualquier conexión SIO

Todas requieren **flashear** y, salvo U1, **configurar entradas**: cada una necesita autorización expresa. Ninguna
conecta el SIO.

| # | Prueba | Criterio verificable |
|---|---|---|
| U1 | Línea base con el firmware actual (`rb_hw_diag`), sin escucha | Ya realizada en H4 (`h4-validacion-diagnostico-2026-10-24.md`) |
| U2 | **Lectura digital** de GPIO39, GPIO33 y GPIO36 como entradas, sin pull | GPIO39 en alto (R11) y GPIO36 en bajo (R2) [E]; GPIO33 se registra (sin pull en el esquema, R3). **Es una lectura lógica, no sustituye a una medición analógica** (M1, R7) |
| U3 | **Configuración** con la escucha activada | `gpio_dump_io_configuration`: solo GPIO39 y GPIO33 configurados entre los pines del SIO; GPIO21 sin configurar y sin TX enrutado |
| U4 | **Escucha en reposo** sin Atari | Sin tramas ni errores; COMMAND en alto; estadísticas a cero salvo los `Timeout` |
| U5 | **Puerta cerrada** | Con el perfil sin aprobar, el firmware rechaza la escucha y no configura ningún pin del SIO |
| U6 | **Estabilidad** de varias horas con la escucha activa | `diag status`: sin reinicios, brownouts ni pánicos; montón estable |

## 8. Estimaciones internas frente a M5

- Con el Atari conectado (después de H4-L), el firmware podrá **estimar** la duración de COMMAND, la velocidad del
  bus y la disponibilidad de los bytes al subir COMMAND (P9) a partir de sus propias marcas de tiempo [I].
- Son **estimaciones con la latencia de la ISR incluida**. Sirven para fijar umbrales provisionales (P6) y orientar
  D1, y se documentan **como estimaciones del firmware**.
- **No sustituyen a M5:** la velocidad, t0 y t1 para `sio-tiempos.md` y la Meta B siguen exigiendo un osciloscopio o
  un analizador con tolerancia a 5 V confirmada.

## 9. H4-T: transmisión (bloqueada)

H4-T autorizaría a manejar DATA IN (GPIO21) y responder al Atari (H6, H7). **Sigue bloqueada** hasta cumplir:

- **H4-L aprobada** y la escucha validada con el Atari;
- **todo el checklist actual** de `h4-puerta-sio.md` §7, incluidas M0–M6, con M3a o una mitigación de R1 aprobada y
  validada, y **M5 con instrumento**;
- el análisis de bloqueo del bus (R2): DATA IN liberada en reposo y tras cualquier error, comprobado en el equipo;
- las protecciones de transmisión en el firmware (liberar la línea siempre, sin transmitir fuera de una transacción
  aceptada) con sus pruebas.

## 10. Resumen de lo que se puede hacer sin instrumentos

| Sin instrumentos | Con instrumentos |
|---|---|
| L1–L5 y U1–U6 (con autorización para flashear) | Tensiones analógicas: VCC_BUF (R7), nivel real de MOTOR (R1), pin 10 |
| La decisión sobre R1 (L6) y la mitigación C con multímetro, si se elige | Picos y flancos (IO33, R3) |
| Estimaciones de tiempo del firmware (§8), después de H4-L | M5: velocidad, t0 y t1 para `sio-tiempos.md` |
| — | M6: corriente del pin 10. Todo lo necesario para H4-T |

## 11. Fuentes

- `h4-puerta-sio.md` (§2, §3.4, §5 y §7), `procedimiento-medicion-pasiva-sio.md`, `mitigacion-c-motor-pin8.md`.
- Diseño de H5: `docs/diseno/h5-escucha-solo-recepcion.md` (§1, §5, §7, §8 y §9).
- `rb_board` (`validate_for_sio()`, `SioAssignment`) y los componentes de host de H5.
- Registro histórico WSS G5-2 T5 (privado, no versionado).

No se consultó código de FujiNet (ADR-012). Sin fuentes externas nuevas.
