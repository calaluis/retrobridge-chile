# Banco T8 de menu-tnfs-persistente: unidades de red en el 65XE (guion, 2026-11-23)

> Estado: **guion preparado, no ejecutado.** Ningún paso tiene aprobación todavía. Sin flasheo, COM3 ni conexión
> física hasta que el responsable apruebe cada paso de §0.
> Alcance: AC1–AC7 y AC9 de `02-DOCS/wiki/sdd/specs/menu-tnfs-persistente.md`. AC8 solo por SIO está bloqueado
> (§4); aquí se mide su versión con USB. Riesgo R2 del plan: se mide aquí. T9 sigue bloqueado.

**Convención:**

- **[H]** hecho comprobado en el equipo.
- **[C]** comprobado en la consola USB (COM3). La MAC va enmascarada y la clave nunca aparece.
- **[P]** comprobado en la pantalla del Atari por el dueño del equipo.

**Lo que este banco NO prueba:** la seguridad eléctrica del montaje ni M3a/H4. Las pruebas de host no validan nada
eléctrico.

## 0. Aprobaciones, una por paso

Cada paso pide su propio «sí». Un «sí» no cubre el siguiente.

| Paso | Qué se pide | Aviso |
|---|---|---|
| M | **Montaje USB + SIO para este banco.** El plan (§1) pide autorización específica. La excepción de `menu-cinta` T8 valía solo para aquel banco | Sin este «sí» no se conecta nada |
| A | Descargar el ZIP `menu-sio` de la ejecución de CI de main **después de fusionar el PR #98** | Se dan nombre, ejecución y tamaño antes |
| B | Leer la NVS (0x9000, 0x6000) en memoria, solo su SHA-256 | Abrir COM3 puede **reiniciar el ESP32** |
| C | Flashear 0x1000, 0x8000 y 0x10000 con `verify_flash` | **SIO desconectado** del 65XE |
| D | Preparar el servidor TNFS de la fuente 2: una `.ATR`, una `.XEX` y una `.ATR` grande (AC6). Para AC5, **renombrar** una imagen a `.off` y devolverla al final | No se borra nada en el servidor |
| E | Abrir COM3 para leer la consola durante el ensayo | Puede reiniciar el ESP32 |

Reglas que siguen valiendo:

- No borrar la NVS ni mostrar su contenido. No abrir `retrobridge-wifi.txt`.
- En la microSD y en el servidor no se borra nada. Lo que sobra se renombra a `.off`.
- Un solo `retrobridge_host` por hub NetSIO. Aquí no se usa el host.
- Ni el SSID ni la IP del servidor van al repositorio. El servidor se nombra «servidor de la fuente 2».

## 1. Paquete

| Dato | Valor |
|---|---|
| Firmware | main con T7 (`5b9cfc1`, PR #96) y el PR #98. La cabeza exacta se completa en el paso A |
| ZIP | se completa en el paso A: nombre, ejecución de CI, tamaño |
| SHA-256 del ZIP | se completa en el paso A |
| Comprobaciones del paquete | se copian del registro de CI («puertas H4 y M3a sin aprobar», pilas de `app_main` y `rb_drive_save`) |

Antes de descargar: la CI de esa cabeza debe estar en verde en todos los trabajos. El 2026-11-23, la ejecución
`45283043206` de `5b9cfc1` aún estaba en curso.

## 2. Instalación

Procedimiento (igual que en `menu-cinta` T8):

1. [H] SHA-256 de la NVS antes del flasheo. Valor de los bancos anteriores:
   `d8a04e883160efe379c45f9b317884c043eedd81f710366f818bd2b1d7a55c1d`.
2. [P] **Antes de flashear**, foto del menú principal con D1–D8 y `C:` tal como están. Es la referencia de AC7.
3. [H] SIO desconectado. `write_flash` y `verify_flash` en las tres regiones: 3 de 3 «verify OK».
4. [H] SHA-256 de la NVS después: igual al de antes.

**Primera vez con la ranura v3 (riesgo R5 del plan):** el primer guardado escribe v3. Si después se vuelve a un
firmware anterior, este toma la otra ranura v2. Restaura las asignaciones del último guardado v2, no las más nuevas.
Las unidades de red no existen en v2 y vuelven vacías. No se pierde ningún archivo.

## 3. Arranque: lo que la consola debe mostrar [C]

```text
D1 ready at <N> ms (before the microSD and the catalog)
catalog: <n> image(s) on the microSD root
unidades restauradas a <t> ms (...); guardado activo
Wi-Fi connected in <ms> ms
sources: <n> TNFS server(s)
```

Se anotan `N` de «D1 ready» y el `t` de «unidades restauradas».

**Nunca debe aparecer:** `panic`, `Guru Meditation`, `DATA IN: cambio`, `uart_driver_delete`.

**Consola de la restauración (decisión O1, resuelta el 2026-11-23: opción b).** Hasta `5b9cfc1`, los eventos
`menu_restore` (`net start`, `net restored`, `net failed`, `net dropped`, `net paused`) salían en nivel DEBUG, y
la imagen restaurada no emite `menu_mount`. La restauración de red no dejaba ninguna línea. El responsable eligió un
PR previo, solo de firmware, que imprime `menu_restore` en nivel INFO: PR #98. **El paso A usa la cabeza de main
después de fusionar el PR #98**, no `5b9cfc1`.

Al copiar líneas al registro, el servidor de `net start` se escribe como «servidor de la fuente 2»: sin IP ni
nombre de host.

## 4. Casos

Las teclas se pulsan en el 65XE con el menú en pantalla. «Línea de D2» es la fila de D2 del menú principal.
La columna de consola vale con el paquete que incluye el PR #98.

| AC | Montaje | Acción | Pantalla esperada [P] | Consola esperada [C] | Resultado |
|---|---|---|---|---|---|
| **AC1** | D2 montada desde la fuente 2 (`.ATR` pequeña); servidor encendido | Apagar y encender el 65XE | Línea de D2: `D2: CARGANDO <nombre>`; luego `D2 <nombre>`. El Atari lee D2 (por ejemplo, `DIR` en DOS) | `D2 net start server=… path=…`, luego `D2 net restored name=…` | pendiente |
| **AC2** | `.XEX` de la fuente 2 asignado a D1 | Encender; esperar a que termine la carga; `A` | Mientras carga, `A` responde `D1 CARGANDO, ESPERA`. Después, `A` arranca el programa | `D1 net restored … xex=1` | pendiente |
| **AC3** | D1 de la microSD; D2 de la fuente 2 (`.ATR` grande) | Encender; `A` mientras D2 dice `CARGANDO` | El programa de D1 arranca; D2 sigue cargando | `D1 ready at N ms` con N en el plazo de `menu-unidades-persistentes` | pendiente |
| **AC4** | D2 de red recordada; **servidor de la fuente 2 apagado** | Encender; esperar 30 s | Línea de D2: `D2: SIN RED`; el menú responde | `D2 net failed: no network` | pendiente |
| **AC4b** | Igual, después de AC4; servidor encendido | Apagar y encender el 65XE | D2 vuelve como en AC1 | `D2 net restored …` | pendiente |
| **AC5** | D2 de red recordada; en el servidor, su imagen renombrada a `.off` (paso D) | Encender | `D2: FALTA <nombre>`; D2 queda vacía | `D2 net failed: missing` | pendiente |
| **AC5b** | Después de AC5 | Apagar y encender el 65XE | D2 vacía, sin `CARGANDO` | ningún `D2 net start` | pendiente |
| **AC6** | D2 de red con la `.ATR` grande, para que dure la carga | Con `D2: CARGANDO`: fuente 1, una `.ATR` local, `2` | D2 muestra la imagen local; nunca la de red | `D2 net dropped: user` | pendiente |
| **AC6b** | Igual | Con `D2: CARGANDO`: `U`, luego `2` | `D2 DESMONTADA`; D2 vacía | `D2 net dropped: user` | pendiente |
| **AC6c** | Después de AC6 o AC6b | Apagar y encender el 65XE | D2 sigue con la elección del usuario | ningún `D2 net start` | pendiente |
| **AC7** | Primer arranque del firmware nuevo (la NVS tiene v2 de `menu-cinta`) | Encender tras el flasheo | D1–D8 y `C:` iguales a la foto de §2 paso 2 | `unidades restauradas …` sin problemas | pendiente |
| **AC9** | D2 recordada desde la fuente 2 | `E` en la fuente 2: apuntarla a otro nombre de servidor; apagar y encender | D2 vuelve desde el servidor **original** | `D2 net start server=<original> …` | pendiente |
| **R2** | D2 de red con la `.ATR` grande | Con `D2: CARGANDO`, abrir la fuente 2 y navegar; volver con `Esc` | La lista responde; al volver, D2 sigue cargando y termina | `D2 net paused: user job`, luego `D2 net start` otra vez | pendiente; anotar segundos de descarga repetida |

**Después de AC9:** devolver la fuente 2 a su servidor original con `E`.

**AC8 está bloqueado por T1a y T1b.** AC8 pide encender solo por SIO. Hoy no hay registro de T1a ni de T1b en
`02-DOCS/raw/`. Por eso el banco hace **AC8-USB**:

- **AC8-USB.** Con D2 de red recordada, encender con USB. `listo_ms` de `sio arranques` y `D1 ready at N ms` deben
  ser ≤ 282 ms. La referencia es 232 ms (`menu-cinta` T8), más 50 ms.
- AC8 queda pendiente hasta que T1a/T1b estén confirmados, o hasta una autorización escrita del responsable para un
  ensayo limitado solo por SIO, copiada aquí con su fecha.

## 5. Cierre del ensayo

1. [C] Consola: `sio arranques`. Debe dar `brownout=0` y `panic=0`. Se copian las líneas del último registro.
2. [H] En el servidor, devolver el nombre de la imagen renombrada en AC5.
3. [P] La fuente 2 apunta otra vez a su servidor original (después de AC9).
4. Rellenar §1, §4, §6 y §7.

## 6. Desviaciones del montaje previsto

(vacío hasta el ensayo)

## 7. Veredicto

Pendiente. Este documento solo prepara el banco. No aprueba M3a, H4, el montaje solo por SIO ni T9.
