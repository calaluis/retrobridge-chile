---
type: progress
title: Progreso — menu-navegacion
description: Avance por tarea de la spec menu-navegacion (rama feat/menu-navegacion).
tags: [sdd, progress]
topic: sdd
slug: menu-navegacion
---

# Progreso — menu-navegacion

Rama `feat/menu-navegacion` (worktree `.worktrees/feat-menu-navegacion`, desde main `68c9d9f`).

## N001 — Códigos de navegación en el 6502 (2026-11-19)

- TDD: 8 pruebas nuevas en `tools/tests/test_menu_program.py`, rojas primero (faltaban `NAV_*` y `STICK0`).
- Hecho en `tools/make_menu.py`: flechas con Control (`$8E` `$8F` `$86` `$87`, también con Shift) mandan `$40`
  a `$43`; `=` solo sigue siendo `$0F`; subrutina `joy` (puerto 1): dirección nueva al instante, sostenida a los
  30 cuadros y luego cada 9, diagonal ignorada; fuego `$44` solo al pasar de suelto a pulsado, y uno sostenido
  al arrancar no cuenta; la espera de la acción 2 también mira el joystick. Constantes `kMenuNav*` en
  `menu_program.inc`.
- `Menu6502`: JSR/RTS genéricos, CLC, CPY/CMP abs, LDA abs,Y; joystick por cuadro; `STEPS_PER_FRAME` 20 -> 200
  (con 20, el bucle de espera ya no cabía en un cuadro).
- Efecto: el programa pasa de 11 a 12 sectores; la pantalla queda en `$08F0`-`$0CAF` (riesgo 2 del plan; va a
  la enmienda 3 de ADR-017 en N005).
- Cambio de alcance: el pie de teclas nuevo pasa a N003, donde cambia el comportamiento del host.
- rsc risk: tier 1. Revisor refuter-correctness, ronda 1: should-fix (la repetición comparaba RTCLOK con signo y
  se paraba unos 2 s si una respuesta tardaba más de 127 cuadros) corregido con una cuenta atrás por cuadro visto
  y la prueba `test_a_slow_answer_only_pauses_the_repeat`; nit (tolerancias y vuelta de RTCLOK) corregido: huecos
  exactos 30 y 9, RTCLOK desde 240.
- Mutantes (opcodes válidos): sin comprobar el mismo cuadro, primera repetición 29, fuego que repite, flecha sin
  Control: los 4 detectados.
- Controles: py 29/29, sonda 1/1, host 59/59, retrobridge_host 1/1, check_notices y check_layering OK,
  `make_menu.py --check` al día. Commit aprobado: a430f53.

## N002 — 999 entradas y almacenamiento intercambiable (2026-11-19)

- TDD: pruebas rojas primero (no compilaban: faltaban `count()`, `path()`, `ready()`, `kCapacity`).
- `TnfsFolder`: un bloque fijo de 999 entradas (nombre para mostrar 38 + nombre del servidor hasta 255), reservado
  una vez con `new (std::nothrow)` al crear el objeto; `fill()` en el hilo del cargador; `path(i)` arma la ruta al
  elegir; `swap()` intercambia el puntero. Una entrada cuya ruta pase de 255 bytes no se guarda.
- `TnfsLoader`: `workFolder_` solo del hilo del cargador; `publish()` lo intercambia con `doneFolder_` bajo el
  cerrojo (solo si el resultado es carpeta); el descarte llama `clear()`. Tres bloques que cambian de mano, nunca
  se liberan ni se reservan de nuevo. Sin memoria al crear: `main` no arranca y dice la causa.
- `MenuCatalog::kMaxEntries` 999. Las listas siguen dibujando 20 filas (páginas en N003).
- Cambios a propósito en pruebas existentes: `test_host_menu` (25 nombres, ya no 20); `menu_entry` del registro
  numera desde 1 en vez de A-T (`test_log_collision.py`; README en N005).
- Pruebas nuevas: 205 y 1200 entradas; `swap` sin reservas y con el puntero intercambiado; `poll()` con 0
  reservas para 1200; ruta de 255 sí y de 256 no; sin memoria; `test_menu_catalog` con 25 y 1200.
- rsc risk: tier 1. Revisor refuter-correctness, ronda 1: sin blocker ni should-fix. Nit 3 (sin prueba de falta de
  memoria) corregido. Nits 1, 2 y 4 descartados: el nombre de la prueba de 129 sigue a AC9 de menu-tnfs; una
  carpeta con '/' final se trata como la raíz y da la ruta correcta; el cierre del registro sin memoria sigue el
  mismo patrón ya usado en main al fallar el socket UDP.
- Mutantes: swap que copia el contenido, capacidad 998, límite de ruta con un byte de menos, publish sin
  intercambio: detectados por las pruebas; subcarpeta sin '/': detectado solo al compilar (variable sin uso).
- Controles: host 60/60, retrobridge_host 1/1.
  Commit aprobado: e18768c.

## N003 — Cursor, páginas y pila de posiciones (2026-11-19)

- TDD: `test_menu_navegacion` nuevo (rojo primero: 11 casos fallaban por falta de cursor y páginas). El banco de
  `test_menu_tnfs` pasa sin cambios a `test/host/menu/menu_tnfs_rig.hpp` para compartirlo.
- Hecho en `MenuController`: `$40`-`$44` se reconocen antes de `& $3F`; arriba/abajo mueven una fila y cruzan de
  página en el borde; izquierda/derecha cambian de página (fila 1); RETURN (`$0C`) y fuego (`$44`) confirman la
  fila marcada; las letras eligen en la página visible. `EntryIndex` (16 bits) en `chosen_`, `Shown::entry`,
  `assignD1` y `mountOn`. Fila del cursor en vídeo inverso; se dibujan solo las 20 filas visibles. Pila de 32
  niveles: `listed()` empuja tras entrar y saca tras subir, ajustada al total nuevo; abrir la fuente la vacía.
- `pageLine()`: «PAGINA n DE m (t)» o «(k DE t)»; reemplaza «MOSTRANDO 20 DE n». Lista local: total = `atrSeen`
  cuando el catálogo está lleno.
- Pie de la lista: `[A-T] ELEGIR [RET] OK [<>] PAG [ESC] VOLVER` (40 columnas; el texto del boceto no cabe con
  los marcos gráficos). `menu_program.inc` regenerado; sigue en 12 sectores.
- Cambios a propósito en pruebas existentes: fila 1 marcada en `test_menu_tnfs` y `test_host_menu`; texto de
  página en `test_menu_tnfs` y `test_tnfs_folder`.
- rsc risk: tier 1. Revisor refuter-correctness, ronda 1: sin defectos en producción; should-fix (pila más allá de
  32 sin probar) y nits (bordes 20/21/40, lista local llena, reabrir fuente y principal) corregidos con pruebas.
- Done-check ampliado por el responsable: `test_esc_to_a_parent_that_shrank_keeps_the_cursor_in_range` (entrar
  desde la página 3, el servidor deja al padre en una página de 15, Esc deja el cursor en la última entrada y
  RETURN entra en ella) y `test_a_cancelled_or_failed_entry_keeps_the_position_and_the_history` (con un nivel
  guardado, una entrada cancelada y otra rechazada conservan carpeta, página 2 y fila; Esc vuelve a la raíz en la
  fila de BIG). `FakeTnfsServer::remove()` nuevo (archivo o carpeta vacía, entre trabajos). Mutantes dirigidos:
  sin ajuste tras recarga menor, y empujar al empezar la entrada en vez de al terminar bien: cada uno lo detecta
  su prueba.
- Mutantes: 10 de 11 detectados (códigos enmascarados, pila que pisa el nivel 32, pila que restaura niveles
  perdidos, bajar más allá del borde, subir a la fila 1, abrir sin reiniciar, total local sin `atrSeen`, sin
  inverso, línea de página con 20, páginas mal redondeadas). Equivalente: letras acotadas por el total en vez de
  por las filas de la página (mismo resultado).
- Controles: host 61/61 (navegación 16 casos, carpeta 16), retrobridge_host 1/1, py OK,
  `make_menu.py --check` al día.
  Commit aprobado: 7669773.

## N004 — Cursor en la principal y en la elección de unidad (2026-11-19)

- TDD: 5 casos nuevos en `test_menu_navegacion`, rojos primero (sin marca en la principal ni en D1-D8).
- Hecho en `MenuController`: `sourceLine_` marca una de las 8 fuentes (también las vacías); arriba/abajo la mueven
  y paran en 1 y 8; izquierda/derecha no hacen nada; RETURN y fuego actúan como el dígito de la fuente marcada; el
  dígito mueve el cursor a su fuente. Elegir unidad (`Pick`, `FolderPick`, `Unmount`): pantalla principal con el
  cursor sobre D1-D8 (empieza en D1); RETURN y fuego eligen la unidad marcada; los dígitos siguen; Esc como antes.
  `FolderPick` deja de dibujar la lista (M42-4). Mensaje « UNIDAD PARA: <nombre>».
- Cambios a propósito en pruebas existentes: la fuente 1 marcada en `test_host_menu`; «UNIDAD PARA:» en la línea
  de la principal (`test_host_menu`, `test_menu_tnfs`, pruebas de N003); en la principal, arriba y abajo ya mueven.
- rsc risk: tier 1. Revisor refuter-correctness, ronda 1: sin blocker ni should-fix. Nit 3 (sin prueba de Esc en
  Desmontar ni de la marca durante una carga desde la principal) corregido con pruebas. Nits 1 y 2 (texto de
  Desmontar; mensaje que queda al mover) sin cambio: el texto sigue valiendo y el dígito se comporta igual. Nit 4
  descartado: el 6502 enmascara las teclas con `& $3F` antes de enviar, así que ninguna llega como `$40`.
- Mutantes: 10 de 10 detectados (fuego ignorado, bajar más allá de 8, subir más allá de 1, dígito sin mover el
  cursor, cursor de unidad sin reiniciar (tras reforzar la prueba local), confirmar siempre D1, FolderPick en la
  lista, sin marca de fuente, desmontar sin cursor, izquierda que mueve).
- Controles: host 61/61 (navegación 22 casos), retrobridge_host 1/1.
  Commit aprobado: 250af3c.

## N005 — Documentación (2026-11-19)

- ADR-017, enmienda 3 (códigos `$40`-`$44` que amplían M41-3, repetición y fuego, host antes de la máscara, 999
  entradas en páginas de 20, elegir unidad sobre la principal; 12 sectores, pantalla `$08F0`-`$0CAF` comprobada
  con `make_menu.menu_program()`, pie nuevo, `menu_entry` desde 1). Estado del ADR enlazado a la enmienda.
- `host/README.md`: cursor, flechas y joystick, tabla de teclas, `PAGINA n DE m (t)`, `menu_entry` desde 1.
- `02-DOCS/wiki/sdd/decisions.md` (en la rama): sección nueva con M42-1 a M42-4, enlazada al ADR.
- Controles: `check_notices` OK; sin emoji en los tres archivos; enlace a ADR-017 con la misma ruta relativa que
  las secciones anteriores.
- rsc risk: tier 1. Revisor refuter-correctness (contraste con el código): todo correcto, sin enlaces rotos ni
  emoji. Nit 1 (la ruta del plan no existe en la rama) sin cambio: los planes viven en el checkout principal sin
  versionar, igual que `menu-tnfs`; es texto, no enlace. Nit 2 sin cambio (el texto es exacto). Los 250 ms los
  comprueba `Rig::press` en cada tecla de las pruebas.
  Commit aprobado: e893489.

## N006 (demo) — 2026-11-19

- Demo en Altirra contra pulgarcito hecha por el responsable: «super bien». Dos observaciones: (1) mover el
  cursor no debe procesar, solo mover, salvo RETURN o el botón rojo; (2) la ruta no debe ir en el título.
- Causa de (1): cada movimiento era una transacción SIO con la pantalla entera (962 bytes, unos 0,6 s).
- Propuesta aprobada: N007 cursor local en el 6502; N008 línea de ruta bajo el título.

## N007 — cursor local en el 6502 — 2026-11-19

- 6502: bloque de cursor de 5 bytes en la pantalla (`kMenuCursor` = 955: base, filas, fila, paginada). Con la
  acción 0 y filas > 0, arriba y abajo mueven la barra en el Atari, sin SIO. RETURN y fuego envían `$C0` + fila.
  En el borde, e izquierda y derecha, se pregunta al host solo si hay más páginas. Sin respuesta del host, el
  cursor local se apaga. 13 sectores; pantalla `$0993`-`$0D52`, dentro de 4 KB.
- Host: escribe el bloque en cada pantalla mostrada (0 filas al cargar o con la lista vacía). `$C0`+fila fija la
  fila y equivale a RETURN; una fila que no existe no hace nada. Arriba en una lista parte de la fila 0, abajo de
  la última. Rango `$C0`: el `$50` chocaba con Shift+dígito (prueba existente).
- Pruebas: Python 9 nuevas (139 en total, OK); C++ 4 nuevas y el rig imita el 6502 (`Rig::atari`); host 61/61,
  retrobridge_host 1/1.
- Mutantes: 6502 7/7; host 9/9 (3 cerrados con pruebas puntuales; «always paged» muere por compilación).
- rsc risk: tier 1. Revisor refuter-correctness: sin bloqueantes. Nit 2 arreglado (cursor apagado tras fallo SIO,
  prueba roja antes del arreglo). Hallazgo 1 abierto: una tecla que el host no usa devuelve la barra a la fila
  que el host dibujó. Hallazgo 3: el host supone siempre su propio programa 6502 (cierto hoy).
  Commit aprobado: 88de12d.

## N008 — título fijo y línea de ruta — 2026-11-19

- La lista lleva el título fijo de la principal (" RETROBRIDGE CHILE", en la plantilla). La fuente y la carpeta van
  en una línea de 40 columnas (`kListPath` = 20) donde estaban las 8 líneas en blanco bajo el título; misma altura
  (200 líneas). Entradas desde 60, pie 860, mensaje 900; bloque de cursor 955 intacto. Pantalla `$099A`-`$0D59`.
- Ruta recortada por la izquierda con ".." (tnfsTitle) a 40 columnas menos el prefijo.
- Pruebas: Python 140 OK (2 nuevas); C++ 61/61 (`assert_title` comprueba título fijo y línea de ruta);
  retrobridge_host 1/1. Mutantes 5/5. rsc risk tier 1; revisor refuter-correctness: ninguno. Nit: README y
  decisiones sin el layout nuevo (va en la tarea de documentación).
- Demo preparada en `D:\rb-e0\m44`.
  Commit aprobado: 554888c.

## N009 — documentación de N007 y N008 — 2026-11-19

- ADR-017: «Enmienda 4» (cursor local, `$C0` + fila, host y fila del Atari con el límite conocido, título fijo y
  línea de ruta; consecuencias en lugar de las de la enmienda 3: 13 sectores, pantalla `$099A`-`$0D59`). Estado
  enlazado a la enmienda.
- `host/README.md`: el cursor se mueve en el Atari; título fijo y línea de ruta.
- `decisions.md`: M42-5 y M42-6.
- Controles: check_notices OK; sin emoji. rsc risk: tier 0 (docs), sin revisor.
  Commit aprobado: 6a11f6d.

## Ship — 2026-11-19
- Push de feat/menu-navegacion y PR #45 abierto: https://github.com/calaluis/retrobridge-chile/pull/45 (CI en curso).

## Revisión del PR #45 (CI y ChatGPT) — 2026-11-19

- CI de Linux: `pageLine` fallaba con -Werror=format-truncation. Arreglo: formato en búfer local de 96 bytes y
  copia con límite. Commit f2fa82e, con push (Auto-fix).
- Observación 2 (ChatGPT): en una lista de dos páginas, izquierda en la primera o derecha en la última hacía SIO y
  la barra volvía a la fila 1. Causa: el bloque solo decía «hay varias páginas». Arreglo: bits PAGE_BEFORE y
  PAGE_AFTER; el 6502 pregunta solo si hay página en esa dirección, si no la barra se queda sin SIO. Pruebas:
  Python 1 nueva (ambos bordes, fila conservada, sin SIO); C++ 1 nueva (hostCalls sin cambio, fila conservada,
  RETURN toma la fila). Mutantes 4/4. ADR-017 enmienda 4 y M42-5 ajustados; pantalla `$09B0`-`$0D6F`.
  Commit aprobado: c039a83, con push.
- PR #45 fusionado en main el 2026-11-19 (merge 612ebf0), con c039a83 incluido; CI 8 OK, 2 omitidos.
