---
type: design
title: Registro de eventos SIO para la Issue #125 — diseño
description: Diseño, sin implementar, de una cola SPSC de eventos SIO con hora monotónica para correlacionar la orden C, las tramas cortas, las esperas de COMMAND, el dueño de DATA IN y parked.
tags: [ftd, sio, diagnostico, issue-125]
timestamp: 2026-11-28T00:00:00Z
topic: ftd
---

# Registro de eventos SIO para la Issue #125 — diseño

> **Documento de diseño, revisión 2.** No hay cola, pruebas ni cambios de firmware. Lo publico con la
> autorización del responsable para pedir una revisión independiente (PR #124). No autoriza implementar,
> flashear ni fusionar. La Issue #125 sigue abierta. H3, H4 y GPIO36 quedan fuera de alcance.
>
> **Cambios respecto de `3fa06b6`**, por la revisión de ese HEAD:
> - H1: las tramas cortas llevan el estado de recepción (§3.2).
> - H2: las rachas se definen como esperas de liberación vencidas, con su causa de cierre (§3.3).
> - M1: el límite del control estático y el presupuesto de latencia (§5.3, §7).
> - M2: un consumidor que no bloquea la consola, con una cota comprobable (§6).
> - M3: un evento de estado completo e intervalos incompletos (§3.4).

## 1. Para qué

La medición con `3863461` (FTD `cinta-data-in-65xe.md`) solo dio contadores acumulados: `cortas 16`,
`command_pegado 4803` y `ordenes 1`. Sin la hora de cada evento no se pueden atribuir a la orden C ni
relacionar entre sí. Este diseño registra **eventos con hora monotónica** para poder leer la secuencia
alrededor de la pulsación C.

**No se da por demostrado** que las 16 tramas cortas vengan de `parked_`. `EspSioUart::read()` devuelve 0
bytes cuando la línea está aparcada, y eso *puede* contar como trama corta. Pero en la captura no consta
ninguna cesión de DATA IN. Las causas siguen abiertas, y el evento de trama corta (§3.2) lleva el estado
para distinguirlas.

## 2. Qué no cambia

- No cambian el protocolo SIO, los plazos, los reintentos, las decisiones de error ni el árbitro de DATA IN.
- El árbitro no se modifica. Sus transiciones se observan desde el bucle, comparando el estado antes y
  después de `step()`.
- `SioLineTransport` y `MenuSioTransport` reciben un punto de registro opcional, `nullptr` por defecto. Sin
  él, el código sigue exactamente el camino actual.
- La tarea SIO no imprime nada nuevo: solo escribe en RAM.

## 3. Eventos

### 3.1 Formato binario (16 bytes)

Cada evento ocupa cuatro palabras de 32 bits, alineadas a 4 bytes. El productor escribe cada palabra con un
almacenamiento simple de 32 bits. La publicación la hace `head` (§4).

| Palabra | Bits | Campo | Unidad y rango |
|---|---|---|---|
| `w0` | 31–0 | `t_lo` | bits 31–0 de `esp_timer_get_time()`, en µs desde el arranque |
| `w1` | 31–16 | `t_hi` | bits 47–32 del mismo tiempo. Con 48 bits no da la vuelta en ~8,9 años |
| `w1` | 15–8 | `kind` | tipo de evento (§3.5). El 0 no se usa |
| `w1` | 7–0 | `a` | primer valor, 0–255 |
| `w2` | 31–0 | `b` | segundo valor, 0–2³²−1 |
| `w3` | 31–0 | `seq` | número de evento intentado, contando también los descartados. Un salto en `seq` indica una pérdida |

El llamador toma el tiempo **una vez** por evento, antes del `push` (§5). Todos los eventos usan el mismo
reloj monotónico (`esp_timer`). Por eso, dentro de la tarea SIO, ordenar por `t` da el orden real.

### 3.2 Estado de recepción en cada ventana (H1)

Los eventos que cierran una ventana COMMAND (tipos 2, 3 y 5) llevan en `a` el estado `E`, tomado en ese
instante:

| Bits de `E` | Campo |
|---|---|
| 2–0 | bytes recibidos en la ventana, de 0 a 5 |
| 3 | `parked()` de la UART en el instante del cierre |
| 6–4 | dueño de DATA IN (`DataInOwner`, de 0 a 6), según la última lectura del bucle |
| 7 | nivel de COMMAND al cierre: 1 liberado, 0 afirmado |

`parked()` y el nivel de COMMAND los lee la tarea SIO en el mismo punto del código. El dueño es el que el
bucle registró en su último paso, porque `SioLineTransport` no ve el árbitro. Así cada trama corta se
puede interpretar sola, aunque se hayan perdido transiciones. **Límite:** el dueño puede ir un paso de
bucle por detrás del cambio real. El evento de dueño (tipo 8) da el instante exacto.

### 3.3 Ventanas COMMAND y rachas de esperas vencidas (H2)

Una **ventana** empieza cuando `waitCommandLevel(true, …)` ve COMMAND afirmado. Termina de una de estas
cuatro formas, que hoy ya existen en `SioLineTransport::waitCommandFrame`:

| Fin de la ventana | Condición actual | Contador actual | Evento |
|---|---|---|---|
| Trama completa | 5 bytes y COMMAND liberado antes de `kReleaseWaitUs` (2 ms) | `frames` | tipo 1 |
| Trama corta | menos de 5 bytes y COMMAND liberado | `shortFrames` | tipo 2 |
| Espera de liberación vencida | COMMAND no se liberó en 2 ms después de la lectura; luego `recover()` | `stuckLows` | tipo 3 o se acumula en la racha |
| Error de lectura | `read()` falló; luego `recover()` | `receiveErrors` | tipo 5 |

**Qué es una racha:** una serie de ventanas **seguidas** que terminan todas en espera de liberación vencida.
- **Apertura:** la primera ventana que vence después de una que no venció. Emite el evento tipo 3, con `E`
  y los bytes leídos.
- **Siguientes ventanas vencidas:** no emiten evento. Solo suben el contador de la racha.
- **Cierre:** lo produce la primera ventana que termina de otra forma, o una espera de afirmación de
  COMMAND que vence (`waitCommandLevel(true)` sin COMMAND afirmado). Emite el evento tipo 4, con la causa
  del cierre, el número de esperas vencidas y el nivel de COMMAND en ese momento.

**Qué no es una racha:** no prueba que COMMAND estuviera bajo de forma continua. Cada espera vencida es
una ventana de lectura (hasta `kFrameReadUs`, 6 ms) más una espera de liberación de 2 ms. Entre una y otra
hay un `recover()` y un nuevo `waitCommandLevel(true)`, y COMMAND pudo subir y bajar en ese intervalo sin
que se registre. La racha cuenta intentos, no tiempo eléctrico. No se debe describir como una sola
pulsación larga.

### 3.4 Estado completo e intervalos incompletos (M3)

El **evento de estado** (tipo 12) lleva:
- en `a`: los indicadores `parked` (bit 0), MOTOR (bit 1), cinta en C (bit 2), puertas (bit 3), +5V bajo
  según `SioPowerWatch` (bit 4) y nivel de COMMAND (bit 5);
- en `b`: el dueño de DATA IN (bits 7–0) y los mV medidos del SIO +5V (bits 23–8; 0 si no hay medida).

**Cuándo se emite:**
1. Al arrancar la tarea SIO, como primer evento.
2. Con cada evento eléctrico de `SioPowerWatch` (tipo 11).
3. Después de una pérdida. Cuando el productor descarta un evento, activa `needState`. En el siguiente
   `push` que tenga hueco, emite primero el estado y luego el evento.
4. Cada 5 s, para que una captura que empieza tarde tenga un punto de referencia.

**Regla de interpretación (consumidor):**
- Todo lo anterior al primer estado recibido es **incompleto**.
- Si `seq` salta o `dropped` sube, el intervalo desde el último evento visto hasta el siguiente estado es
  **incompleto**. El consumidor lo marca con una línea `INCOMPLETO desde seq X hasta seq Y`.
- En un intervalo incompleto no se infiere ninguna transición. Solo valen los eventos que trae.

### 3.5 Tipos de evento

| `kind` | Evento | Dónde | `a` | `b` |
|---|---|---|---|---|
| 1 | Trama completa | `SioLineTransport` | dispositivo (byte 0) | orden \| AUX1<<8 \| AUX2<<16 \| checksum<<24 |
| 2 | Trama corta | `SioLineTransport` | `E` (§3.2) | hasta 4 bytes leídos, el primero en los bits 7–0; el resto en 0 |
| 3 | Espera de liberación vencida: apertura de racha | `SioLineTransport` | `E` | hasta 4 bytes leídos |
| 4 | Fin de racha | `SioLineTransport` | causa en los bits 2–0 (1 trama completa, 2 trama corta, 3 error de lectura, 4 COMMAND no afirmado) \| COMMAND al cierre << 7 | esperas vencidas en la racha; satura en 2³²−1 |
| 5 | Error de lectura | `SioLineTransport` | `E` | 0 |
| 6 | Orden de menú recibida | `MenuSioTransport` | tecla (AUX1) | AUX2 (secuencia y fila) \| orden<<8 |
| 7 | Respuesta de menú | `MenuSioTransport` | 0 completa, 1 cortada, 2 ACK fallido | µs desde el fin del ACK; satura |
| 8 | Cambio de dueño de DATA IN | bucle, tras `step()` | dueño nuevo (0–6) | dueño anterior \| `DataInChange`<<8 |
| 9 | Cambio de `parked` | bucle, tras cada fase | 0 no, 1 sí | 0 |
| 10 | Cambio de MOTOR | bucle | 0 off, 1 on | 0 |
| 11 | SIO +5V: cambio eléctrico | `SioPowerWatch` | 0 bajo, 1 alto | mV medidos, 0–6000 |
| 12 | Estado completo | bucle (§3.4) | indicadores | dueño \| mV<<8 |

**Separación eléctrica (tipos 11 y 12):** el evento dice **«SIO +5V bajo/alto»** y su voltaje, según los
umbrales de `SioPowerWatch`: bajo es menos de 1000 mV durante 100 ms, alto es 4000 mV o más. Nunca dice
«Atari apagado». El interruptor del 65XE es físico, y su posición solo la informa el responsable. Ni un
botón de la interfaz ni el voltaje la prueban.

## 4. Sincronización SPSC

- **Un productor:** la tarea SIO (`rb_sio_menu`, en el núcleo 1). **Un consumidor:** la tarea de
  diagnóstico (§6).
- **Contadores:** `head` y `tail` son `std::atomic<uint32_t>` de recorrido libre, que dan la vuelta en 2³².
  `N = 128`, potencia de dos, y el índice es `c & (N−1)`. La ocupación es `head − tail` en aritmética
  módulo 2³², y siempre queda entre 0 y N.
- **Cada contador tiene un solo escritor:** `head` solo lo escribe el productor y `tail` solo el
  consumidor. No hay lectura-modificación-escritura atómica.

Productor (`push`):
1. `h = head.load(relaxed)`: solo él lo escribe.
2. `t = tail.load(acquire)`: así ve terminadas las lecturas del consumidor sobre el hueco que liberó.
3. Si `h − t == N`, la cola está llena:
   - `dropped.store(dropped.load(relaxed) + 1, release)`, saturado en 2³²−1;
   - `seq` avanza y se activa `needState`;
   - se descarta **el evento nuevo**, y nunca se sobrescribe uno sin leer.
4. Si hay hueco, escribe `w0..w3` en `slot[h & (N−1)]` con almacenamientos simples de 32 bits.
5. `head.store(h + 1, release)`: publica el hueco ya escrito.

Consumidor (`pop`):
1. `t = tail.load(relaxed)`: solo él lo escribe.
2. `h = head.load(acquire)`: así ve el hueco que el productor escribió antes de su `release`.
3. Mientras `t ≠ h`, copia `slot[t & (N−1)]` a una variable local y después hace `tail.store(t + 1,
   release)`. El `release` va **después** de la copia, para que el productor no reutilice el hueco mientras
   se lee.
4. `dropped` se lee con `acquire` en cada vaciado.

En Xtensa, *acquire* es `l32i` seguido de `memw`, y *release* es `memw` seguido de `s32i`. Es el mismo patrón
de `data_in_handover.hpp` y de los contadores de `3863461`, y parte de la misma suposición sobre el orden de
memoria entre los núcleos del ESP32. El control de §5 comprueba lo que emite el compilador, no la teoría.

## 5. Control del código objeto Xtensa

### 5.1 Perfil `events`

`check_atomic_calls.py` gana un perfil `events` sobre el objeto de la cola. Exige:

1. **Que `push` no haga ninguna llamada ni salto fuera de la función.** En su cuerpo se rechaza:
   - las llamadas directas `call0`, `call4`, `call8` y `call12`;
   - las llamadas indirectas `callx0`, `callx4`, `callx8` y `callx12`;
   - el salto indirecto `jx`, y `ret`/`retw` hacia otra función (llamada en cola);
   - un `j` o un `l32r` con reubicación (`R_XTENSA_SLOT0_OP`, `R_XTENSA_ASM_EXPAND`, `R_XTENSA_32`) a un
     símbolo que no sea la propia función ni los datos de la cola;
   - `syscall`, `simcall`, `break`, `waiti` y `rsil`, y los registros especiales `rsr`/`wsr`/`xsr`.
2. **Que `push` exista como función aparte** en su `.cpp`. Si el compilador la integra en el llamador, el
   control falla, en vez de pasar en vacío.
3. **Orden de memoria:** *release* en `head` y `tail` (`memw` + `s32i`), *acquire* en sus cargas cruzadas
   (`l32i` + `memw`), y ninguna referencia a `__atomic_*`, `__sync_*` ni a secciones críticas en el objeto.
4. **Controles negativos:** cada forma rechazada tiene un listado sintético que debe hacer fallar el
   perfil, y un listado limpio debe pasarlo.

### 5.2 El tiempo

`esp_timer_get_time()` es una llamada. Por eso `push` recibe el tiempo como argumento, y es el llamador
quien lo toma. El control solo exige que `push` no llame a nada.

### 5.3 Límite del control estático (M1)

El perfil `events` cubre **solo** la función `push` de una compilación. No demuestra que toda la ruta de
captura sea no bloqueante. Esa ruta también incluye tomar la hora, leer `parked()` y COMMAND, armar las
palabras y la llamada misma. Eso lo cubren la equivalencia (§8, criterios 3 y 4) y el presupuesto de
latencia (§7). Ninguno de los dos es una prueba formal.

## 6. Consumidor: núcleo, prioridad y salida no bloqueante (M2)

| Parámetro | Valor | Por qué |
|---|---|---|
| Tarea | `rb_sio_diag` | aparte de la tarea SIO |
| Núcleo | 0 | la tarea SIO está en el núcleo 1 (`kTaskCore = 1`) |
| Prioridad | `tskIDLE_PRIORITY + 1` | por debajo de `rb_drive_save` y `rb_net` (+2) y de la casetera (4 y 5) |
| Pila | 3072 B | una línea de texto local, sin recursión |
| Periodo | 200 ms (`vTaskDelay`) | nunca espera a la tarea SIO |
| Máximo por despertar | 16 eventos | acota el trabajo de cada despertar |

**El problema:** la consola es `esp_console_new_repl_stdio`, sobre la UART0. La tarea SIO ya escribe ahí
sus líneas `SIO diag` y `DATA IN diag`, y una escritura en stdout espera a que termine la que está en curso.
Los ~8,3 ms por línea a 115200 baudios son tiempo de transmisión, **no** una cota de espera.

**Política no bloqueante:** el consumidor no escribe líneas enteras en stdout. Hace esto:
1. Arma la línea en un búfer local.
2. La envía en trozos de **16 bytes** como máximo, y cada trozo solo cuando la FIFO de TX de la UART0 tiene
   como mucho 16 bytes pendientes.
3. Si la FIFO tiene más, el consumidor cede el procesador (`vTaskDelay(1)`) y vuelve a intentarlo. Nunca
   espera dentro de la escritura.

**Cota que resulta:** cuando la tarea SIO quiere escribir, encuentra como mucho 16 bytes pendientes más un
trozo de 16 bytes del consumidor. Son 32 bytes a 115200 baudios, unos **2,8 ms**, además de lo que ya cuesta
su propia línea.
- **A verificar en la implementación:** que la versión de ESP-IDF del proyecto expone la ocupación de la
  FIFO de TX de la UART0 sin instalar otro driver, por ejemplo con `uart_ll_get_txfifo_len`. Que la
  escritura de un trozo de 16 bytes con hueco en la FIFO no espera. Si no se puede, esta política no vale,
  y la alternativa es que el consumidor imprima **solo con la orden `sio eventos`**, nunca de forma
  automática.

**Prueba de saturación:** con la cola llena y llegando eventos sin parar, deben cumplirse tres cosas:
- el consumidor respeta los 16 eventos por despertar;
- `dropped` sube;
- los máximos de fase SIO (`SioLoopDiag`) y la latencia COMMAND→respuesta se mantienen dentro de §7.

**`sio eventos`** muestra el contador de descartes y, si la tarea de diagnóstico no existe, los eventos
pendientes. Usa el mismo `pop` y la misma política de escritura.

## 7. Presupuesto de latencia (M1, M2)

| Medida | Referencia con `3863461` | Límite con eventos |
|---|---|---|
| Coste de una captura en la tarea SIO (hora + estado + `push`) | — | ≤ 10 µs (máximo medido) |
| Latencia COMMAND→respuesta, máxima | 290 µs | ≤ referencia + 20 µs |
| Respuesta de menú, completa | 504 ms | ≤ referencia + 1 ms |
| Máximos de fase de `SioLoopDiag` | los de `3863461` | sin ninguna fase «lenta» nueva |
| Espera de la tarea SIO por la consola (§6) | — | ≤ 2,8 ms calculados; se mide |

La captura que cierra una ventana (tipos 1–5) ocurre **antes** del ACK. Por eso su coste se suma a la
latencia COMMAND→respuesta, y su límite es el más estricto. En el banco, el coste de una captura se mide con
un máximo atómico, en una compilación de medición. Esa medición no forma parte del firmware de
diagnóstico normal.

## 8. Correlación

Con el mismo reloj se puede leer una secuencia como esta:
1. Primero, un estado completo (tipo 12) como punto de referencia.
2. La orden de menú con la tecla C (tipo 6) en t₀.
3. Después, tramas cortas (tipo 2), cada una con `parked`, dueño y COMMAND, o rachas de esperas vencidas
   (tipos 3 y 4) con su causa de cierre.
4. Los cambios de `parked` (tipo 9) o de dueño (tipo 8), si los hay.

La hora de la pulsación C la informa el responsable. Es una referencia, no una medición.

## 9. Criterios de aceptación (para la implementación futura)

**Pruebas RED→GREEN en el host:**
1. **Cola:** orden FIFO y vuelta de los contadores, también cerca de 2³². Con la cola llena se descarta el
   evento nuevo, `dropped` sube, `seq` salta, se activa `needState`, y nada se sobrescribe.
2. **Formato:** cada `kind` se codifica y se decodifica con los campos y rangos de §3, incluidos `E`, los
   indicadores y los valores saturados.
3. **Equivalencia de `SioLineTransport`:** con y sin punto de registro, devuelve los mismos resultados,
   estadísticas y bytes. Casos: trama completa, trama corta, espera de liberación vencida, error de
   lectura, y una racha cerrada por cada una de sus cuatro causas.
4. **Equivalencia de `MenuSioTransport`:** con y sin punto de registro, envía los mismos bytes, en el mismo
   orden y en los mismos bloques. Casos: respuesta completa, ACK fallido y bloque fallido. La orden
   recibida se registra con su tecla y su secuencia.
5. **Árbitro:** el ayudante que observa las transiciones llama a `step()` una sola vez y devuelve el mismo
   cambio. Solo registra un evento cuando cambian el dueño o `parked`.
6. **Rachas:** N ventanas vencidas seguidas dan exactamente un tipo 3 y un tipo 4 con N y la causa de
   cierre correcta.
7. **Estado de recepción:** una trama corta con la UART aparcada lleva `parked = 1`, y una sin aparcar lleva
   `parked = 0`, con el dueño del último paso.
8. **Estado completo e intervalos incompletos:** el arranque emite un estado como primer evento. Tras una
   pérdida, el siguiente `push` con hueco emite primero el estado. El decodificador marca como incompleto
   todo lo anterior al primer estado (caso de arranque tardío) y el tramo entre una pérdida y el siguiente
   estado.
9. **Eléctrico:** los tipos 11 y 12 usan los umbrales de `SioPowerWatch`, y nunca registran un apagado del
   equipo.
10. **Consumidor:** con una FIFO simulada llena, no escribe y cede el procesador. Con hueco, escribe trozos
    de 16 bytes o menos y nunca más de 16 eventos por despertar.

**Controles en la CI:**
11. El perfil `events` (§5) pasa sobre el objeto real del ESP32, y sus controles negativos fallan.
12. CI 6/6, `ctest` completo, layering y notices en OK.

**Medición en el banco** (con autorización aparte):
13. Se cumplen los límites de §7, también con la prueba de saturación.

**Límites declarados:**
14. La cola no es una instantánea de todo el sistema y puede perder eventos. `dropped`, `seq` y los
    intervalos incompletos lo muestran.
15. El control estático cubre solo `push`. El resto de la ruta se cubre con equivalencia y medición.
16. Una racha cuenta esperas vencidas, no tiempo eléctrico con COMMAND bajo.
17. La hora de la pulsación C solo la da el responsable.

**Puertas:** primero la revisión independiente de este diseño, después la de las pruebas y el código.
Flasheo solo con autorización expresa del responsable. Sin merge.
