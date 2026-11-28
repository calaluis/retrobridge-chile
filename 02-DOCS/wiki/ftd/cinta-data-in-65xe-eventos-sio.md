---
type: design
title: Registro de eventos SIO para la Issue #125 — diseño
description: Diseño, sin implementar, de una cola SPSC de eventos SIO con hora monotónica para correlacionar la orden C, las tramas cortas, COMMAND, el dueño de DATA IN y parked.
tags: [ftd, sio, diagnostico, issue-125]
timestamp: 2026-11-28T00:00:00Z
topic: ftd
---

# Registro de eventos SIO para la Issue #125 — diseño

> **Documento de diseño.** No hay cola, pruebas ni cambios de firmware. Publicado con la autorización del
> responsable para pedir una revisión independiente (PR #124). No autoriza implementar, flashear ni fusionar.
> La Issue #125 sigue abierta; H3, H4 y GPIO36 quedan fuera de alcance.

## 1. Para qué

La medición con `3863461` (FTD `cinta-data-in-65xe.md`) dio contadores acumulados: `cortas 16`,
`command_pegado 4803`, `ordenes 1`. Sin hora, no se pueden atribuir a la orden C ni relacionar entre sí.
Este diseño registra **eventos con hora monotónica** para leer la secuencia alrededor de la pulsación C.

**No se da por demostrado** que las 16 tramas cortas vengan de `parked_`. `EspSioUart::read()` devuelve 0
bytes cuando la línea está aparcada, lo que *puede* contar como trama corta, pero en la captura no consta
una cesión de DATA IN. Las causas siguen abiertas.

## 2. Qué no cambia

- El protocolo SIO, los plazos, los reintentos, las decisiones de error y el árbitro de DATA IN.
- El árbitro no se modifica: sus transiciones se observan desde el bucle, comparando antes y después de
  `step()`.
- `SioLineTransport` y `MenuSioTransport` reciben un punto de registro opcional (`nullptr` por defecto).
  Sin él, el código sigue exactamente el camino actual.
- La tarea SIO no imprime nada nuevo: solo escribe en RAM.

## 3. Formato binario del evento (16 bytes)

Cuatro palabras de 32 bits, alineadas a 4 bytes. El productor escribe cada palabra con un almacenamiento
de 32 bits simple; la publicación la hace `head` (§4).

| Palabra | Bits | Campo | Unidad y rango |
|---|---|---|---|
| `w0` | 31–0 | `t_lo` | bits 31–0 de `esp_timer_get_time()`, µs desde el arranque |
| `w1` | 31–16 | `t_hi` | bits 47–32 del mismo tiempo. 48 bits = ~8,9 años sin vuelta |
| `w1` | 15–8 | `kind` | tipo de evento, tabla siguiente. 0 no se usa |
| `w1` | 7–0 | `a` | primer valor, 0–255 |
| `w2` | 31–0 | `b` | segundo valor, 0–2³²−1 |
| `w3` | 31–0 | `seq` | número de evento intentado, incluidos los descartados. Un salto en `seq` muestra una pérdida |

El tiempo se toma **una vez** por evento, en el productor, antes de escribir. Todos los eventos usan el
mismo reloj monotónico (`esp_timer`), así que el orden por `t` es el orden real dentro de la tarea SIO.

| `kind` | Evento | Dónde | `a` | `b` |
|---|---|---|---|---|
| 1 | Trama completa | `SioLineTransport` | dispositivo (byte 0) | orden \| AUX1<<8 \| AUX2<<16 \| checksum<<24 |
| 2 | Trama corta | `SioLineTransport` | bytes recibidos, 0–4 | hasta 4 bytes recibidos, el primero en los bits 7–0; el resto 0 |
| 3 | COMMAND pegado: inicio de racha | `SioLineTransport` | 0 | 0 |
| 4 | COMMAND pegado: fin de racha | `SioLineTransport` | 0 | repeticiones de la racha, satura en 2³²−1 |
| 5 | Orden de menú recibida | `MenuSioTransport` | tecla (AUX1) | AUX2 (secuencia y fila) \| orden<<8 |
| 6 | Respuesta de menú | `MenuSioTransport` | 0 completa, 1 cortada, 2 ACK fallido | µs desde el fin del ACK, satura |
| 7 | Cambio de dueño de DATA IN | bucle, tras `step()` | dueño nuevo (`DataInOwner`, 0–6) | dueño anterior \| `DataInChange`<<8 |
| 8 | Cambio de `parked` | bucle, tras cada fase | 0 no, 1 sí | 0 |
| 9 | Cambio de MOTOR | bucle | 0 off, 1 on | 0 |
| 10 | SIO +5V, cambio eléctrico | `SioPowerWatch` | 0 bajo, 1 alto | mV medidos, 0–6000 |

**Rachas de COMMAND pegado:** una racha empieza en el primer `stuckLows` tras una trama sin pegado y
termina en la siguiente trama sin pegado. Así 4803 repeticiones son 2 eventos, no 4803.

**Separación eléctrica (kind 10):** el evento dice **«SIO +5V bajo/alto»** y su voltaje, según los umbrales
de `SioPowerWatch` (bajo: < 1000 mV durante 100 ms; alto: ≥ 4000 mV). Nunca dice «Atari apagado». El
interruptor del 65XE es físico; su posición solo la informa el responsable, y ni un botón de la interfaz
ni el voltaje la prueban.

## 4. Sincronización SPSC

- **Un productor:** la tarea SIO (`rb_sio_menu`, núcleo 1). **Un consumidor:** la tarea de diagnóstico (§6).
- **Contadores:** `head` y `tail` son `std::atomic<uint32_t>` de recorrido libre (dan la vuelta en 2³²).
  `N = 128`, potencia de dos; el índice es `c & (N−1)`. La ocupación es `head − tail` en aritmética módulo
  2³², siempre entre 0 y N.
- **Cada contador tiene un solo escritor:** `head` solo lo escribe el productor; `tail`, solo el
  consumidor. No hay lectura-modificación-escritura atómica.

Productor (`push`):
1. `h = head.load(relaxed)` — solo él lo escribe.
2. `t = tail.load(acquire)` — ve las lecturas del consumidor sobre el hueco que libera.
3. Si `h − t == N`: cola llena. `dropped.store(dropped.load(relaxed) + 1, release)` (satura en 2³²−1),
   `seq` avanza, y se descarta **el evento nuevo**. Nunca se sobrescribe un evento sin leer.
4. Si no: escribir `w0..w3` en `slot[h & (N−1)]` con almacenamientos simples de 32 bits.
5. `head.store(h + 1, release)` — publica el hueco ya escrito.

Consumidor (`pop`):
1. `t = tail.load(relaxed)` — solo él lo escribe.
2. `h = head.load(acquire)` — ve el hueco que el productor escribió antes del `release`.
3. Mientras `t ≠ h`: copiar `slot[t & (N−1)]` a una variable local, y luego `tail.store(t + 1, release)`.
   El `release` va **después** de la copia, para que el productor no reutilice el hueco mientras se lee.
4. `dropped` se lee con `acquire` en cada vaciado.

En Xtensa, *acquire* es `l32i` seguido de `memw`, y *release* es `memw` seguido de `s32i`. Es el mismo patrón
que `data_in_handover.hpp` y los contadores de `3863461`, y la misma suposición sobre el orden de memoria
entre núcleos del ESP32. El control de §5 comprueba lo que emite el compilador, no la teoría.

## 5. Control del código objeto Xtensa

Hoy `check_atomic_calls.py` busca nombres de rutinas de lock o de libatomic. Para la cola se añade un perfil
`events` sobre su objeto, que exige:

1. **`push` sin ninguna llamada ni salto fuera de la función.** Se rechaza en su cuerpo:
   - llamadas directas `call0`, `call4`, `call8`, `call12`;
   - llamadas indirectas `callx0`, `callx4`, `callx8`, `callx12`;
   - saltos indirectos `jx`, y `ret`/`retw` hacia otra función (cola de llamada);
   - un `j` o un `l32r` con reubicación (`R_XTENSA_SLOT0_OP`, `R_XTENSA_ASM_EXPAND`, `R_XTENSA_32`) a un
     símbolo que no sea la propia función ni los datos de la cola;
   - `syscall`, `simcall`, `break`, `waiti`, `rsil` (cambio de interrupciones) y los accesos a registros
     especiales `rsr`/`wsr`/`xsr` salvo los que el compilador use para el tiempo, que se listan aparte.
2. **`push` existe como función aparte** en su `.cpp`, para poder revisarla. Si el compilador la integra en
   el llamador, el control falla en vez de pasar en vacío.
3. **El tiempo:** `esp_timer_get_time()` es una llamada. Por eso `push` recibe el tiempo como argumento, y el
   llamador lo toma; el control solo exige que `push` no llame a nada.
4. **Orden de memoria:** `release` en `head` y `tail` (`memw` + `s32i`), `acquire` en sus cargas cruzadas
   (`l32i` + `memw`), y ninguna referencia a `__atomic_*`, `__sync_*` ni secciones críticas en el objeto.
5. **Controles negativos:** un listado sintético por cada forma rechazada debe hacer fallar el perfil, y
   uno limpio debe pasarlo.

**Límite:** el control lee el código objeto de una compilación. No prueba el comportamiento en tiempo de
ejecución ni cubre lo que no esté en el objeto revisado.

## 6. Consumidor: núcleo, prioridad y límites

| Parámetro | Valor | Por qué |
|---|---|---|
| Tarea | `rb_sio_diag` | aparte de la tarea SIO |
| Núcleo | 0 | la tarea SIO está en el núcleo 1 |
| Prioridad | `tskIDLE_PRIORITY + 1` | por debajo de `rb_drive_save` y `rb_net` (+2) y de la casetera (4 y 5) |
| Pila | 3072 B | una línea de texto local, sin recursión |
| Periodo | 200 ms (`vTaskDelay`) | nunca espera a la tarea SIO |
| Máximo por despertar | 16 eventos | acota el uso de la consola |
| Longitud de línea | ≤ 96 caracteres | una línea por evento |

- **Coste de impresión:** a 115200 baudios, 96 caracteres son ~8,3 ms; 16 líneas son ~133 ms como máximo por
  despertar. El ritmo sostenido es ≤ 80 eventos/s. Si llegan más, la cola se llena y los descartes se cuentan.
- **Interferencia con SIO:** el consumidor corre en el otro núcleo y no comparte variables con la tarea SIO,
  salvo la cola. El único recurso común es la salida de la consola: la tarea SIO ya escribe sus líneas
  `SIO diag`/`DATA IN diag` existentes, y podría esperar a que termine **una** línea del consumidor
  (~8,3 ms). Para acotarlo, el consumidor imprime una línea por llamada, nunca un bloque.
- **`sio eventos`** en la consola muestra los eventos pendientes y el contador de descartes. Usa el mismo
  `pop`; mientras la tarea de diagnóstico existe, la orden solo muestra el contador.
- **Riesgo residual:** esa espera de una línea en la tarea SIO no queda eliminada, solo acotada. Se mide con
  los máximos de fase de `SioLoopDiag`, y la respuesta de menú debe seguir en ~504 ms.

## 7. Correlación

Con el mismo reloj se puede leer una secuencia como esta: orden de menú (kind 5, tecla C) a t₀, después
tramas cortas (2) o rachas de COMMAND pegado (3, 4), y cambios de `parked` (8) o de dueño (7) si los hay.
La hora de la pulsación C la informa el responsable; es una referencia, no una medición.

## 8. Criterios de aceptación (para la implementación futura)

**Pruebas RED→GREEN en el host:**
1. Cola: orden FIFO y vuelta de los contadores, también cerca de 2³². Con la cola llena se descarta el
   evento nuevo, `dropped` sube, `seq` salta y nada se sobrescribe.
2. Formato: cada `kind` se codifica y se decodifica con los campos y rangos de §3, incluidos los valores
   saturados.
3. Equivalencia de `SioLineTransport`: con y sin punto de registro, los mismos resultados, estadísticas y
   bytes, en estos casos: trama completa, trama corta, COMMAND pegado, y COMMAND pegado seguido de
   recuperación.
4. Equivalencia de `MenuSioTransport`: con y sin punto de registro, los mismos bytes enviados, en el mismo
   orden y bloques, para una respuesta completa, un ACK fallido y un bloque fallido. Además, la orden
   recibida se registra con su tecla y su secuencia.
5. Árbitro: el ayudante que observa las transiciones llama a `step()` una sola vez y devuelve el mismo
   cambio. Registra un evento solo cuando cambian el dueño o `parked`.
6. Rachas: N repeticiones seguidas de COMMAND pegado dan exactamente 2 eventos, el fin con N.
7. Eléctrico: el kind 10 usa los umbrales de `SioPowerWatch` y nunca se registra como apagado del equipo.

**Controles en la CI:**
8. El perfil `events` de §5 pasa sobre el objeto real del ESP32, y sus controles negativos fallan.
9. CI 6/6, `ctest` completo, layering y notices en OK.

**Medición:**
10. En el banco, con autorización aparte: los máximos de fase y la respuesta de menú siguen en el rango de
    `3863461`.

**Límites declarados:**
11. La cola no es una instantánea de todo el sistema y puede perder eventos; `dropped` y `seq` lo dicen.
12. La hora de la pulsación C solo la da el responsable.

**Puertas:** revisión independiente de este diseño, luego de las pruebas y el código. Flasheo solo con
autorización expresa del responsable. Sin merge.
