---
type: spec
title: Spec — Pantalla principal del menú Atari (fuentes y unidades)
description: QUÉ y POR QUÉ de la nueva pantalla principal del menú de RetroBridge, basada en la maqueta de Display List Lab del responsable.
tags: [sdd, spec, host, netsio, menu, tnfs]
timestamp: 2026-11-17T23:30:00Z
topic: sdd
slug: menu-principal
branch: feat/40-menu-principal
status: approved
---

# Spec — Pantalla principal del menú Atari

> Slug: `menu-principal` · Estado: aprobada por el responsable el 2026-11-17 · Creada: 2026-11-17
> Origen: pedido del responsable del 2026-11-17, tras la fusión del PR #39 (merge 750ce10 en main).
> Parte del menú de arranque de la Issue #38 (ADR-017).

## Problema y por qué

El menú de #38 es una lista de hasta 20 ATR de la carpeta `--images`. Cada letra monta una imagen en D1 y
arranca. No muestra de dónde vienen las imágenes ni qué tiene cada unidad. Tampoco permite montar en otra unidad
ni elegir otra fuente. La maqueta del responsable define la pantalla que sí lo hace: fuentes numeradas, estado de
D1 a D8 y C:, y un pie con las acciones.

## ¿Qué pasa si no lo construimos?

RetroBridge sigue con una sola unidad y una sola fuente. Un programa que pide un segundo disco no tiene cómo
recibirlo. No hay camino visible hacia los servidores TNFS.

## La alternativa más barata

Ampliar la lista actual con una columna de unidad. Se descarta: no sigue la maqueta y no deja lugar para las
fuentes remotas.

## Inventario verificado en el código (main 750ce10)

| Pieza | Estado hoy | Evidencia |
|-------|-----------|-----------|
| Menú en el Atari | Una pantalla, 20 entradas A..T, monta en D1 y arranca (COLDSV) | `tools/make_menu.py`, `host/src/menu_controller.cpp` |
| Comandos del menú | Solo `$5A` `'M'` (AUX1 = índice): validar, montar en D1 | `host/src/menu_sio_transport.cpp` |
| Unidades D2..D8 | El dominio las admite (`DriveNumber` 1..8, `DriveBay` con 8 ranuras), pero el host configura una sola | `host/src/main.cpp:297,358` (`DriveBay bay{1}`) |
| Comandos de disco | STATUS y READ SECTOR. Unidades de solo lectura | `rb_sio/sio_codes.hpp`, `sio_command_dispatcher.cpp` |
| Casete C: | No existe ningún soporte | sin código |
| Fuentes configuradas | No existe el concepto. Solo la carpeta `--images` | `host/src/main.cpp` |
| TNFS | No existe. Solo puntos de extensión: `ImageByteSource`, `ImageRepository`, `ImageLocation` (127 ASCII), el futuro `rb_net_tnfs` | `docs/arquitectura-inicial.md` §5.3 |
| Entrada de texto en el Atari | No existe (el menú solo lee una tecla) | `tools/make_menu.py` |
| Volver al menú | Shift+F5 (COLD_RESET FF) vuelve D1 al menú; F5 conserva la imagen | ADR-017 |

## Objetivos

- Ver en una pantalla las fuentes configuradas y el estado real de D1 a D8 y C:.
- Abrir la fuente local, elegir una ATR y montarla en la unidad que el usuario elija.
- Arrancar lo que hay en D1 cuando el usuario lo pida, sin que elegir obligue a arrancar.
- Que cada tecla del pie haga lo que dice, o que la spec la declare pendiente con lo que le falta.

## Usuarios y contexto

El responsable, con Altirra, el hub NetSIO y `retrobridge_host` en el PC. Más adelante, la misma pantalla en el
firmware del ESP32 con la tarjeta SD como fuente 1.

## Qué podrá hacer el usuario

1. Encender el Atari (o Shift+F5) y ver la pantalla principal.
2. Pulsar `1` para abrir la fuente local: la lista de ATR de `--images`.
3. Elegir una ATR y la unidad (D1..D8) donde montarla. Volver a la pantalla principal y ver el nombre en esa
   unidad.
4. Pulsar `U` para gestionar las unidades: desmontar una unidad (y, si se aprueba Q3, intercambiar dos).
5. Pulsar `A` para arrancar el disco montado en D1.
6. Pulsar `2`..`8` y `E`: según la decisión Q1 (abajo), funcionan con TNFS o muestran «PENDIENTE: ...» con lo
   que falta. Nunca quedan mudas ni fingen funcionar.

## Qué resultado obtiene

Referencia visual aprobada: la maqueta de Display List Lab entregada por el responsable el 2026-11-17 (copiada
en `## Referencia` abajo). Se reproduce:

- título `RETROBRIDGE CHILE` en ANTIC modo 7 (20 columnas, doble alto);
- ocho filas `n  <fuente>` en modo 2, en blanco cuando no hay fuente en esa posición;
- subtítulo centrado `ESTADO DE UNIDADES` en modo 6;
- una línea vacía, D1..D8 con el nombre de la ATR montada o `SIN DISCO`, la línea `C:` y otra línea vacía;
- pie de dos filas de 40 columnas: teclas en inverso entre los remates gráficos $42 y $56, descripciones en
  texto normal;
- colores de la maqueta: COLBK $A0, COLPF0 y COLPF1 $0E, COLPF2 $A0, COLPF3 $46, CHACTL $02.

Lo que cambia respecto de la maqueta:

- direcciones: la display list, el texto y el código viven en la zona que carga el programa real (hoy $0700 en
  adelante), no en $3000/$5000; el juego de caracteres es el de la ROM (CHBASE $E0), no una copia en $4000;
- `DOS.ATR` y los servidores son datos de ejemplo: la pantalla muestra las fuentes configuradas en el host y lo
  montado de verdad;
- la fuente 1 se muestra como `SD` y en el host corresponde a la carpeta `--images`.

## Qué debe respetar

- ADR-012: nada de código de FujiNet ni de emuladores. Si hay TNFS, se escribe desde la especificación pública
  del protocolo, con su procedencia anotada y sin afirmar sala limpia.
- ADR-017: dispositivo `$5A`, Shift+F5 vuelve al menú, F5 conserva la imagen, validación al elegir.
- ADR-014: el montaje ocurre en el contexto de almacenamiento; la unidad lo aplica entre pasos.
- ADR-011: sin memoria dinámica en la ruta SIO.
- La protección de `--log` del PR #39 sigue cubriendo toda la carpeta.
- Unidades de solo lectura: no se agrega escritura.
- H4, H4-L y H4-T siguen cerrados. `sioGateApproved=false`. Nada de hardware ni flasheo.

## Cómo lo demuestra

- Pruebas del programa 6502 con el intérprete propio (`tools/tests/test_menu_program.py`): display list
  byte a byte contra la referencia, cada tecla del pie, las teclas pendientes y un host que no responde.
- Pruebas de host (`test/host`) para los comandos nuevos del menú y el estado de las unidades.
- Altirra, con el host lanzado una sola vez y ningún disco montado desde Altirra:
  1. captura de la pantalla principal real;
  2. `1`, elegir una ATR, montarla en D1, `A` arranca esa ATR, Shift+F5 vuelve a la pantalla con el nombre en
     D1;
  3. montar otra ATR en D2 y ver su nombre en D2;
  4. si Q1 incluye TNFS: abrir un servidor TNFS local de prueba, entrar en un directorio, elegir una ATR,
     montarla en D3 y ver su nombre en D3.
- Log JSONL del host y sha256 de cada archivo de evidencia, en `D:\rb-e0\m40` (fuera del repo).

## Comportamiento

- Al encender, D1 sirve el menú. Lo que el usuario monta en D1 desde el menú queda **asignado** a D1, pero D1
  sigue sirviendo el menú hasta que se pulsa `A`. Desde `A` hasta el siguiente Shift+F5, D1 sirve la imagen
  asignada.
- Shift+F5 (FF) vuelve D1 al menú y conserva lo asignado: la pantalla muestra lo que había.
- D2..D8 sirven sus imágenes en todo momento, también mientras se ve el menú.
- Una ATR inválida se rechaza al elegirla, con el motivo en pantalla, y la unidad conserva lo que tenía.
- Una tecla sin capacidad detrás muestra `PENDIENTE: <qué falta>` en la línea de mensajes y no cambia nada.
- Si el host no responde, se ve `ERROR: EL HOST NO RESPONDE`, como hoy.

## Criterios de aceptación

1. La captura de Altirra coincide con la maqueta en estructura, textos fijos, colores y pie (40 columnas, $42 y
   $56, teclas en inverso).
2. Las fuentes y D1..D8 muestran datos del host, no textos fijos.
3. El flujo local completo funciona en Altirra: abrir, elegir, montar en D1, arrancar, volver.
4. Una ATR montada en D2 aparece en D2.
5. Cada tecla del pie está implementada o muestra `PENDIENTE`, y la tabla de pendientes del PR dice qué falta.
6. Si Q1 incluye TNFS: el montaje desde un servidor TNFS funciona y el nombre aparece en la unidad elegida.
7. Las pruebas de tools, `test/host` y `retrobridge_host` pasan, y `check_layering` y `check_notices` también.

## Fuera de alcance

- Escritura en disco, formateo y unidades de lectura y escritura.
- Casete: la línea `C:` se muestra, pero sin soporte de casete.
- Servidores TNFS reales de terceros como parte de la prueba (no se envían datos a hosts externos).
- TNFS sobre WebSocket (WSS).
- Firmware del ESP32, hardware y flasheo.
- Más de 20 ATR por pantalla en la fuente local (se conserva el límite de #38).

## Puntos a clarificar

- Q1 (bloqueante) — Alcance de TNFS en esta iteración. Recomendación: pantalla, fuente 1, `A`, `U` y montaje en
  D1..D8 ahora; fuentes 2..8 leídas de un archivo de configuración y mostradas; abrir un servidor y `E` quedan
  `PENDIENTE`, con un cliente TNFS propio en una iteración siguiente. Motivo: TNFS necesita un cliente nuevo,
  un servidor de prueba propio y la entrada de texto en el Atari para `E`. Son tres capacidades nuevas.
- Q2 — Dónde viven las fuentes 2..8. Recomendación: un archivo de texto `--sources <archivo>`, una fuente por
  línea, como máximo 7. Sin archivo, las filas 2..8 quedan en blanco.
- Q3 — Qué hace `U`. Recomendación: elegir una unidad y desmontarla. Intercambiar unidades queda fuera.
- Q4 — Texto de la línea `C:`. Recomendación: `C: SIN CINTA`, que es cierto (no hay casete montado), con el
  soporte de casete anotado como pendiente en el PR.

## Decisiones tomadas por el responsable (2026-11-17)

- Q1: TNFS queda pendiente. Esta iteración implementa la pantalla, la fuente 1, `A`, `U` y el montaje en
  D1..D8. Las fuentes 2..8 se leen de `--sources` y se muestran; abrirlas y `E` muestran `PENDIENTE`. El flujo
  TNFS de Altirra (paso 4 de «Cómo lo demuestra» y criterio 6) no se ejercita en esta iteración.
- Q2: `--sources <archivo>`, una fuente por línea, como máximo 7.
- Q3: `U` elige una unidad y la desmonta.
- Q4: la línea `C:` dice `SIN CINTA`; el soporte de casete queda pendiente.
- Rama nueva `feat/40-menu-principal` desde origin/main, en `.worktrees/feat-40-menu-principal`, con una issue
  nueva enlazada a #38.

## Supuestos (review-after)

- La fuente 1 se rotula `SD` también en el host, aunque allí sea una carpeta.
- La display list de la maqueta ocupa 216 líneas de barrido (8 en blanco arriba). Se conserva. Si Altirra la
  recorta, se ajusta solo el blanco superior y se anota.
- El nombre en cada unidad se muestra en mayúsculas, con 35 columnas como máximo, como en la maqueta.

## Riesgos

- El programa del menú crece (navegación, montaje en unidad, pie). Puede necesitar más sectores que los 10 de hoy.
- Asignar D1 sin servirlo cambia el contrato de ADR-017: necesita una enmienda del ADR.
- Si se incluye TNFS, el host abre sockets a direcciones que da un archivo de configuración: hay que acotar la
  entrada y no usar servidores de terceros en las pruebas.

## Referencia

Maqueta de Display List Lab entregada por el responsable (2026-11-17), resumida:

```text
DLIST $3000: blank 8 / mode 7 lms $5000 / blank 8 / mode 2 lms $5100 x8 / blank 8 /
             mode 6 lms $5300 / mode 2 lms $5314 x11 / mode 2 lms $5500 x2 / jvb $3000
Colores: COLBK $A0, COLPF0 $0E, COLPF1 $0E, COLPF2 $A0, COLPF3 $46, CHBASE $40, CHACTL $02
$5000 " RETROBRIDGE CHILE  "
$5100 "1  SD" / "2  tnfs.calaluis.cl" / "3  fujinet.cl" / "4".."8" (40 columnas cada una)
$5300 " ESTADO DE UNIDADES " / vacía / "D1 DOS.ATR" / "D2".."D8 SIN DISCO" / "C: SIN CINTA" / vacía
$5500 $42 {1-8} $56 " ABRIR FUENTE" $42 {E} $56 " EDITAR SERVIDOR" + 3 ceros
      $42 {A} $56 "   ARRANCAR D1 " $42 {U} $56 " GESTIONAR UNIDADES"
```
