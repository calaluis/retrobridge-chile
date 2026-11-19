---
type: analysis
title: Análisis — Fuentes TNFS en el menú del Atari
description: Lectura cruzada de constitución, spec, plan y tareas de menu-tnfs antes de implementar.
tags: [sdd, analysis, menu, tnfs]
timestamp: 2026-11-18T06:00:00Z
topic: sdd
slug: menu-tnfs
---

# Análisis — menu-tnfs

> Artefactos: [constitución](../constitution.md) (borrador) · [spec](../specs/menu-tnfs.md) (aclarada y aprobada) ·
> [plan y tareas](../plans/menu-tnfs.md) (aprobados). Fecha: 2026-11-18. Este informe no cambia ningún artefacto.

## Veredicto

**GATE: PASS.** CRITICAL 0 · HIGH 0 · MEDIUM 4 · LOW 6.

**Actualización 2026-11-18:** los cuatro MEDIUM (F1 a F4) están resueltos con la aprobación del responsable
(«Corregir los 4 medios»). Quedan abiertos los seis LOW (F5 a F10), que no bloquean. Ver *Resolución*.

No hay ningún hueco de cobertura ni ninguna contradicción que impida implementar. Los cuatro MEDIUM conviene
cerrarlos antes de `implement`, o aceptarlos a conciencia. Tres de ellos son datos que un trabajador que solo ve
su tarea tendría que adivinar.

## Mapa de cobertura

| Req | Requisito de la spec (corto) | Plan | Tareas | Estado |
| --- | --- | --- | --- | --- |
| AC1 | `2` abre la raíz; carpetas con `/` primero | §2, §3 TnfsFolder | T010, T012, T016 | covered (ver F5) |
| AC2 | entrar, subir, Esc en la raíz a la principal | §3 MenuController | T012, T016 | covered |
| AC3 | `Atari Logo.atr` a D1 con su mensaje | §4 flujo principal | T009, T012, T016 | covered (ver F2) |
| AC4 | `A` arranca; SHA-256 en el registro | §4, §5 | T004, T013, T016 | covered |
| AC5 | servidor mudo conserva D1 y `A` | §3 TnfsLoader | T011, T012, T016 | covered (ver F4) |
| AC6 | nunca «EL HOST NO RESPONDE» | §0, §3 | T012, T016 | covered (ver F4) |
| AC7 | «SERVIDOR NO ENCONTRADO» | §2 UdpTnfsPort | T008, T013, T016 | covered (ver F7) |
| AC8 | ATR inválida, D3 igual, prueba aparte | §4 | T012 | covered |
| AC9 | «MOSTRANDO 20 DE 128» | §3 TnfsFolder | T010, T012, T016 | covered |
| AC10 | «CARPETA VACIA» | §3 TnfsFolder | T010, T012 | covered |
| AC11 | fuente local `1` igual que en #40 | §0, §5 | T012, T016 | covered |
| AC12 | ninguna escritura en el servidor | §0, M41-4 | T002, T003, T005, T013, T016 | covered |
| AC13 | `E` sigue pendiente | §0 | T012 | covered |
| AC14 | `ARCADE.ATR` en D2 con «CARGANDO nn%» | §5 sector 65 535 | T006, T007, T009, T015, T016 | covered |
| AC15 | carga larga automática termina montada | §5 | T006, T012, T015 | covered |
| AC16 | el servidor cae a mitad de carga | §3 | T005, T011, T012 | covered |
| AC17 | Esc cancela la carga | §3 | T006, T011, T012 | covered |
| AC18 | respuesta tardía tras cancelar | §3, §4 | T011, T012 | covered |
| AC19 | listado lento con «CARGANDO nn%» | §3 | T006, T012 | AMBIGUOUS (F3) |
| AC20 | «ERROR: NO SE PUDO ABRIR» | §3 | T005, T011, T012 | covered |
| AC21 | respuestas mal formadas | §3 TnfsCodec | T002, T005, T011, T012 | covered |
| — | lectura del sector 65 535 con un disco propio | §5, M41-7 | T007, T015 | DRIFT aprobado (F6) |

Sentido inverso: las 17 tareas trazan a un criterio, a una restricción de la spec o a una decisión M41. Las únicas
piezas que la spec no pide de forma literal son la normalización de teclas (F1) y el disco de lectura (F6). Las
dos las pidió el responsable al revisar el plan.

## Hallazgos

| # | Gravedad | Tipo | Artefacto A (lugar) | Artefacto B (lugar) | Conflicto | Se resuelve en |
| --- | --- | --- | --- | --- | --- | --- |
| F1 | MEDIUM | contradicción | spec, *Qué debe respetar*, «Programa 6502 del menú»: «cambia **solo** para volver a preguntar al host mientras dure una carga» | plan §0 «Códigos de tecla», §3 «Normalización», M41-3; T006 | El plan cambia el 6502 una segunda vez (`AND #$3F`). Un revisor que compare el diff con la spec lo marcaría como violación. | `clarify`: una línea en la spec que nombre la normalización |
| F2 | MEDIUM | ambigüedad (portador) | spec AC3: `ATARI LOGO.ATR` en mayúsculas; *Behaviour*: nombres no imprimibles «se sustituyen» | plan §3 TnfsFolder «nombre para mostrar (38 columnas)»; T010 y T012, Interfaces | Ni el plan ni las interfaces dicen que el nombre pasa a mayúsculas (#40 lo hace con `toScreen(..., upper)` en `menu_image.cpp`) ni con qué carácter se sustituye un byte UTF-8 de 0x80 o más. `toScreen` hoy los pinta como códigos de pantalla crudos. | `tasks`: añadir la regla a las Interfaces de T010 y T012 |
| F3 | MEDIUM | ambigüedad | spec, suposición «listado lento»: «el porcentaje es una estimación» | plan §3 TnfsLoader `EnCurso(porcentaje)`; T011, Interfaces | No dice cómo se calcula el porcentaje de un listado, cuyo total no se conoce hasta el final. AC19 no puede comprobar que «sube». | `tasks` (o `plan`): fijar la regla en T011 |
| F4 | MEDIUM | ambigüedad | spec AC5 y AC6: el error aparece al abrir una fuente muda | plan §3 TnfsClient: «espera por intento según el mínimo que anuncia el servidor» | Antes del primer MOUNT el servidor no anunció nada. Falta la espera del primer contacto, y de ella depende cuánto tarda en aparecer «ERROR: SERVIDOR NO RESPONDE» (la sonda de laboratorio usó 2 s y 3 intentos). | `plan`: un valor en §0 o §3 |
| F5 | LOW | ambigüedad | spec AC1: «y el mensaje debajo del pie» | plan §0 textos | En la raíz de `pulgarcito` (9 entradas) ningún aviso aprobado aplica. No se dice qué mensaje se espera; lo razonable es línea vacía. | aceptar, o `clarify` |
| F6 | LOW | alcance | spec *Cómo lo demuestra*: no menciona el sector 65 535 | plan §5, M41-7; T007, T015 | Prueba más fuerte, pedida por el responsable al revisar el plan. La spec no la refleja. | aceptar, o `clarify` para nombrarla |
| F7 | LOW | ambigüedad | spec *Cómo lo demuestra*: «una fuente que nombra un destino sin servidor TNFS» | T016: «servidor caído (AC5, AC6, AC7)» | AC7 necesita un nombre que no se resuelve, y AC5/AC6 uno que se resuelve pero no contesta. La demo necesita dos fuentes distintas en `--sources`. | `tasks`: precisar T016 |
| F8 | LOW | ambigüedad | plan §3 `MemoryImageStore.adopt -> SinHueco` | spec: ningún texto | Con 9 huecos no debería pasar. Si pasa, no se dice qué aparece en pantalla. Por analogía con M41-6, sería «ERROR: NO SE PUDO ABRIR». | `tasks`: una línea en T011 |
| F9 | LOW | constitución | constitución: todas las secciones «Pending» | spec y plan | No hay principios ratificados, así que no hay nada que violar. Las restricciones salen de los ADR y de M40/M41. No bloquea, pero el chequeo de constitución es vacío. | `constitution`, cuando el responsable quiera |
| F10 | LOW | decisión | `docs/arquitectura-inicial.md` §5.3: `TnfsImageRepository`, `TnfsByteSource` y `ImageLocation` con esquema | plan §2 (memoria y `*tnfs*n`), M41-2; T014 | El plan se aparta de §5.3 y lo defiende, pero la comprobación de T014 no exige que ADR-018 deje escrito ese apartamiento. | `tasks`: añadirlo a la comprobación de T014 |

## Por qué importan los MEDIUM

- **F1:** si no se corrige, el revisor de T006 tiene razón al marcar el cambio, y se pierde una ronda de revisión
  discutiendo algo que ya está decidido.
- **F2:** sin la regla, el trabajador de T010 puede no pasar a mayúsculas. AC3 fallaría en la demo, o la «ñ» de
  `Atari Tarots Español5.atr` saldría como caracteres raros.
- **F3:** sin la regla, cada trabajador inventa la suya y la prueba de AC19 no sabe qué esperar.
- **F4:** sin el valor, «servidor caído» puede tardar 3 s o 30 s, y las pruebas de AC5/AC6 no tienen un límite
  que comprobar.

## Ruta recomendada

- **`clarify` (spec):** F1. Opcionales: F5 y F6.
- **`plan`:** F4.
- **`tasks`:** F2, F3, F7, F8 y F10. Son añadidos a las Interfaces y a las comprobaciones, sin cambiar IDs.
- **`constitution`:** F9, sin prisa.

Ninguno obliga a rehacer una fase. Son cambios de una o dos líneas cada uno.

El índice `02-DOCS/wiki/index.md` no existe en este repositorio, así que este informe no se indexó.

## Resolución (2026-11-18)

- **F1, resuelto en la spec:** *Qué debe respetar* nombra los dos cambios del programa 6502: el sondeo durante la carga y la normalización de teclas (M41-3). Entrada en Revisions de la spec.
- **F4, resuelto en el plan:** §0 «Esperas TNFS» y §3 TnfsClient. Primer contacto con 3 intentos de 2000 ms, así que un servidor mudo da el error en 6 s como máximo. Después del MOUNT, la espera mínima anunciada, o 2000 ms si no hay ninguna.
- **F2, resuelto en las tareas:** Interfaces de T010 (mayúsculas como en #40; cada carácter no ASCII da un solo `?`; `path` conserva los bytes originales) y de T012 (D1 a D8 y el mensaje de asignación usan ese `display`).
- **F3, resuelto en las tareas:** Interfaces de T011. Traer: bytes leídos sobre el tamaño. Listar: 0 durante READDIR y después las entradas consultadas sobre las entradas por consultar. Tope en 99 y nunca baja. AC19 retrasa los STAT.
- **F5 a F10:** siguen abiertos como LOW, aceptables. Ningún ID de tarea cambió.
