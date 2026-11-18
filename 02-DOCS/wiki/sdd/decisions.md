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
