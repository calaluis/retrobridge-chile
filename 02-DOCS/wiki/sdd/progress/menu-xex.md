---
type: progress
title: Progreso — menu-xex
description: Avance por tarea de la spec menu-xex (arrancar archivos .xex desde el menú, en D1).
tags: [sdd, progress, menu, xex, 6502]
timestamp: 2026-11-20T16:00:00Z
topic: sdd
slug: menu-xex
status: in-progress
---

# Progreso — menu-xex

Rama `feat/menu-xex` (worktree `.worktrees/feat-menu-xex`, desde main `ff9ff63`).

## X001 — Fuente y cargador 6502 (hecha, 2026-11-20)

- Fuentes en `docs/fuentes.md`:
  - formato `.xex` (gury.atari8.info; cc65 dice lo mismo);
  - `PORTB` bit 1 (*Altirra Hardware Reference Manual*, §2.6).
  - La fuente del formato cubre `INITAD` entre segmentos, así que no se activó la regla de parada del plan.
- Desviación 1 del plan, aprobada («Solo PORTB»):
  - el cargador no toca `RAMTOP` ni reabre `E:`, porque el manual no cubre las variables del SO;
  - lee los sectores con `SIOV`, no con `DSKINV`;
  - muestra el aviso de error con una lista de visualización propia.
- `tools/make_xex_loader.py` genera `host/src/xex_loader.inc`:
  - 3 sectores de arranque en `$0700`, más un búfer de 128 bytes sin cargar;
  - zona prohibida `$0700`-`$08FF`;
  - el largo del `.xex` va en el desplazamiento 336 (3 bytes);
  - sin página cero: escribe cada byte con un `STA` cuyo operando reescribe.
- Pruebas en `tools/tests/test_xex_loader.py` (intérprete 6502 propio, `SIOV` simulado sobre el disco virtual):
  - se escribieron primero, y fallaron porque el módulo no existía;
  - después, 11 de 11 en verde; tras la revisión, 13 de 13.
- Mutantes:
  - BASIC sin apagar: lo detecta la prueba del BASIC;
  - `INITAD` sin reiniciar: lo detectan las pruebas de `INITAD`;
  - error de lectura ignorado: lo detecta la prueba del error.
- Controles:
  - pruebas: PASS. `tools/tests` 161 de 161.
  - compilación: NO-APLICA. El `.inc` nuevo todavía no lo incluye ningún `.cpp` (lo hará X002); `ctest` no cambia.
  - seguridad: NO-APLICA. Es código 6502 generado; no hay entrada nueva en el host.
  - trazabilidad: PASS.
    - `INITAD` entre segmentos y `RUNAD` = AC6.
    - BASIC apagado = AC7.
    - El error de lectura cumple *Behaviour/Fallos* de la spec.
  - `check_layering`, `check_notices`, `make_menu --check` y `make_xex_loader --check`: OK.
- Riesgo `rsc risk`: nivel 1, un revisor (correctitud). No encontró nada obligatorio; dejó 5 detalles menores:
  - 1, basura tras el texto de error: descartado. `screen_codes` rellena con espacios hasta 40 columnas
    (`tools/make_menu.py:146`); la prueba ahora lo comprueba.
  - 2, la prueba no miraba la lista de visualización: corregido, ahora comprueba sus bytes y el salto `JVB`.
  - 3, faltaban casos: se agregaron dos pruebas, una cabecera `$FFFF` partida entre sectores y un segmento que
    termina en `$FFFF`.
  - 4, un `.xex` cortado salta a `RUNAD` sin aviso: queda para X002, donde `wrap` lo rechaza como `Invalid`
    (AC8).
  - 5, un segmento en la página 1 (la pila) rompería el cargador: decisión abierta para X002 (¿se agrega
    `$0100`-`$01FF` a la zona prohibida?).

## X002 — Validar y envolver el `.xex` (hecha, 2026-11-20)

- `host/src/xex_image.{hpp,cpp}`: `XexImage::wrap(span, ImageBuffer&)` devuelve `{Ok, Invalid, Incompatible,
  TooLarge, NoMemory}` y el número de segmentos.
  - Función pura: no toca archivos, red ni SIO. Usa las constantes de `xex_loader.inc`; ningún número está
    duplicado.
  - Orden de las comprobaciones:
    1. `TooLarge` (más de 65535 sectores), mirando solo el tamaño;
    2. `Invalid` (sin `$FFFF` inicial, inicio > fin, cabecera o datos cortados);
    3. `Incompatible` (algún segmento toca `$0700`-`$08FF`);
    4. `NoMemory`.
  - `out` cambia solo con `Ok`.
  - Imagen: ATR de 128 bytes por sector. Lleva el bloque del cargador con el largo escrito (3 bytes) y después el
    `.xex` tal cual, rellenado con ceros hasta completar el último sector.
- `xex_image.cpp` se agregó al ejecutable (`host/CMakeLists.txt`) y la prueba `test_xex_image` a
  `test/host/CMakeLists.txt`.
- Pruebas (`test_xex_image`, 12):
  - se escribieron primero, y CMake falló porque `xex_image.cpp` no existía;
  - después, 12 de 12 en verde.
- Mutantes:
  - zona con `<` en lugar de `<=`: muerto;
  - byte alto del largo a 0: muerto;
  - sin la comprobación «inicio > fin»: sobrevivió, porque el byte de datos sobrante hacía inválido el archivo por
    casualidad. Se agregó un caso con un segmento bien formado detrás, y ahora muere.
- Controles:
  - pruebas: PASS. `ctest` 66 de 66; `tools/tests` 161 de 161.
  - compilación: PASS. MinGW ucrt64, `-Werror`; `retrobridge_host.exe` enlaza.
  - seguridad: PASS.
    - Un `.xex` de la red no desborda nada: `end - start + 1` se calcula después de comprobar `start <= end`.
    - Las restas de tamaño no pueden pasar por debajo de cero.
    - El tope de sectores se comprueba antes de reservar memoria.
  - trazabilidad: PASS. `Invalid` = AC8; `Incompatible` = AC9.
  - `check_layering` y `check_notices`: OK.
- Riesgo `rsc risk`: nivel 1, un revisor (correctitud). No encontró nada obligatorio; dejó 3 detalles menores:
  - 1, `TooLarge` va antes que `Invalid`: aceptado. Está documentado y la spec solo fija que `Invalid` va antes que
    `Incompatible`.
  - 2, un segmento sin cabecera que empieza en `$FFFF` se lee como cabecera: el cargador 6502 hace lo mismo
    (`AND dest` / `CMP #$FF`), así que el host y el Atari coinciden. Sin cambio.
  - 3, `out` nunca se probaba ya relleno: corregido. La prueba de rechazo parte de un `out` con contenido y comprueba
    que no cambia. `NoMemory` queda sin prueba: no se puede forzar sin un asignador falso.
- Pendiente para X004:
  - decidir la página 1 (la pila; hallazgo 5 de X001);
  - el límite de tamaño de un `.xex` que llega por TNFS.

## X003 — Listar `.XEX` en local y en TNFS (hecha, 2026-11-20)

- Una regla común en `host/src/menu_catalog.{hpp,cpp}`: `hasXexExtension` y `hasImageExtension` (`.atr` o `.xex`,
  sin distinguir mayúsculas).
  - `isCatalogName` (carpeta local) y `kept()` de `tnfs_folder.cpp` (TNFS) la usan.
  - Ya no queda ningún otro usuario de `hasAtrExtension`.
  - La lista no marca los `.xex`: basta la extensión visible (spec, *Clarifications*).
- La protección de `--log` (`file_identity.cpp`, `endsWithImage`) también cubre los `.xex` de la carpeta.
  - El mensaje de carpeta vacía ahora dice `no .atr or .xex images`.
  - Se actualizaron los comentarios de `main.cpp` y `file_identity.hpp`.
- Pruebas:
  - nuevas: `test_menu_catalog` (orden mezclado, `x.xexx`, `.xex` solo), `test_tnfs_folder` (`.XEX` junto a `.ATR` y
    carpetas) y `test_log_collision.py` (un `--log` que nombra un `.xex` de la carpeta se rechaza);
  - se escribieron primero, y la compilación falló porque `hasXexExtension` no existía; después, en verde;
  - la prueba vieja de carpeta vacía en TNFS usaba `a.xex` como «no imagen»; ahora usa `a.com`.
- Mutantes:
  - filtro TNFS solo `.atr`: muerto;
  - protección de `--log` sin `.xex`: muerto. El primer intento no compiló por `-Werror`, se repitió con
    `(void)xex` para que compilara y la prueba lo detectó.
- Controles:
  - pruebas: PASS. `ctest` 66 de 66; `ctest` del ejecutable 1 de 1; `tools/tests` 161 de 161.
  - compilación: PASS. `-Werror`; el ejecutable enlaza.
  - seguridad: PASS. El `--log` sigue sin poder escribir sobre ninguna imagen de la carpeta, ahora tampoco sobre un
    `.xex`.
  - trazabilidad: PASS. AC1 (local), AC2 (TNFS), AC10 (las pruebas de `.ATR` siguen en verde).
  - `check_layering` y `check_notices`: OK.
- Riesgo `rsc risk`: nivel 1, un revisor (correctitud). No encontró nada obligatorio; dejó 3 detalles menores:
  - 1, mensajes y comentarios que solo decían `.atr`: corregido.
  - 2, el contador `atrSeen` ahora cuenta también los `.xex`: el nombre queda igual (se usa como total de la lista);
    el comentario del encabezado ya lo dice.
  - 3, la prueba del registro solo usa mayúsculas en Windows: aceptado. La regla de extensión con mayúsculas y
    minúsculas mezcladas ya la prueba `test_menu_catalog`.
- Estado intermedio conocido: elegir un `.xex` hoy da «ATR INVALIDA» (`DiskImage::open` falla). X004 lo resuelve.

## X004 — Elegir y arrancar un `.xex` (hecha, 2026-11-20)

- Decisión del responsable (2026-11-20): un segmento en la página 1 (`$0100`-`$01FF`, la pila) da
  ` XEX NO COMPATIBLE` (`XexImage::kStackLow`/`kStackHigh`; prueba nueva en `test_xex_image`).
- Desviación 2 del plan, anotada en el plan.
  - Leer y envolver un `.xex` reserva memoria, y `key()` no puede hacerlo (ADR-011).
  - Por eso `MenuLoader::startXex` lo lleva al hilo de carga: fuente 1 = archivo de `--images`; 2..8 = TNFS.
  - `TnfsLoader::adopt()` envuelve el `.xex` y guarda en el hueco de memoria solo el disco resultante.
  - Los rechazos llegan como `XexNoValido` y `XexNoCompatible`.
- `MenuController`:
  - `chooseXex`: si la unidad no es D1, dice ` UN XEX SOLO VA EN D1` y no cambia nada (AC4);
  - si es D1, inicia `Job::Xex` (mientras tanto se ve `CARGANDO`);
  - si se acepta, `place()` deja el `.xex` en D1, como una imagen TNFS (AC3); `A` monta el hueco (AC5, parte host);
  - los rechazos muestran ` XEX NO VALIDO` (AC8) o ` XEX NO COMPATIBLE` (AC9).
  - Registro:
    - `menu_select ... assigned xex=1 segments=n bytes=m`;
    - `menu_select ... refused: result=invalid|incompatible`.
- `main.cpp` pasa `--images` al hilo de carga.
- Pruebas:
  - `test_menu_xex` (nueva, 7) usa el rig de TNFS con su propia carpeta local, creada por la prueba;
  - cada `K` comprueba que contesta en menos de 250 ms y que no reserva memoria;
  - se escribieron primero, y no compilaron porque faltaban el constructor con la carpeta y `startXex`; después,
    en verde.
  - Al rig se le agregaron la carpeta y `[[maybe_unused]]` en cuatro ayudantes.
- Mutantes (los 4 detectados):
  - sin la regla de solo D1;
  - el `.xex` crudo en el hueco, sin envolver;
  - el rechazo sin registrar;
  - un `.xex` de TNFS tratado como `.ATR`.
- Controles:
  - pruebas: PASS. `ctest` 67 de 67; `ctest` del ejecutable 1 de 1; `tools/tests` 161 de 161.
  - compilación: PASS. `-Werror`; el ejecutable enlaza.
  - seguridad: PASS.
    - Los bytes de la red solo los lee `XexImage::wrap`, ya revisado.
    - El archivo local se arma con el nombre del catálogo, que sale del escaneo de la carpeta y no lleva `/`, `:` ni
      `\`.
    - El tamaño se comprueba antes de reservar memoria.
  - trazabilidad: PASS. AC3, AC4, AC5 (parte host), AC8, AC9 y AC10.
  - `check_layering` y `check_notices`: OK.
- Riesgo:
  - el plan había previsto nivel 2; `rsc risk` dio nivel 1, con un revisor, al que se le pidió mirar también hilos y
    entradas de la red.
  - No encontró nada obligatorio; dejó 4 detalles menores:
    - 1, falta una prueba de Esc, reinicio en frío o un trabajo reemplazado durante una carga de `.xex`: aceptado.
      `Job::Xex` usa los mismos `startJob`, `cancelJob` y `publish` que `Fetch`, que ya prueba `test_menu_tnfs`.
    - 2, faltaban casos de `readLocal`: se agregó el de un `.xex` listado que ya no está, que da
      ` ERROR: NO SE PUDO ABRIR`. El caso de un archivo demasiado grande queda sin prueba en el menú; lo cubre
      `test_xex_image`.
    - 3, un nombre con `..`: el escaneo no los produce. Sin cambio.
    - 4, `events.has` busca un trozo del texto: aceptado. Los textos que se buscan son específicos.

## X005 — el `.xex` de prueba de RetroBridge

- Estado: hecho, pendiente de commit.
- Archivos:
  - `tools/make_xex_demo.py` (nuevo): programa 6502 propio en `$2000`, más INITAD y RUNAD. `--out` escribe el archivo.
  - `tools/tests/test_xex_demo.py` (nuevo): 6 pruebas.
  - `tools/tests/test_xex_loader.py`: `Cpu.run` acepta `pc`; el intérprete suma `AND #imm` (`$29`).
- Qué muestra en el Atari:
  - línea 1: ` RETROBRIDGE XEX OK` si corrió INITAD; si no, ` RETROBRIDGE XEX: INITAD NO CORRIO`;
  - línea 2: ` BASIC APAGADO` si PORTB bit 1 vale 1; si no, ` BASIC ENCENDIDO`.
- Rojo visto: las pruebas fallaron sin el módulo.
- Mutantes, 4 de 4 muertos:
  - el salto del marcador invertido;
  - la máscara de PORTB cambiada;
  - `init` sin marcar;
  - el segmento de INITAD quitado.
- Controles:
  - pruebas: PASS. `tools/tests` 167 de 167 (19 de `test_xex_*`).
  - compilación: no aplica; solo Python.
  - seguridad: PASS. Herramienta local; solo escribe la ruta de `--out`.
  - trazabilidad: PASS. Prepara la demo de AC5, AC6 y AC7 (X006).
- Riesgo: `rsc risk` nivel 1, un revisor.
- Revisión: un revisor. Sin fallas en el hardware; 2 huecos de prueba y 3 detalles:
  - 1, la prueba no miraba la lista entera: ahora compara los 12 bytes, con el salto JVB a sí misma.
  - 2, la guarda del bloque de 1 KB nunca se vio fallar: nueva prueba; su mutante muere.
  - 3, cada prueba sin INITAD o con BASIC mira una sola línea: aceptado; la prueba con el cargador cubre el resto.
  - 4, `BASIC_OFF` repetido: ahora se importa de `make_xex_loader`.
  - 5, no fija colores ni apaga el modo de atracción: aceptado; no cambia la prueba de "OK".
