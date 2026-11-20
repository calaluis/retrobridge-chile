---
type: plan
title: Plan — Arrancar archivos XEX desde el menú
description: CÓMO se construye la spec menu-xex: listar .xex, validarlos al elegirlos, envolverlos en un disco virtual con un cargador 6502 propio y arrancarlos desde D1.
tags: [sdd, plan, menu, xex, 6502, host]
timestamp: 2026-11-20T09:00:00Z
topic: sdd
slug: menu-xex
spec: 02-DOCS/wiki/sdd/specs/menu-xex.md
branch: feat/menu-xex
status: approved
---

# Plan — Arrancar archivos XEX desde el menú

## 0. Global constraints (literales, para cada tarea)

- C++17 con `-fno-exceptions -fno-rtti -Werror` (ADR-004). Sin reservas en la ruta SIO (ADR-011): el disco virtual
  se arma al elegir, fuera de la ruta SIO, como hoy una descarga TNFS.
- Código propio (ADR-012). El cargador 6502 se escribe desde una fuente pública del formato binario del Atari (por
  ejemplo, el manual del DOS 2.0S o *Mapping the Atari*, `RUNAD` `$02E0` e `INITAD` `$02E2`) y del *Altirra Hardware
  Reference Manual* (ya registrado: `PORTB`, `DSKINV`). Sin leer código de FujiNet, de emuladores ni de otros
  cargadores. La fuente nueva se registra en `docs/fuentes.md` antes de escribir el cargador (X001).
- El cargador se genera con un script de `tools/` en un `.inc`; nunca se edita a mano; las pruebas comparan.
- `check_layering` en verde (ADR-002): el formato `.xex` y el disco virtual viven en el host, no en el dominio.
- Las `.ATR` no cambian (AC10). Un solo `retrobridge_host` por hub; en Altirra, D1..D8 en Off.

## 1. Context & constraints

- Spec: 10 criterios de aceptación; 5 puntos tipados.
- Hoy (main `ff9ff63`):
  - la lista local la arma `MenuCatalog` con `hasAtrExtension` (`menu_catalog.cpp:40`) y el escaneo de carpeta de
    `file_identity.cpp` (`endsWithAtr`, líneas 100-185); TNFS filtra con `tnfs_folder.cpp:65`;
  - elegir una imagen pide la unidad; una `.ATR` de TNFS se descarga a un slot de `MemoryImageStore` (9 slots) y se
    sirve con la ubicación `*tnfs*<n>`; una local se abre como archivo;
  - `A` monta D1 (`MenuController::boot`) y el 6502 hace un arranque en frío; el OS lee los sectores de arranque.
- El tamaño de una descarga TNFS ya tiene su límite (`allocate`); el `.xex` usa el mismo (suposición de la spec).

## 2. Architecture

```text
lista ──(.XEX)──> elegir ──> unidad 1 ──> leer bytes (archivo local o descarga TNFS)
                               │                │
                       2..8: " UN XEX SOLO VA EN D1"   ▼
                                     XexImage::wrap(bytes) ──> Invalid      -> " XEX NO VALIDO"
                                                          ──> Incompatible -> " XEX NO COMPATIBLE"
                                                          ──> disco virtual (ATR en memoria) -> slot -> D1
A ──> monta el slot en D1 ──> arranque en frío ──> el OS carga el cargador ──> el cargador carga el .xex
```

1. **Listar.** Una regla común `hasImageExtension` acepta `.ATR` y `.XEX` (sin distinguir mayúsculas) en el
   catálogo local, el escaneo de carpeta y el filtro TNFS. Cada entrada sabe si es `.xex`.
2. **Validar y envolver** (`host/src/xex_image.{hpp,cpp}`, función pura, probada con `ctest`):
   - recorre los segmentos: cabecera `$FFFF` al inicio (y opcional entre segmentos), inicio, fin, datos;
     inicio > fin o datos que faltan = `Invalid`;
   - un segmento que toca la zona del cargador (`$0700` hasta el fin de su búfer) = `Incompatible`;
   - arma un ATR de 128 bytes por sector: cabecera de 16 bytes, los sectores del cargador (con el largo del `.xex`
     escrito en él) y después los bytes del `.xex` tal cual, sector tras sector. Sin tope de 720 sectores.
3. **Cargador 6502** (`tools/make_xex_loader.py` → `host/src/xex_loader.inc`, original):
   - registro de arranque en el sector 1; se carga en `$0700`;
   - apaga el BASIC del XL/XE (`PORTB` bit 1 a 1); no toca `RAMTOP` ni reabre `E:` (desviación 1);
   - lee los sectores de datos uno a uno con `SIOV` (orden `R` a la unidad 1) en un búfer propio y recorre el
     `.xex` en flujo: copia cada segmento a su dirección; antes de cada segmento apunta `INITAD` a un `RTS` propio y
     después hace `JSR (INITAD)`, así solo corre la rutina que el segmento escribió;
   - al final, `JMP (RUNAD)`; sin `RUNAD`, se detiene (bucle propio);
   - error de lectura: muestra ` ERROR DE LECTURA XEX` con una lista de visualización propia y se detiene.
4. **Elegir y arrancar** (`MenuController`): un `.xex` en otra unidad que D1 da el mensaje; en D1, lee los bytes
   (local: archivo leído entero al slot; TNFS: la descarga de hoy), llama a `wrap` y deja el disco virtual en un
   slot. D1 muestra el nombre del `.xex`. `A` monta ese slot como hoy una imagen de TNFS. El slot es de solo lectura.
5. **Registro.** `menu_select` agrega `xex=1 segments=n bytes=m`; los rechazos van con `result=invalid|incompatible`.

## 3. Interfaces & contracts

- `XexImage::wrap(span<const uint8_t> xex, ImageBuffer& out) -> XexResult` con `{Ok, Invalid, Incompatible,
  TooLarge, NoMemory}` y el número de segmentos. No toca archivos ni red.
- El `.inc` del cargador exporta: el bloque, la cantidad de sectores de arranque, el desplazamiento donde el host
  escribe el largo del `.xex`, y `kXexLoaderLow`/`kXexLoaderHigh` (zona prohibida para los segmentos).
- Protocolo con el menú (ADR-017): sin cambios. Mensajes nuevos: ` UN XEX SOLO VA EN D1`, ` XEX NO VALIDO`,
  ` XEX NO COMPATIBLE`.

## 4. Data model & flow

- `MenuCatalog::Entry` gana una marca `xex`. `Shown` de D1 apunta a un slot de memoria, como TNFS; su nombre es el
  del `.xex`.
- Un `.xex` local se lee entero a memoria al elegirlo (no se sirve como archivo, porque el disco no es el archivo).

## 5. Testing strategy

- **Cargador** (`tools/tests/test_xex_loader.py`, intérprete 6502 de `test_menu_program.py` con `DSKINV` simulado):
  segmentos copiados a su dirección (AC6); `INITAD` llamado entre segmentos, en orden (AC6); `RUNAD` al final; BASIC
  apagado (`PORTB` bit 1) (AC7); error de lectura con su texto; `.inc` al día.
- **Envoltura** (`ctest test_xex_image`): válido de 1 y de varios segmentos; con `$FFFF` entre segmentos; sin
  cabecera, inicio > fin, datos cortos = `Invalid` (AC8); segmento en `$0700`-fin de cargador = `Incompatible` (AC9);
  `INITAD`/`RUNAD` en `$02E0`-`$02E3` permitidos; sectores y cabecera ATR correctos.
- **Menú** (`ctest`, pruebas del controlador con catálogo y cargador falsos): `.XEX` listado local y TNFS (AC1, AC2);
  D1 muestra el nombre (AC3); `2`..`8` dan el mensaje y nada cambia (AC4); inválido e incompatible no tocan D1
  (AC8, AC9); `A` monta el slot (AC5, parte host); las pruebas de hoy en verde (AC10).
- **Demo** (X006): un `.xex` de prueba propio (generado en `tools/`, muestra un texto) y un juego de
  `tnfs.calaluis.cl` elegido por el responsable (AC5).

## 6. Sequencing & dependencies

X001 (fuente + cargador) y X003 (listar) son independientes. X002 (envoltura) usa las constantes del cargador: va después de X001. X004 (elegir y arrancar) necesita
las tres. X005 (`.xex` de prueba) puede ir con X001. X006 (demo) y X007 (docs) al final.

## 7. Risks & open decisions

- **Compatibilidad.** Un `.xex` que usa `$0700`-`$08FF` se rechaza (` XEX NO COMPATIBLE`); uno que espera DOS falla
  después de cargar. Se mide en la demo (área no formulable de la spec).
- **BASIC en un 400/800.** No hay `PORTB`: escribirlo no tiene efecto útil. El cargador lo hace igual; el BASIC de
  cartucho se quita a mano. Se anota en el README.
- **Pantalla tras apagar el BASIC.** Reabrir `E:` mueve la pantalla al tope de la RAM; un `.xex` que carga ahí la pisa
  (normal en juegos).
- **Tamaño del cargador.** Si no cabe en 3 sectores (384 bytes) con su búfer, la zona prohibida crece; el generador la
  exporta y el host la usa, así que no hay número duplicado.
- **Fuente del formato.** Si la fuente pública elegida no cubre `INITAD` entre segmentos, X001 se detiene y se
  consulta (decisión del responsable).

- **Desviación 1 (2026-11-20, aprobada: «Solo PORTB»).** El *Altirra Hardware Reference Manual* no cubre las
  variables del SO (`RAMTOP`, `CIOV`, `SAVMSC`); sin fuente citada no se usan (ADR-012). El cargador solo apaga el
  BASIC. La pantalla del SO queda donde estaba; un programa que lea `RAMTOP` no ve los 8 KB que libera el BASIC. El
  aviso de error usa `SDLSTL`/`RTCLOK`, que ya tienen fuente. `SIOV` reemplaza a `DSKINV` por la misma razón: el
  bloque de control de SIO ya está documentado en el proyecto.

## Tasks

Rama `feat/menu-xex` (desde main `ff9ff63`). Riesgo previsto por tarea (0/1/2); `rsc risk` decide el real.

| # | [P] | Tarea | Done-check | Depende de | Cubre | Riesgo |
|---|---|---|---|---|---|---|
| X001 | [P] | Fuente pública del formato en `docs/fuentes.md`; cargador 6502 en `tools/make_xex_loader.py` → `host/src/xex_loader.inc` | `tools/tests/test_xex_loader.py` en verde (segmentos, `INITAD`, `RUNAD`, BASIC, error de lectura, `--check`) | — | AC6, AC7 | 2 |
| X002 |  | `xex_image.{hpp,cpp}`: validar y envolver en ATR | `ctest test_xex_image`: casos de §5 | X001 (constantes del `.inc`) | AC8, AC9 | 1 |
| X003 | [P] | Listar `.XEX` en el catálogo local, el escaneo de carpeta y TNFS | `ctest` de catálogo y TNFS: `.XEX` listado, `.ATR` igual | — | AC1, AC2, AC10 | 1 |
| X004 |  | `MenuController`: elegir `.xex` (solo D1, mensajes, leer, envolver, slot), `A`, eventos | `ctest` del menú: AC3, AC4, AC8, AC9, AC5 (host); todas las pruebas de hoy en verde | X002, X003 | AC3–AC5, AC8–AC10 | 2 |
| X005 | [P] | `.xex` de prueba propio (texto en pantalla) generado en `tools/` | prueba de `tools/`: el archivo es un `.xex` válido y `wrap` lo acepta | X001 | AC5 (demo) | 0 |
| X006 |  | Demo en Altirra: `.xex` local de prueba y juego de TNFS; `2`..`8`, inválido e incompatible | carpeta `D:\rb-e0\m48-xex`: `host.jsonl`, `version.txt`, capturas; un solo host | X004, X005 | AC1–AC5, AC8, AC9 | 0 |
| X007 | [P] | Docs: ADR-017 enmienda 11 (o ADR nuevo del cargador), `host/README.md`, `docs/fuentes.md` | archivos existen y enlazan; sin emoji; `check_notices` OK | X004 | R: ADR-017 | 0 |

- Aprobado por el responsable («Sí, rama nueva») el 2026-11-20.
