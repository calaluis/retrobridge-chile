---
type: spec
title: Spec — Pulso COMMAND breve con la cinta dueña de DATA IN (H2, issue #125)
description: QUÉ y POR QUÉ debe hacer RetroBridge cuando COMMAND baja y sube entre dos pasos del árbitro, con MOTOR apagado y DATA IN en la cinta. Sin técnica de implementación.
tags: [sdd, spec, firmware, sio, cassette, data-in, issue-125]
timestamp: 2026-11-28T06:00:00Z
topic: sdd
slug: h2-command-pulse
branch: test/issue-125-command-pulse
status: draft
---

# Spec — Pulso COMMAND breve con la cinta dueña de DATA IN (H2, issue #125)

**Estado:** borrador para revisión independiente. **No** está aprobada, no elige técnica y **no** resuelve
H2. Rama de investigación `test/issue-125-command-pulse`, a partir de `0644ad3`. No toca la PR #124 ni
`main`.

## Problema y por qué

`DataInArbiter::givesBack()` devuelve DATA IN al disco cuando, con MOTOR apagado, **el nivel** de COMMAND
está bajo en el momento del paso (regla 3). En el modelo de host (`test_command_pulse`, 23 casos), el
paso del árbitro llega después de `SioSession::step()`, que espera hasta 20 ms; con la UART aparcada,
cada lectura espera 6 ms y la liberación 2 ms. Un COMMAND de 8 ms o menos baja y sube dentro de esa
espera: cuando el árbitro mira, COMMAND ya está alto, la cinta conserva DATA IN y el disco sigue mudo.
Con 10 ms o más, el árbitro lo ve y devuelve la línea.

Es un riesgo **demostrado en el modelo**: 11 RED esperados, revisados de forma independiente en `0644ad3`.
**No** es una causa confirmada de «EL HOST NO RESPONDE» en el 65XE.

## ¿Qué pasa si no lo construimos?

- El flujo aceptado sigue funcionando: tras una carga, el responsable apaga y enciende el 65XE. El
  apagado visto (`atariOff`) devuelve DATA IN por otra vía (`test_power_off_seen_gives_back`, GREEN), y
  la variante F lo confirmó en una ejecución física.
- El riesgo queda en los casos sin apagado: un programa que, después de la cinta y con MOTOR apagado,
  habla con un disco mediante órdenes cortas. En el modelo, el disco no contestaría hasta que llegue el
  fin de cinta, la expulsión o el apagado. No se sabe si ese caso ocurre en el 65XE.
- Costo concreto hoy: **bajo y no medido**. Ese es el hallazgo, no se exagera.

## La alternativa más barata

No cambiar el firmware y documentar que, tras cargar una cinta, se apaga y enciende el 65XE (regla física
ya vigente). Cubre el uso aceptado. No cubre el caso de un programa que pase de cinta a disco sin
apagado, y deja abierta una diferencia entre la regla 3 escrita («COMMAND bajo devuelve») y lo que el
modelo hace con pulsos breves. Esta spec existe para que esa decisión se tome con el comportamiento
definido; elegir esta alternativa sigue siendo válido.

## Objetivos

1. Definir qué cuenta como «COMMAND visto» cuando la aserción ocurre y termina entre dos pasos.
2. Mantener la exclusión de DATA IN: UART2 nunca transmite mientras la cinta controla GPIO21.
3. Separar la detección de COMMAND de la recepción de la orden de 5 bytes.
4. Garantizar que la siguiente orden válida, tras recuperar DATA IN, se contesta normalmente.
5. No cambiar ninguna otra regla del árbitro.

## Usuarios y contexto

- El responsable, con un 65XE y la FujiNet FN32ROV-1.3-Q24, paquete `menu-sio-cinta`.
- Un programa del Atari que, con MOTOR apagado, envía órdenes SIO a un disco de RetroBridge mientras la
  cinta todavía es dueña de DATA IN.
- El revisor independiente, que necesita criterios comprobables en el host.

## Qué podrá hacer el usuario

Con una cinta montada en `C:` que ya tomó DATA IN, y con MOTOR apagado, el Atari podrá enviar una orden
SIO a un disco de RetroBridge. Si esa orden se pierde mientras la línea vuelve al disco, el Atari podrá
reenviarla, y una orden posterior será contestada. Condición previa: puertas M3a y H4 abiertas para el
paquete; sin ellas el árbitro nunca mueve la línea (regla 4) y nada de esto aplica. **M1:** esas puertas
valen solo bajo las excepciones y el perfil físico ya autorizados por el responsable (FN32ROV-1.3-Q24,
paquete `menu-sio-cinta`). **H4 no está aprobada en general**, y aprobar esta spec no amplía ninguna
aceptación.

## Qué resultado obtiene

- Referencia aprobada: el contrato escrito de `DataInArbiter` (reglas 1 a 5, `data_in_arbiter.hpp`) y los
  23 casos de `test_command_pulse` revisados en `0644ad3`. No hay referencia de pantalla para H2.
- Observable en el host: tras un COMMAND breve con MOTOR apagado, el dueño pasa por `ReleasePending` a
  `Disk`, `parkedNow` vuelve a 0 solo después de `Done`, y la siguiente trama de estado a D1 termina
  `Completed` con exactamente 7 bytes `41 43 00 FF E0 00` más checksum.

## Qué debe respetar

- **Cambio de contrato propuesto (C3):** H2 **amplía la regla 3** de `DataInArbiter`. Hoy, con MOTOR
  apagado, devuelve DATA IN el **nivel** COMMAND bajo en el paso. La ampliación añade una **aserción de
  COMMAND que ya terminó** antes del siguiente paso (B1). No es una corrección de aserciones de prueba:
  es comportamiento nuevo, y necesita aprobación antes de cualquier diseño. Las demás condiciones de
  devolución y las invariantes de seguridad quedan intactas.
- **R1, exclusión eléctrica:** UART2 no se reanuda mientras la casetera pueda manejar GPIO21. Con
  `release()` en `Pending`, `Failed` o vencido, el disco sigue aparcado (`ReleasePending`, `Fault`).
- Regla 1 (toma por estado), regla 2 (pausas de MOTOR conservan la cinta), regla 3 (apagado visto,
  expulsión, puertas cerradas, y con MOTOR apagado fin de cinta o COMMAND), regla 4 (sin puertas el disco
  nunca suelta) y regla 5 (toma rechazada vuelve al disco).
- Plazos y reintentos: `kTakeTimeoutUs` 60 ms, `kReleaseTimeoutUs` 600 ms, `kRetryUs` 100 ms, y los
  estados `TakePending`, `Fault`, `Released` y `DiskFault`.
- El fallo de `resume()` deja D1 mudo y reintenta (`DiskFault`).
- Decisiones del responsable: no se amplía ninguna aceptación; H3, H4 y GPIO36 quedan fuera.

## Cómo lo demuestra

Pruebas de host en `test_command_pulse` y el resto de `ctest`, ejecutadas en la CI. Las 11 RED esperadas
pasan a GREEN **sin debilitar** sus expectativas, las 12 GREEN siguen GREEN y se añaden los casos nuevos
de los criterios. Evidencia que queda: el registro de la CI y el informe de `ctest`. Lo que solo el
hardware puede mostrar se lista aparte y **no** se declara demostrado por el host.

## Comportamiento

### 1. Detección

- **B1.** Una aserción de COMMAND (paso a bajo) que ocurre mientras el dueño es `TakePending` o `Tape`
  cuenta como «COMMAND visto» en el siguiente paso del árbitro, **aunque al consultar ya esté alto**.
- **B2.** Varias aserciones entre dos pasos cuentan como una sola.
- **B3.** Una aserción anterior al comienzo de la propiedad actual (`TakePending` o `Tape`) no cuenta:
  no se arrastran detecciones viejas.
- **B4.** La detección se consume cuando comienza la devolución (`ReleaseRequested`). Las aserciones
  durante `ReleasePending`, `Fault`, `Released` o `DiskFault` no adelantan nada.
- **B5.** Con el dueño en `Disk`, el árbitro no reacciona a COMMAND (camino normal de la sesión SIO).
- **B6.** COMMAND sostenido, que sigue bajo al consultar, se comporta como hoy.
- **B7 (decisión D2 del responsable).** Una aserción cuenta como COMMAND visto solo si MOTOR estaba
  apagado **en el instante de la aserción y también en el paso** del árbitro que la procesa. Una aserción
  ocurrida con MOTOR encendido no devuelve DATA IN, aunque MOTOR ya esté apagado en el paso (regla 2:
  las pausas de MOTOR conservan la cinta). Esto cambia la B7 anterior, que evaluaba MOTOR solo en el
  paso.

### 2. Devolución segura

- **B8.** Con la regla 3 ampliada (C3), un COMMAND visto según B1 pide `release()`, igual que hoy lo
  pide el nivel COMMAND bajo. Las otras condiciones de la regla 3 no cambian.
- **B9.** UART2 se reanuda solo después de `release()` = `Done`. `Pending` mantiene `ReleasePending`, y
  `Failed` o el plazo vencido llevan a `Fault`, con el disco aparcado. `Released` deja la línea sin dueño.
  Esto ya rige y no cambia.

### 3. Orden SIO perdida

- **B10.** Detectar COMMAND no es recibir la orden. Los bytes que llegaron mientras la UART estaba
  aparcada no se leyeron y no se reconstruyen.
- **B11.** Una orden perdida no recibe ninguna respuesta: ni ACK, ni NAK, ni datos. Ningún registro
  afirma que se recuperó. Se cuenta según cómo terminó su ventana de COMMAND en
  `SioLineTransport::waitCommandFrame()`, y los tres resultados no se mezclan:

  | Resultado | Cuándo | Contador | Paso de `SioSession` |
  |---|---|---|---|
  | Trama corta | COMMAND se liberó con menos de 5 bytes leídos | `shortFrames` | `Ignored` |
  | COMMAND sostenido | COMMAND no se liberó dentro de `kReleaseWaitUs` | `stuckLows` | `TransportFailed` |
  | Error de recepción | falló `read()` | `receiveErrors` | `TransportFailed` |

  Con la UART aparcada, `EspSioUart::read()` no falla: espera, devuelve 0 bytes y cuenta
  `readsWhileParked`. Por eso, en el modelo, una orden perdida con la cinta dueña termina como trama
  corta o como COMMAND sostenido, no como error de recepción. **No** toda orden perdida es una trama
  corta, y el error de recepción sigue siendo un resultado distinto con la UART del disco activa.
- **B12.** Una trama que empezó con la UART aparcada y termina tras la reanudación se descarta. Nunca se
  contesta como si fuera completa.

### 4. Orden posterior

- **B13.** Tras `ToDisk`, la siguiente orden completa y válida (COMMAND bajo, 5 bytes y checksum
  correcto) a un disco montado se contesta igual que si la cinta nunca hubiera tomado la línea.

### 5. Compatibilidad

- **B14.** Las demás vías de devolución (fin de cinta con MOTOR apagado, puertas cerradas, expulsión y
  apagado visto) y la toma por estado no cambian. Tampoco los plazos, los reintentos ni los estados
  pendientes o de fallo.

## Criterios de aceptación

Todos en el host (H). Los marcados como HW **no** se dan por demostrados en el host.

1. (H, RED→GREEN) Dado `Tape` con MOTOR apagado, cuando hay un pulso de 1, 2, 3, 4, 6 u 8 ms en las 7
   fases de `test_command_pulse`, entonces el árbitro devuelve DATA IN: 0 pérdidas.
2. (H, RED→GREEN) Dados 10 pulsos simulados de 3 ms en secuencia fija cada 5, 10, 20 y 50 ms, entonces
   hay devolución, y **una sola** detección inicia `release()`. Las aserciones dentro de una misma
   ventana pueden agruparse (B2), y las posteriores a `ReleaseRequested` no cuentan (B4). No se exige
   contar cada pulso por separado.
3. (H, RED→GREEN) Dada una trama de estado a D1 por `SioSession::step()` con MOTOR apagado, entonces el
   paso termina `Ignored`, con 1 trama corta, 1 lectura aparcada y 0 bytes de TX (B10 y B11), y después
   hay devolución.
4. (H, nuevo) Dado un pulso ocurrido con el dueño en `Disk`, cuando luego la cinta toma la línea,
   entonces ese pulso no la devuelve (B3).
5. (H, nuevo) Dados pulsos durante `ReleasePending` con `release()` = `Pending`, entonces `parkedNow`
   sigue en 1 y no hay TX hasta `Done` (B4 y B9).
6. (H) Dado un pulso de 3 ms con MOTOR encendido, entonces la cinta conserva DATA IN (B7). Ya existe
   como GREEN y sigue GREEN. **Y** (H, prueba dedicada nueva, D2) dado un pulso que cae con MOTOR
   encendido, y MOTOR apagado antes del paso que lo procesa, entonces la cinta conserva DATA IN.
7. (H, prueba específica) Dada una trama que empieza con la UART aparcada y termina tras `ToDisk`,
   entonces no hay respuesta (B12). Si resulta GREEN sin cambios, se registra como **control** y no como
   corrección.
8. (H) Tras la devolución, la siguiente trama de estado a D1 termina `Completed` con exactamente 7 bytes
   `41 43 00 FF E0 00` más checksum (B13). Ya existe como GREEN y sigue GREEN.
9. (H, invariante) En todos los pasos de todos los casos, nunca `parkedNow` = 0 mientras el dueño no es
   `Disk`, y nunca hay TX de UART2 con el dueño distinto de `Disk`.
10. (H, sin regresiones) Las 12 GREEN actuales de `test_command_pulse`, el resto de `ctest`,
    `check_layering` y `check_notices` siguen OK. Ningún RED se debilita para pasar.
11. (HW, fuera del host) Ancho real de COMMAND en el 65XE, ruido o falsos flancos en la línea, el
    comportamiento eléctrico de GPIO21, si H2 ocurre físicamente y su relación con «EL HOST NO RESPONDE».
    Cualquier prueba física necesita una autorización expresa aparte.

## Fuera de alcance

- Elegir la técnica: interrupción GPIO, latch, muestreo más frecuente, reordenar `sioTask` u otra. Se
  evalúa después de aprobar esta spec.
- Cambios de firmware, de pruebas existentes, de la PR #124 o de `main`.
- Atribuir H2 a «EL HOST NO RESPONDE», o cerrar #125, #126 o #127.
- El punto 3 de la #125, la #126, la #127, H3 (`baud_`), H4 y GPIO36.
- Cambiar el flujo de uso aceptado: el ciclo físico de apagado y encendido tras una carga.
- Reconstruir o reproducir una orden perdida.

## Puntos a clarificar

- **pregunta abierta** — ¿Cuál es el ancho mínimo de una aserción para contar como COMMAND visto, y
  conviene un filtro de ruido? **Sin decidir.** Falta evidencia física del ancho real y del ruido de
  COMMAND en el 65XE. Las RED de 1 ms exigen contar 1 ms. Si el responsable aprobara un filtro más ancho,
  el criterio 1 se revisa **en esta spec** antes de implementar, nunca debilitando la prueba en silencio.
  Usar el mismo criterio de nivel que hoy usa el camino SIO es una opción a evaluar, no una decisión
  aprobada. **Decisión D1 del responsable (2026-11-28):** queda abierta hasta tener una medición física
  del ancho y del ruido de COMMAND, con su propia autorización expresa. Mientras siga abierta, esta spec
  no se aprueba para implementar.

- **suposición tomada** — En `TakePending`, un COMMAND visto según B1 pide la devolución como hoy lo hace
  COMMAND bajo. *Base:* `step()` ya llama a `givesBack()` en `TakePending`.
- **suposición tomada** — B12 puede estar ya cubierto. *Base (C4):* dos vaciados de la entrada UART.
  `SioLineTransport::waitCommandFrame()` llama a `flushInput()` antes de esperar COMMAND, y
  `EspSioUart::resume()` exige `flushed()` antes de quitar el aparcado. Es evidencia de la protección
  existente, **no** demostración de B12 en todas las condiciones: por ejemplo, una reanudación con
  COMMAND todavía bajo. Por eso el criterio 7 se conserva como prueba específica.
- **decisión diferida** — Que el Atari reintente y cuántas veces. *Base:* no se da por real ningún
  número de reintentos del Atari; los 10 pulsos son simulados.
- **área no formulable** — El ruido real de COMMAND en el 65XE durante una pausa de MOTOR. Se vuelve
  pregunta cuando haya una medición física autorizada.

## Objeción registrada

El riesgo es real en el modelo, pero puede no existir en el uso: el flujo aceptado apaga el 65XE y
devuelve la línea por otra vía. Construir una corrección sin un caso físico podría añadir complejidad a
un camino de seguridad eléctrica (R1). Por eso esta spec define el comportamiento sin elegir técnica, y
la alternativa «no construir» sigue abierta para el responsable.

## Clarificaciones

- 2026-11-28 — Revisión propia contra el código, correcciones C1–C4 autorizadas por el responsable. C1:
  la spec distingue trama corta (`shortFrames`), COMMAND sostenido (`stuckLows`) y error de recepción
  (`receiveErrors`). C2: el criterio 2 acepta agrupar pulsos (B2 y B4). C3: H2 se declara una
  ampliación de la regla 3, y el ancho mínimo y el filtro siguen abiertos. C4: los dos vaciados de la
  entrada se citan como evidencia parcial de B12, y el criterio 7 se conserva. Sigue en `draft`.
- 2026-11-28 — Revisión independiente de `ee10638` (comentario 7235075213 en la #125): C1–C4
  conformes; pide resolver D1 y D2 y mantener explícito M1. Decisiones del responsable: **D1** queda
  abierta hasta una medición física autorizada; **D2** exige MOTOR apagado en la aserción y en el paso
  (B7 y criterio 6, con prueba dedicada), y reemplaza la suposición anterior de MOTOR solo en el paso.
  **M1**: H4 no aprobada en general, puertas solo bajo el perfil autorizado. Sigue en `draft`.
