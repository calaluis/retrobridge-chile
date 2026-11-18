---
type: decisions
title: Decisiones del método ASDD
description: Registro de solo anexado de las decisiones tomadas en los bucles ASDD y la tarea en que se tomaron.
topic: sdd
---

# Decisiones del método ASDD

Registro de solo anexado. Cada decisión indica la tarea y quién la tomó. El detalle técnico vive en `docs/adr/`;
aquí solo queda el resumen y el enlace. Las decisiones del arnés siguen en [`wiki/harness/decisions.md`](../harness/decisions.md).

## Pantalla principal del menú (Issue #40, PR #41)

Referencia de las decisiones del producto: [ADR-017](../../../docs/adr/0017-menu-de-arranque.md), enmienda 1.
Alcance: [Issue #40](https://github.com/calaluis/retrobridge-chile/issues/40).

| # | Fecha | Decisión | Tarea | Procedencia |
|---|---|---|---|---|
| M40-1 | 2026-11-17 | Terminal delgado: el programa 6502 envía cada tecla con `$5A` `K` y el host dibuja la pantalla entera. Reemplaza el comando `M` y el catálogo dentro del disco. | T1, T4 | Propuesta de Claude en el plan; aprobada con la spec. ADR-017, enmienda 1, puntos 1 y 2. |
| M40-2 | 2026-11-18 | AUX2 numera cada tecla; si `SIOV` repite AUX1 y AUX2, el host reenvía la respuesta sin aplicar la tecla otra vez. Un redibujo (`$FF`) o una trama descartada tras `FF` borra esa memoria. | T4 | Hallazgo «should-fix» del revisor de tier 1; corrección de Claude. ADR-017, enmienda 1, punto 1. |
| M40-3 | 2026-11-17 | D1 queda asignada y sigue sirviendo el menú hasta `A`; D2 a D8 se montan enseguida por el buzón. | T3 | Propuesta de Claude en el plan; aprobada con la spec. ADR-017, enmienda 1, punto 3. |
| M40-4 | 2026-11-17 | TNFS (fuentes 2 a 8) y `E` quedan pendientes y lo dicen en pantalla. `--sources` admite hasta 7 nombres, `U` desmonta y C: muestra «SIN CINTA». | T2, T3 | Decisión del usuario (Q1 TNFS pendiente; Q2 a Q4 aceptadas). ADR-017, enmienda 1, punto 4. |
| M40-5 | 2026-11-18 | La línea de mensajes va debajo del pie de teclas, en la pantalla principal y en la lista. | T1 | Pedido del usuario en la revisión humana; la lista se alineó por coherencia (Claude). ADR-017, enmienda 1, consecuencias. |

## Fuentes TNFS de solo lectura (menu-tnfs, Issue #41)

Plan: `wiki/sdd/plans/menu-tnfs.md`. M41-1 a M41-7 los escribe T014; estas filas son las decisiones tomadas al implementar.

| # | Fecha | Decisión | Tarea | Procedencia |
|---|---|---|---|---|
| M41-8 | 2026-11-18 | Tras reconectar, el cliente TNFS repite la operación entera desde el principio: LSEEK no está entre las órdenes permitidas. `ReadSink::begin()` se llama otra vez y la lectura vuelve al desplazamiento 0; un listado llama a `DirVisitor::restart()`. | T005 | Propuesta de Claude al implementar. |
| M41-9 | 2026-11-18 | La espera por intento que anuncia el servidor tiene un techo de 5 s (`kMaxWaitUs`). EAGAIN espera lo que pide, pero nunca más que una espera de intento, y como mucho 10 veces por orden. Se aparta del plan, que no ponía techo. | T005 | Hallazgos de seguridad del revisor de tier 1 (rondas 1 y 2); el techo de 5 s lo decidió el responsable. |
| M41-10 | 2026-11-18 | La cancelación corta las esperas de READDIR, STAT y READ en tramos de 100 ms; `stat()` acepta un `CancelCheck` opcional. OPEN y OPENDIR se esperan hasta el final, para conocer su número y cerrarlo siempre. | T005 | Hallazgos del revisor (rondas 1 y 2); corrección de Claude. |
| M41-11 | 2026-11-18 | Si el MOUNT de la reconexión falla o es rechazado, el resultado es SinRespuesta. Un fallo al recibir que no sea un tiempo agotado (por ejemplo «puerto inalcanzable») termina el intento al momento. | T005 | Hallazgo del revisor (ronda 1); el segundo punto se mantuvo a propósito: no hay nadie escuchando. |
| M41-12 | 2026-11-18 | Lectura: 3 respuestas OK seguidas sin datos dan MalFormada; recibir más datos de los pedidos, o EOF antes del tamaño que dio STAT, también. | T005 | Propuesta de Claude al implementar. |
| M41-13 | 2026-11-18 | El cliente no reserva memoria: el listado y la lectura entregan por `DirVisitor` y `ReadSink`. El progreso sale de `begin(size)` y de cada `data(offset, bytes)`, sin una llamada aparte. El reloj es `TnfsClock`, propio, porque las capas no dejan a `rb_net_tnfs` usar `rb/app`. | T005 | Propuesta de Claude al implementar; pregunta del revisor sobre la interfaz, respondida así. |
