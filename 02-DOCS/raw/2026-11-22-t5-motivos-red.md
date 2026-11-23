# T5 en el 65XE: motivos de red y E crea el archivo de fuentes (menu-sio-wifi-tnfs, 2026-11-22)

> Estado: **T5 cumplida en el 65XE físico** (C6 y la parte «sin archivo Wi-Fi» de C7).
> No cubre «Wi-Fi conectando», «red no encontrada» ni «clave incorrecta» en pantalla (§4).

**Convención:**
- **[H]** hecho comprobado en el equipo;
- **[P]** comprobado en la pantalla del Atari por el dueño del equipo;
- **[S]** comprobado en la microSD desde el PC.

## 1. Paquete

| Dato | Valor |
|---|---|
| Firmware | `1bb940dbb680329480d0aefb3385109ccff1def1` (rama `docs/spec-menu-sio-wifi-tnfs`, PR #83) |
| ZIP | `retrobridge-fw-menu-sio-fujinet-atari-v1x-1bb940d`, ejecución de CI `45082643271` (todos los trabajos verdes) |
| SHA-256 del ZIP | `e6dd93058f1ffdf023918786466bc3aba264af5f8db0e265199c6b3565a9482c` |
| Comprobaciones del paquete | 36 OK, ninguna falla |
| Tamaño de la app | 1 126 528 B |
| Pruebas de host | 86 de 86 verdes; 5 nuevas de T5 (`test_menu_editar`, `test_menu_tnfs`) |

## 2. Instalación

- [H] `write_flash` y `verify_flash` en 0x1000, 0x8000 y 0x10000: 3 de 3 regiones «verify OK (digest matched)».
- [H] NVS (0x9000, 0x6000) leída en memoria, solo SHA-256: antes y después
  `d8a04e883160efe379c45f9b317884c043eedd81f710366f818bd2b1d7a55c1d`, sin cambios.

## 3. Resultados

Primer encendido con el Wi-Fi y las fuentes presentes: [P] todo funciona como en T7b. El Wi-Fi conecta antes de
que se alcance a pulsar `2`, así que no se ve ningún motivo.

Prueba en un solo encendido, con `retrobridge-wifi.txt` y `retrobridge-fuentes.txt` renombrados a `.off`
(nada borrado):

| Paso | Acción | Resultado |
|---|---|---|
| 1 | `2` sin archivo de fuentes | ✅ [P] `FUENTE 2 SIN CONFIGURAR` |
| 2 | `E` en la fuente 2, `tnfs.calaluis.cl`, RETURN | ✅ [P] `FUENTE 2 GUARDADA` |
| 3 | `2` sin archivo de Wi-Fi | ✅ [P] `SIN WI-FI: FALTA RETROBRIDGE-WIFI.TXT`; la pantalla principal no cambia |
| 4 | `1` | ✅ [P] la lista de la microSD se abre |
| 5 | microSD en el PC | ✅ [S] `retrobridge-fuentes.txt` nuevo, 17 B: `tnfs.calaluis.cl` + LF; sin `.tmp` |

Después [S] `retrobridge-wifi.txt` volvió a su nombre; `retrobridge-fuentes.txt.off` queda como respaldo.
El archivo creado tiene fecha 1980-01-01: el ESP32 no tiene hora; no afecta al menú.

## 4. No visto en pantalla

- `WI-FI CONECTANDO, REPITE EN UN RATO`: el Wi-Fi conecta en ~1,7 s. Cubierto solo por las pruebas de host.
- `RED NO ENCONTRADA`, `CLAVE INCORRECTA`, `EL WI-FI NO ARRANCO`, `ARCHIVO WI-FI NO VALIDO`: cubiertos por las
  pruebas de host; en el equipo harían falta una red apagada, una clave mala o un archivo mal formado.
- Fallo de escritura de `E` (`NO SE PUDO GUARDAR EL ARCHIVO`): cubierto por las pruebas de host.

## 4b. Revisión de código de T5 y segunda prueba (87e6b0c)

Tres rondas de revisión externa sobre T5 encontraron y se corrigieron:

| Commit | Hallazgo | Corrección |
|---|---|---|
| `e5e7122` | FAT de ESP-IDF (`f_rename`) no sobrescribe: el segundo guardado con `E` fallaba en la FujiNet | Reemplazo sin sobrescribir; `recoverSourcesFile()` al arrancar |
| `e5e7122` | Tras perder el enlace, el menú daba la red por disponible 5 s | `Connecting` se publica al caer el enlace |
| `e5e7122` | Se aceptaba una tercera línea en `retrobridge-wifi.txt` | `WifiConfig` en `rb_menu`, con `test_wifi_config` |
| `a081114` | Un renombrado fallido podía borrar el archivo y su `.tmp`; un `.tmp` cortado podía darse por bueno | `.tmp` → archivo a `.bak` → `.tmp` a su nombre → borrar `.bak`; al arrancar nunca se confía en un `.tmp` |
| `a081114` | Red de 32 bytes o clave de 63 con CRLF rechazadas | El CR se retiene hasta saber si es fin de línea |
| `87e6b0c` | Reintentar `E` con solo el `.bak` presente perdía las demás fuentes | Antes de guardar se restaura el `.bak`; si no se puede, no se guarda |

Paquete `retrobridge-fw-menu-sio-fujinet-atari-v1x-87e6b0c`, CI `45086140242` verde, SHA-256 del ZIP
`da07a50881549258a127cc651f7b0fed0ab32c3c5fa494cba436cf47b6dd8333`, 36 comprobaciones OK.
[H] 3 de 3 regiones verificadas; NVS `d8a04e88…` sin cambios.

| Paso | Acción | Resultado |
|---|---|---|
| 1 | `E` en la fila vacía, `prueba.cl` | ✅ [P] `GUARDADA COMO FUENTE 3` |
| 2 | `E` en la fuente 3, `prueba2.cl` (segundo guardado sobre un archivo existente) | ✅ [P] `FUENTE 3 GUARDADA` |
| 3 | Apagar y encender | ✅ [P] fuente 2 `tnfs.calaluis.cl`, fuente 3 `prueba2.cl` |
| 4 | `E` en la fuente 3 con el campo vacío | ✅ [P] `FUENTE 3 BORRADA` |

## 5. Pendiente

- Corrección del tiempo de arranque de D1 (230 ms con el cargador TNFS).
- T1a y T1b; T8.
- Nombre, ruta y SHA-256 del `.XEX` de la demo T7b.

## 6. Evidencia

`D:\GitHub\Respaldos\FujiNet-RB-menu-sio-1bb940d\` (fuera del repo): ZIP y paquete.
