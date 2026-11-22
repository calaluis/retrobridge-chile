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

## 5. Pendiente

- Corrección del tiempo de arranque de D1 (230 ms con el cargador TNFS).
- T1a y T1b; T8.
- Nombre, ruta y SHA-256 del `.XEX` de la demo T7b.

## 6. Evidencia

`D:\GitHub\Respaldos\FujiNet-RB-menu-sio-1bb940d\` (fuera del repo): ZIP y paquete.
