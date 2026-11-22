---
type: plan
title: Plan — Unidades montadas que sobreviven al apagado del 65XE
description: CÓMO recordar D1..D8 entre encendidos sin tocar el NVS, sin retrasar D1 y con Break en el menú.
tags: [sdd, plan, firmware, sio, menu]
timestamp: 2026-11-22T00:30:00Z
topic: sdd
slug: menu-unidades-persistentes
spec: 02-DOCS/wiki/sdd/specs/menu-unidades-persistentes.md
status: approved
---

# Plan — Unidades montadas que sobreviven al apagado del 65XE

> Spec: `menu-unidades-persistentes` (aprobada en `c103f6a`, contrato del arranque y criterios 8–12 añadidos en la
> revisión del plan). Estado del plan: **aprobado** por el responsable el 2026-11-22, con el ajuste de C9 (9c).
> Implementación: PR posterior al arreglo de arranque (PR #77), en su propia rama desde `main` tras fusionar #77.

## 0. Restricciones globales (literales; toda tarea las cumple)

- **Contrato del arranque:** D1 sirve el menú dentro del plazo del PR #77 (**D1 listo < 1 s desde EN**; hoy 817 ms).
  El menú muestra `RESTAURANDO UNIDADES`. Las unidades restauradas están disponibles cuando aparece la pantalla
  principal operativa y se habilita `A`. Si la microSD falla, el menú queda operativo y muestra el motivo.
- Un montaje o desmontaje cuenta como **guardado** solo cuando `fwrite`, `fflush`, `fsync` y `fclose` terminan bien.
  El menú dice `GUARDANDO`, `GUARDADO` o `NO SE GUARDO: <motivo>`.
- La recuperación solo se garantiza si sobrevive una ranura válida y la tarjeta puede leerse.
- **Nunca** `nvs_flash_erase`, `nvs_flash_erase_partition` ni `nvs_flash_init`: el NVS puede tener datos antiguos
  de FujiNet. Este trabajo no usa el NVS.
- Sin código de FujiNet ni de terceros.
- `main/menu_sio.cpp` no añade llamadas `gpio_*` ni `uart_*`: el empaquetador las cuenta exactas.
- La tarea SIO nunca escribe en la microSD: escribir dura milisegundos y el Atari espera respuesta en ~1 ms.
- Al encender se ve siempre el menú; D1 solo arranca la imagen asignada con `A`.
- Una orden repetida por el Atari nunca aplica dos veces un montaje o desmontaje.

## 1. Contexto que fija el diseño

- spec §Qué debe respetar y §Criterios 1–12.
- PR #77: `menusio::start()` antes del montaje; `finish()` lee el catálogo y la tarea SIO lo aplica entre tramas.
  Medido por USB: D1 a 817 ms y catálogo a 1648 ms desde EN.
- Partición `nvs` en `0x9000` (24 KB), en la misma posición que FujiNet. El firmware de RetroBridge no llama hoy a
  ninguna función `nvs_*`.
- El programa del menú (`tools/make_menu.py`) lee la tecla en `CH` ($2FC) y la envía en `DAUX1`, con una secuencia
  de 3 bits que los reintentos de SIOV conservan. `menu_sio_transport.cpp` ya descarta una orden con la misma tecla
  y la misma secuencia (`menuRetries`). Break no pasa por `CH`: el SO pone `BRKKEY` ($11) a 0, y una SIOV en curso
  termina con estado 128.
- El catálogo es la raíz de la microSD; los montajes locales son nombres de ese catálogo (`assignD1(EntryIndex)`).
- El divisor del +5 V del SIO en GPIO35 ya se lee (`readSio5v`): 0 mV con el 65XE apagado, ~4,8 V encendido.

## 2. Arquitectura

```text
                 ┌──────────────── rb_menu (C++ puro, tests de host) ───────────────┐
 Atari ──SIO──▶  │ MenuController ──snapshot()──▶ DriveMemory (8 nombres)           │
  menú (6502)    │   ▲ restore() / saveResult()  │                                  │
  Break → código │   │ tecla BREAK               ▼                                  │
                 │   │                     DriveMemoryCodec (ranuras A/B, CRC, seq) │
                 │   │                     SlotFileStore (stdio, ruta de la microSD)│
                 │ SioPowerWatch (mV + tiempo → apagado / encendido)                │
                 └──────────────────────────────────────────────────────────────────┘
 main/menu_sio.cpp (pegamento ESP): la tarea SIO aplica restore, resultados y apagado; rb_drive_save escribe.
```

| Componente | Responsabilidad única | Dónde |
|---|---|---|
| `DriveMemory` | Qué nombre tiene cada unidad D1..D8 (vacío = nada) | `rb_menu`, interno |
| `DriveMemoryCodec` | Codifica y valida una ranura; elige la válida más nueva | `rb_menu`, interno |
| `SlotFileStore` | Lee y escribe las dos ranuras como archivos de tamaño fijo | `rb_menu`, interno (stdio) |
| `MenuController` (cambia) | Emite la memoria al aplicar un cambio; muestra el estado del guardado; restaura; atiende BREAK | `rb_menu` |
| `SioPowerWatch` | Detecta «el 65XE se apagó» y «se encendió» a partir del +5 V del SIO | `rb_menu`, interno |
| Tarea `rb_drive_save` | Escribe la última memoria pendiente y devuelve el resultado; fuera de la tarea SIO | `main/menu_sio.cpp` |
| Programa del menú (cambia) | Mira `BRKKEY`, envía el código BREAK y repite la SIOV abortada con la misma secuencia | `tools/make_menu.py` |

**La decisión que más pesa: persistir en la microSD, no en el NVS.**

- El NVS de esta placa lo escribió FujiNet. `nvs_flash_init` sobre un NVS de otra versión o sin páginas libres
  devuelve `ESP_ERR_NVS_NEW_VERSION_FOUND` o `ESP_ERR_NVS_NO_FREE_PAGES`, y la salida habitual es borrarlo. Eso
  queda prohibido (§0).
- Las imágenes recordadas viven en la microSD. Sin microSD no hay nada que restaurar.
- Coste: la FAT es más frágil ante cortes que el NVS. Lo cubre el formato de §4, con la garantía limitada de §0.

## 3. Interfaces y contratos

```text
DriveMemory
  name(drive 1..8) -> texto (vacío = sin imagen); igualdad por valor

DriveMemoryCodec
  encode(memoria, seq) -> bloque de tamaño fijo (magia, versión, seq, 8 nombres, CRC32)
  decode(bloque) -> memoria + seq | inválido      (magia, versión, longitudes o CRC mal → inválido)
  newest(a | inválido, b | inválido) -> memoria + seq | nada

SlotFileStore(directorio)
  load() -> memoria | nada | error(motivo)   (nada: ninguna ranura válida; error: la tarjeta no se lee)
  save(memoria) -> ok | error(motivo)        (ok solo si fwrite, fflush, fsync y fclose terminan bien;
                                              escribe la ranura que NO tiene la seq más alta; seq = última + 1)

MenuController (añadidos)
  memoryChanged() -> bool; snapshot() -> DriveMemory + número de versión
                       (cambia solo al APLICAR un montaje o desmontaje; la orden repetida no lo aplica)
  saveResult(versión, ok | motivo)   (GUARDADO si la versión es la última emitida; si no, sigue GUARDANDO)
  beginRestore()     (pantalla RESTAURANDO UNIDADES; `A` deshabilitada; el Atari sondea con POLL)
  restore(memoria | nada | error) -> por unidad: restaurada | falta | inválida(motivo)
                     (al terminar: pantalla principal operativa, `A` habilitada, avisos en la línea de mensajes)
  BREAK              (de List, Pick, Folder, FolderPick a Main; vacía la pila de carpetas; no toca unidades)

SioPowerWatch
  sample(mV, ahora) -> apagado | encendido | nada
  apagado   = +5 V < 1000 mV durante ≥ 100 ms
  encendido = después de un apagado, ≥ 4000 mV
```

- La memoria guarda **nombres del catálogo**, no índices: el catálogo puede cambiar entre encendidos.
- D1 restaurada queda **asignada**, no servida: D1 sigue sirviendo el menú hasta `A` (`menu-principal`).
- Código BREAK: un valor de `DAUX1` reservado, distinto de `NO_KEY` ($FF) y de `POLL`, que el teclado no produce en
  `CH`. El valor exacto lo fija la tarea T5 y lo prueba en Altirra.

## 4. Datos y flujo

**Formato en la microSD.** Dos archivos de tamaño fijo en la raíz: `retrobridge-unidades-a.dat` y
`retrobridge-unidades-b.dat`. Cada uno: magia, versión, `seq` (32 bits), 8 nombres de `kMaxName + 1` bytes y CRC32.

**Guardar (montaje o desmontaje aplicado).**

1. El controlador aplica el cambio, sube su versión y muestra `GUARDANDO`.
2. La tarea SIO ve `memoryChanged()` entre dos tramas, copia `snapshot()` y la deja en una cola de un hueco
   (sobrescribe: solo importa la última).
3. `rb_drive_save` la toma y llama a `save()`. La ranura destino es la que **no** tiene la `seq` más alta.
4. El archivo ya existe con su tamaño final: se abre en `r+b`, se sobrescribe en su sitio, `fflush`, `fsync`,
   `fclose`. No cambia la cadena FAT; solo el contenido y la fecha de la entrada.
5. `rb_drive_save` publica `(versión, resultado)`; la tarea SIO llama a `saveResult()` entre tramas.
6. La pantalla pasa a `GUARDADO` o a `NO SE GUARDO: <motivo>`. Con error, la unidad sigue funcionando en esta sesión.
7. La primera vez se crean los dos archivos con su tamaño final, con `seq` 0 e inválidos por CRC.

**Límite de la sincronización.** `fsync` termina cuando FatFs y el controlador SD dan la escritura por hecha. Lo
que haga después la caché interna de la tarjeta no es observable desde el ESP32. Por eso la garantía es la de §0:
una ranura válida que sobreviva y una tarjeta que se lea.

**Dos cambios seguidos y cortes.**

**Regla de recuperación:** al reiniciar, `load()` devuelve la ranura válida con la `seq` más reciente. Una
escritura incompleta falla el CRC, así que vale la anterior. Una escritura completa y válida vale aunque el menú no
llegara a mostrar `GUARDADO`. **Confirmado** = `save()` devolvió ok y la tarea SIO aplicó `saveResult()`.
Partida: E0 confirmado en la ranura X (seq n); los guardados nuevos van a la otra ranura.

| Caso | Escrituras | Último confirmado | Resultado al encender |
|---|---|---|---|
| **9a.** Cambio 1 confirmado, corte durante el guardado del cambio 2 | E1 en Y (seq n+1) ok; E2 en X, cortado | E1 | X falla el CRC; vale Y: **E1** |
| **9b-1.** Corte durante el guardado de E1, con el cambio 2 en cola | E1 en Y, cortado | E0 | Y falla el CRC; vale X: **E0** |
| **9b-2.** Los dos cambios fusionados en un guardado (E2) y corte durante él | E2 en Y, cortado | E0 | Y falla el CRC; vale X: **E0** |
| **9c.** E1 escrito en Y completo y válido; corte antes de `saveResult()` | E1 en Y ok, sin confirmar | E0 | Y válida y más reciente: **E1** |
| Cambio 2 llega mientras el 1 se escribe, sin corte | E1 en Y ok, luego E2 en X ok | E2 | `GUARDADO` cuando termina E2: **E2** |
| Corte durante la creación inicial de los archivos | Ninguna ranura válida: menú operativo sin memoria |
| Escritura fallida (tarjeta llena, protegida, sacada) | `NO SE GUARDO: <motivo>`; las ranuras anteriores quedan intactas (criterio 8) |

**Restaurar (cada encendido).**

```text
EN ─▶ start(): controller.beginRestore(); D1 = menú (≤ 1 s)     sin cambios de plazo respecto al PR #77
   ─▶ mount microSD ─▶ finish(): catálogo + store.load()          (~1,65 s desde EN)
   ─▶ finish() publica catálogo y resultado de load (bandera atómica, como el catálogo hoy)
   ─▶ tarea SIO, entre tramas: aplica catálogo, luego restore(...)
        · nombre fuera del catálogo        → unidad vacía, «D3: FALTA JUEGO.ATR»
        · montaje rechazado al validar     → unidad vacía, «D3: INVALIDA <motivo>»
        · D2..D8 válidas                   → montadas en el buzón, sirven desde ya
        · D1 válida                        → asignada; el menú sigue en D1
   ─▶ el siguiente POLL dibuja la pantalla principal operativa con las asignaciones y `A` habilitada
```

**Microsd fallida.** Si el montaje falla, `finish()` publica `error(motivo)` en lugar del catálogo. `restore` termina
al momento: pantalla principal operativa, sin unidades restauradas, con «MICROSD: <motivo>» (criterio 12). Si la
tarjeta se monta pero las ranuras no se leen, igual con «MEMORIA: <motivo>».

**Por qué basta con la pantalla principal.** D2..D8 dependen de la microSD, que se monta después de servir D1. El
Atari solo las pide desde un programa ya cargado, y el único camino a un programa es `A` en la pantalla principal
operativa. Mientras se ve `RESTAURANDO UNIDADES`, una trama a D2..D8 no se responde, como una unidad vacía hoy.

**Reencendido del 65XE con la FujiNet en USB: se prepara D1 al apagar.**

- La tarea SIO lee el ADC cada 50 ms, solo sin trama en curso, y lo pasa a `SioPowerWatch`.
- En **apagado** (+5 V < 1000 mV durante ≥ 100 ms) llama a `returnToMenu()`. D1 vuelve al menú con el Atari aún
  apagado, así que está listo antes de cualquier lectura del siguiente encendido. Las asignaciones se conservan.
- En **encendido** solo se registra la marca de tiempo, para medir.
- Se descarta actuar solo en el encendido: el Atari lee D1 a los pocos cientos de ms de encender, y el flanco con su
  histéresis puede llegar tarde.
- Un reinicio por software (`A` arranca con COLDSV) no corta el +5 V y no dispara nada. Con SIO solo, la placa
  arranca de cero cada vez y el vigilante no interviene.
- **Medida:** la consola USB registra apagado, `returnToMenu` aplicado, encendido y primera trama a D1. Se exige
  `returnToMenu` aplicado antes de la primera trama, con 2 s apagado y con el ciclo más corto que dé el interruptor
  (criterio 10). La curva de bajada del +5 V se mide en el mismo banco: si no baja de 1000 mV en un ciclo corto, se
  ajusta el umbral con ese dato.

**Break en el programa del menú.**

- **Sin orden en curso:** en el bucle de espera de tecla, el programa mira `BRKKEY`. Si vale 0, lo vuelve a $FF y
  envía el código BREAK por la ruta normal de tecla (`send`), que incrementa la secuencia.
- **Con orden en curso:** SIOV devuelve 128 (Break) en `DSTATS`. El programa:
  1. anota «Break pendiente» y vuelve `BRKKEY` a $FF (si no, la SIOV siguiente abortaría al instante);
  2. salta a una entrada `resend` situada **después** de `INC seq` y del cálculo de `DAUX2`: rehace solo `DSTATS`
     ($40, que SIOV sobrescribe con el estado) y `DTIMLO`, y vuelve a llamar a SIOV. `DAUX1` y el `DAUX2` completo
     (secuencia y fila) quedan intactos;
  3. repite hasta que la orden termina bien, y dibuja su respuesta; un 128 nunca muestra `ERROR: EL HOST NO
     RESPONDE`;
  4. solo entonces, con «Break pendiente», envía BREAK como tecla nueva por `send`.
- **En el host:** `menu_sio_transport` compara tecla y `AUX2` con la última orden. Si coinciden, reenvía la respuesta
  ya construida sin llamar al controlador (`menuRetries`). Un montaje aplicado cuya respuesta se perdió se ejecuta,
  así, una sola vez; si la primera trama no llegó, la repetición es la única que se aplica.
- **En el controlador:** BREAK en List, Pick, Folder o FolderPick vacía la pila de carpetas y vuelve a Main. En Main,
  Unmount, Edit o Create no hace nada (spec: suposición tomada para Main).

## 5. Estrategia de pruebas

| Criterio (spec) | Nivel | Qué afirma | Qué se finge |
|---|---|---|---|
| C1 asignaciones tras apagar y encender | Host + 65XE | `save` → `load` → `restore` dan las mismas D1..D8; foto del menú tras ciclo solo SIO | Directorio temporal |
| C2 `A` arranca D1 restaurada | Altirra + 65XE | Tras restaurar, `A` arranca `DEMO-DOS.ATR` hasta `DEMO DOS OK` sin elegirla | — |
| C3 D2 con la pantalla operativa | Host + Altirra | Antes de restaurar, `A` deshabilitada y D2 sin respuesta; después, D2 devuelve su imagen | Buzón y repositorio de host |
| C4 imagen borrada | Host + 65XE | Unidad vacía y aviso `FALTA`; las demás intactas | Catálogo sin el nombre |
| C5 desmontar se recuerda | Host + Altirra | Desmontar D2 cambia `snapshot()`; tras ciclo, D2 vacía | — |
| C6 menú al primer encendido solo SIO | 65XE + línea de tiempo | D1 listo < 1 s desde EN (USB) y menú al encender sin USB, dos ciclos | — |
| C7 Break en carpetas | Host + Altirra | BREAK desde List, Pick, Folder (3 niveles) y FolderPick lleva a Main; unidades sin cambio | Fuentes de prueba existentes |
| C8 guardado fallido | Host | `save` con error → `NO SE GUARDO`; la unidad sirve; tras «ciclo», la ranura previa manda | Directorio de solo lectura |
| C9a corte en el segundo, primero confirmado | Host | E1 confirmado; ranura de E2 truncada o con un byte cambiado → `load` da **E1** | Archivo manipulado tras escribir |
| C9b corte con ambos pendientes | Host | Ranura de E1 (y, aparte, de E2 fusionado) cortada sin confirmar → `load` da **E0**; la pantalla nunca mostró `GUARDADO` | `save` interrumpido a mitad |
| C9c ranura nueva válida, confirmación interrumpida | Host | `save(E1)` termina ok; el resultado nunca llega al controlador (pantalla en `GUARDANDO`); nuevo `load` → **E1** | Resultado descartado antes de `saveResult()` |
| C10 reencendido con USB | Host + 65XE | `SioPowerWatch` con secuencias de mV (2 s y 300 ms apagado); en banco, `returnToMenu` antes de la primera trama | Muestras sintéticas |
| C11 Break durante una orden | Host + Altirra | Host: montaje aplicado, respuesta perdida, misma `AUX1`/`AUX2` otra vez → un solo montaje y la misma respuesta; después BREAK con secuencia nueva → Main. Altirra: Break durante el montaje → un solo cambio, sin `ERROR: EL HOST NO RESPONDE`, y luego la pantalla principal | Respuesta descartada; trama repetida |
| C12 microSD ausente | Host + 65XE | `restore(error)` → pantalla operativa con el motivo y `A` habilitada; banco sin tarjeta | Error de montaje |
| NVS intacto | Empaquetador | Ninguna llamada `nvs_*` en `main/` ni `components/` | — |

Prueba que debe fallar primero (RED): el empaquetador con una llamada `nvs_flash_erase` de mentira rechaza el
paquete; con el árbol real, lo acepta.

## 6. Secuencia

1. **T1** `DriveMemory` + `DriveMemoryCodec` + tests (incluye corrupción). Sin dependencias.
2. **T2** `SlotFileStore` + tests: directorio temporal, solo lectura, dos cambios y ranura dañada. Depende de T1.
3. **T3** `MenuController`: versión y `snapshot`, `saveResult` con `GUARDANDO`/`GUARDADO`/`NO SE GUARDO`,
   `beginRestore`/`restore` con `A` deshabilitada, avisos FALTA/INVALIDA/MICROSD. Depende de T1.
4. **T4** `SioPowerWatch` + tests. Independiente de T1–T3.
5. **T5** BREAK: código reservado; `make_menu.py` con «Break pendiente», entrada `resend` tras `INC seq` y `DAUX2`, y
   BREAK como tecla nueva al terminar la orden; el controlador vuelve a Main; tests de C11. Independiente.
6. **T6** Pegamento ESP en `menu_sio.cpp`: `rb_drive_save` con resultado, publicación en `finish()`, restore,
   resultados y apagado en la tarea SIO; marcas de tiempo en el registro de arranque y en la consola. Depende de T2–T4.
7. **T7** Empaquetador: rechazo de `nvs_*` (con su prueba RED). Paquete CI, línea de tiempo USB y banco 65XE (C1, C2,
   C4, C6, C10, C12). Depende de todo.

T1, T4 y T5 pueden ir en paralelo.

## 7. Riesgos y decisiones abiertas

| # | Riesgo | Disparador | Impacto | Mitigación |
|---|---|---|---|---|
| R1 | El +5 V del SIO baja despacio al apagar | Condensadores del 65XE | En un ciclo corto, el apagado no se detecta y D1 no vuelve al menú | Medir la curva en T7; ajustar umbral y tiempo con el dato; el encendido queda registrado para verlo |
| R2 | FAT dañada por un corte a mitad de escritura | Corte justo al guardar | Archivo ilegible | Tamaño fijo, escritura en su sitio, dos ranuras; el peor caso es arrancar sin memoria (fuera de garantía, §0) |
| R3 | Break aborta una SIOV del menú | Break durante una orden | Menú colgado o cambio doble | `resend` con `AUX1`/`AUX2` intactos y `BRKKEY` restaurado; el transporte reenvía la respuesta sin reaplicar (T5, C11) |
| R4 | `SioPowerWatch` dispara en falso | Ruido en GPIO35 o caída breve del +5 V | D1 vuelve al menú en mitad de un juego | 100 ms bajo 1000 mV; solo afecta al siguiente arranque; el juego en curso sigue leyendo sus unidades |
| R5 | Restaurar tarda más de lo previsto | Muchas ATR o tarjeta lenta | `RESTAURANDO` se ve más tiempo | Solo ocho aperturas; T7 mide «unidades restauradas a X ms» |
| R6 | TNFS con Wi-Fi necesitará el NVS | Entrega E2 en el ESP32 | `esp_wifi_init` lo inicializa | Fuera de este plan; anotado: Wi-Fi con `CONFIG_ESP_WIFI_NVS_ENABLED=n` y sin borrar nunca |
| R7 | Leer el ADC desde la tarea SIO retrasa una respuesta | Lectura justo antes de una trama | Reintento del Atari | Solo cada 50 ms y sin trama pendiente; 8 muestras ≈ 0,2 ms |

**Decisiones abiertas:** ninguna bloquea. El valor exacto del código BREAK queda para T5, con su prueba en Altirra.

## 8. Tareas

> Rama `feat/menu-unidades-persistentes` desde `main` `861ddb7` (PR #77 fusionado). Riesgo: 0 = local y
> reversible; 1 = toca el comportamiento del menú; 2 = firmware en la placa o el NVS. Paralelo = sin archivos
> compartidos con otra tarea abierta.
>
> Comprobación común de host: `cmake --build build/host && ctest --test-dir build/host --output-on-failure`
> (abreviada «ctest»). Programa del menú: `python -I -m unittest discover -s tools/tests` («pytests»).

| # | Tarea | Criterios | Riesgo | Hecho cuando (literal) | Depende | Paralelo |
|---|---|---|---|---|---|---|
| T1 | `DriveMemory` y `DriveMemoryCodec` en `rb_menu` | C1, C9 | 0 | ctest verde con `test/host/menu/test_drive_memory.cpp`: ida y vuelta de 8 nombres; CRC, magia, versión y longitud dañados → inválido; `newest` elige la `seq` válida más alta y devuelve nada si ninguna vale | — | Sí |
| T2 | `SlotFileStore` (stdio) | C1, C5, C8, C9a, C9b, C9c | 1 | ctest verde con `test_drive_slot_store.cpp`: ida y vuelta en directorio temporal; directorio de solo lectura → `error`; 9a → E1; 9b-1 y 9b-2 → E0; 9c → E1; dos guardados alternan ranura | T1 | No |
| T3 | `MenuController`: versión, `snapshot`, `saveResult`, `beginRestore`, `restore` | C1, C3, C4, C5, C8, C12 | 1 | ctest verde con `test_menu_unidades.cpp`: `GUARDANDO`→`GUARDADO`/`NO SE GUARDO`; versión vieja no confirma; `A` deshabilitada en RESTAURANDO; `FALTA`, `INVALIDA`, `MICROSD`; D1 queda asignada y no servida; los tests de menú existentes siguen verdes | T1 | No (comparte controlador con T5) |
| T4 | `SioPowerWatch` | C10 | 0 | ctest verde con `test_sio_power_watch.cpp`: apagado tras ≥ 100 ms < 1000 mV; 60 ms bajo no dispara; encendido solo tras apagado; ciclo de 300 ms detectado | — | Sí |
| T5 | BREAK: código reservado, controlador a Main, `make_menu.py` con «Break pendiente» y `resend` | C7, C11 | 1 | ctest verde con BREAK desde List, Pick, Folder (3 niveles) y FolderPick → Main sin cambiar unidades; test de transporte: montaje aplicado, respuesta perdida, misma `AUX1`/`AUX2` → un montaje y misma respuesta, luego BREAK → Main; pytests verdes con un test que comprueba que `resend` está después de `INC seq` y que un 128 no escribe `NO_HOST_TEXT` | T3 | No |
| T6 | Pegamento ESP en `main/menu_sio.cpp` | C1–C6, C8, C10, C12 | 2 | CI verde con el paquete `menu-sio`; `check_sio_calls` sigue en 45 llamadas; la consola registra «unidades restauradas a X ms», resultados de guardado y apagado/encendido | T2, T3, T4 | No |
| T7 | Empaquetador sin `nvs_*` y banco | todos | 2 | Prueba RED: una llamada `nvs_flash_erase` falsa hace fallar el empaquetador; el árbol real pasa. Paquete CI flasheado con verify 3 de 3; línea de tiempo USB con D1 < 1 s; banco 65XE: C1, C2, C4, C6 (dos ciclos solo SIO), C10 (2 s y ciclo corto con USB), C12; Altirra: C2, C3, C5, C7, C11; evidencia en `docs/mediciones/` | T5, T6 | No |

**Orden:** T1 y T4 en paralelo → T2 y T3 → T5 → T6 → T7. Cada tarea pasa su revisión según `rsc risk` antes de la
siguiente; T6 y T7 piden aprobación antes de flashear o escribir en COM3.
