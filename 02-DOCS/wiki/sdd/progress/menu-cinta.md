---
type: progress
title: Progreso — menu-cinta
description: Registro por tarea de menu-cinta desde T9a: qué se decidió, con qué pruebas y qué queda.
tags: [sdd, progress, menu, cassette]
topic: sdd
slug: menu-cinta
---

# Progreso — menu-cinta

Plan: [`plans/menu-cinta.md`](../plans/menu-cinta.md). T1–T7 se hicieron en el PR #85. El banco T8 está en
`02-DOCS/raw/2026-11-23-banco-menu-cinta-t8.md`. Este registro empieza en T9a.

## T9 dividida en T9a y T9b (2026-11-24, solo documentos)

- **Origen.** El responsable revisó el firmware FujiNet original y la FN32ROV-1.3-Q24. Las conclusiones:
  - lo funcional de MOTOR y DATA IN tiene evidencia;
  - los niveles eléctricos de GPIO36 y del pin 3 no la tienen.

  La revisión independiente aprobó desarrollar T9 sin salida física (T9a) y dejar el banco (T9b) con sus
  requisitos eléctricos.
- **Plan, §2.4.** El traspaso entre la tarea SIO y la de la casetera es asíncrono:
  - estados `TakePending`, `ReleasePending` (esperando) y `Fault`;
  - confirmaciones con secuencia, plazos de 20 ms para tomar y según la cola de TX para soltar, y una recuperación
    que nunca deja dos transmisores en GPIO21;
  - la salida cambia solo con el hardware libre: TX de UART2 vacío, sin pasada RMT y sin fallo;
  - UART2 se instala una sola vez, así que la tarea SIO no reserva memoria. Las reservas del RMT siguen en la
    tarea de la casetera, como ya está documentado.
- **Plan, §6.** T9a.1–T9a.7 cubren los cinco requisitos de T9 (salida en marcha, `tapeEnded` real, `A` sin disco en
  D1, D1 tras el arranque de cinta y la línea de ayuda). Requisitos previos de T9b:
  - M3a medida;
  - protección de GPIO36 validada;
  - H4-C con C5, C8a, C8b y C9;
  - la aprobación de H4 y M3a que exige el empaquetador;
  - un PR aparte para `kTapeOutput = true`.

  Ninguna aceptación de riesgo los reemplaza.
- **`h4-c-casetera.md`.**
  - C8 se separa en C8a (matriz GPIO y control de UART2/RMT, por USB y sin Atari) y C8b (nivel del pin 3 con el
    Atari, después de C5).
  - C9 no queda cubierto por `brownout=0` ni por el pin 10.
  - H4-C no reemplaza la aprobación de H4 del empaquetador.
- **Spec.** Una nota dice que T9a no es evidencia física de AC9–AC12. Se añadió una pregunta abierta sobre qué
  contesta D1 después de un arranque de cinta, con su recomendación.
- **Sin cambios:** `puertas.json`, ADR-012, `firmware_package.py` y `kTapeOutput = false`. No hubo flasheo ni
  conexiones.

## Revisión 2 del PR #99 (2026-11-24)

- **Política de búfer (decisión 3 del plan).**
  - Problema: el disco instala UART2 sin anillo de TX, así que 512 bytes a 600 baudios bloquearían a la casetera
    unos 6,4 s.
  - Decisión: un anillo de 2048 bytes al arrancar. El disco conserva su `write` + `uart_wait_tx_done`. La cinta
    escribe sin bloquear y con 32 bytes en vuelo como mucho, así que `release` queda en ≤ 0,6 s.
  - Alternativa, si el banco del disco muestra otro tiempo: sin anillo, escribiendo con la FIFO vacía.
- **Recuperación de `Fault`.** `Disk` se confirma solo con el pin devuelto a UART2 **y** la matriz GPIO verificada
  (la misma `route()` del banco C8). Si no se logra, el estado es `Released`: la línea queda suelta y el disco no
  reanuda. Se prueban las confirmaciones viejas o tardías y los fallos de recuperación.
- **`tapeEnded`.** Se publica verdadero solo con el reproductor al final, el TX vacío y sin pasada FSK.
- **C8b.** Se define un ensayo acotado (`h4-c-casetera.md` §3.1): un paquete de banco con las puertas cerradas,
  un patrón fijo de 2 s activado por consola, MOTOR comprobado en 0 V y medido con osciloscopio, después de C5. No
  aprueba H4 ni M3a.

## Revisión 3 del PR #99 (2026-11-24)

- **Bytes en vuelo.** `uart_get_tx_buffer_free_size()` mide solo el anillo, y la interrupción pasa hasta 128 bytes
  a la FIFO. Ahora la cinta cuenta anillo + FIFO + registro de desplazamiento, leídos en el orden que sobrestima, y
  escribe solo por debajo de 32 en total. `kReleaseTimeoutUs` = 600 ms. La prueba incluye un mutante que mira solo
  el anillo y deja 160 bytes en vuelo.
- **`DiskLine.resume()`.** Devuelve `Done` o `Failed`. `Done` solo con 19 200 baudios (±2 %) y 8N1 verificados y la
  entrada vaciada. `Failed` deja el árbitro en `DiskFault`: D1 en silencio, ningún evento de recuperación,
  reintento cada 100 ms. Riesgo nuevo R12.
- **Revisión 4.** La alternativa sin anillo escribía hasta 64 bytes y contradecía la cota de 32. Ahora escribe
  como mucho 32 bytes con la FIFO vacía: misma cota, mismo `kReleaseTimeoutUs` = 600 ms.

## Siguiente

Revisar este PR de documentos. Los PR de código de T9a empiezan solo después de aprobarlo, uno por tarea.
