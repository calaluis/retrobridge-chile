# Banco T8 de menu-cinta: AC1–AC8 en el 65XE (guion, 2026-11-23)

> Estado: **T8 aceptada por decisión del responsable, con excepción de AC5 y evidencia testimonial** (§7), 2026-11-23. Cada fila lleva su fuente.
> Alcance: AC1–AC8 de `02-DOCS/wiki/sdd/specs/menu-cinta.md`. AC9–AC12 son T9 (tras M3a y H4, PR aparte).

**Convención:**
- **[H]** hecho comprobado en el equipo;
- **[C]** comprobado en la consola USB (COM3, MAC enmascarada; la clave nunca aparece);
- **[P]** comprobado en la pantalla del Atari por el dueño del equipo.

**Lo que este banco NO prueba:** que una cinta cargue. El paquete tiene `kTapeOutput = false`: la casetera nunca
toma DATA IN. Cargar una cinta es AC9 (T9).

## 0. Aprobaciones, una por paso

Cada paso pide su propio «sí». Un «sí» no cubre el siguiente.

| Paso | Qué se pide | Aviso |
|---|---|---|
| A | Descargar el ZIP `menu-sio` de la ejecución de CI de la cabeza del PR #85 | Se da nombre, ejecución y tamaño antes |
| B | Leer la NVS (0x9000, 0x6000) en memoria, solo su SHA-256 | Abrir COM3 puede **reiniciar el ESP32** |
| C | Flashear 0x1000, 0x8000 y 0x10000 con `verify_flash` | **SIO desconectado** del 65XE |
| D | Crear `mala.cas` en la raíz de la microSD (AC6) | Archivo nuevo; no se borra nada |
| E | Abrir COM3 para leer la consola durante el ensayo | Puede reiniciar el ESP32 |

Reglas que siguen valiendo: no borrar la NVS ni mostrar su contenido; no abrir `retrobridge-wifi.txt`;
en la microSD no se borra nada (lo que sobre se renombra a `.off`); un solo `retrobridge_host` por hub NetSIO
(aquí no se usa el host).

## 1. Paquete

| Dato | Valor |
|---|---|
| Firmware | `bea39e1dfaeffd31b54b56da3d46c33e541aa378` (rama `spec/menu-cinta`, PR #85); `esp_app_desc='bea39e1'` |
| ZIP | `retrobridge-fw-menu-sio-fujinet-atari-v1x-bea39e1` (5 962 497 B), ejecución de CI `45113237250`, 6 de 6 trabajos en verde |
| SHA-256 del ZIP | `b9fbe7db80a6d137116cc24a6c38467d7d5030a46e415b4a33da5203baeee662` |
| Comprobaciones del paquete | 35 OK, entre ellas «sin nvs_* en main/ ni components/», «puertas H4 y M3a sin aprobar» (perfil y registro) y la pila de `app_main` (1872/3584) y `rb_drive_save` (1808/6144) |
| Tamaño de la app | 1 155 120 B de 4 MB (72,5 % libre); antes 1 125 456 B |

Antes de descargar: CI de esa cabeza en verde en los cuatro trabajos (host, layering, `menu-sio`, build ESP-IDF).

## 2. Instalación

**Hecho el 2026-11-23** (SIO desconectado):
- [H] NVS antes: `d8a04e883160efe379c45f9b317884c043eedd81f710366f818bd2b1d7a55c1d` (leída dos veces, en el paso B y
  en el script de flasheo).
- [H] `write_flash` en 0x1000, 0x8000 y 0x10000, «Hash of data verified» en las tres; `verify_flash`: 3 de 3
  «verify OK (digest matched)».
- [H] NVS después: `d8a04e883160efe379c45f9b317884c043eedd81f710366f818bd2b1d7a55c1d`, igual.

Procedimiento:

1. [H] SHA-256 de la NVS antes del flasheo. Valor esperado, el de los bancos anteriores:
   `d8a04e883160efe379c45f9b317884c043eedd81f710366f818bd2b1d7a55c1d`.
2. [H] SIO desconectado. `write_flash` y `verify_flash` en las tres regiones: 3 de 3 «verify OK».
3. [H] SHA-256 de la NVS después: igual al de antes.
4. [H] microSD: `international_karate.cas` en la raíz (ya está desde `cassette-cas`); crear `mala.cas`
   (paso D, ver AC6). Las `.ATR` y `.XEX` de siempre siguen ahí.
5. ~~[H] Aislamiento del pin 10 sin alimentación~~ — **sustituido** por el montaje aceptado (abajo): la FujiNet
   comercial usa los 13 pines.
6. [C] Con el USB conectado y el 65XE encendido, se **anota** la lectura de «SIO +5V (pin 10)» (~4780 mV con el
   montaje de fábrica). Ya no es una condición de parada.

**Montaje aceptado por el responsable (2026-11-23):** T8 se hace con la FujiNet comercial tal como viene, con USB y
SIO conectados por los 13 pines. Palabras del responsable: «No hay nada que construir físicamente. El hardware de
ESP32 Fujinet está probado y certificado y no lo construí yo, lo compré ya hecho desde EEUU» y «Sí, regístralo y
seguimos con la segunda sesión». Base [E]: USB y SIO_5V llegan a VCC por diodos Schottky (D4, D5). La lectura del
pin 10 se anota y **ya no detiene el banco**. El riesgo R1 (MOTOR → IO36) sigue registrado como riesgo de diseño;
`menu-sio` no configura IO36 sin M3a. Esto **no** aprueba M3a ni H4, ni el montaje solo por SIO (AC5 sigue
bloqueado por T1a/T1b). El adaptador de `docs/hardware/adaptador-banco-t8.md` queda descartado.

**Primera vez con la ranura v2 (riesgo R4 del plan):** la memoria de unidades se guarda ahora en formato v2,
con `C:`. Volver después a un firmware anterior deja D1–D8 vacías. No se pierde ningún archivo de la microSD.

## 3. Arranque: lo que la consola debe mostrar [C]

En este orden, tras encender con el USB conectado:

```text
D1 ready at <N> ms (before the microSD and the catalog)
catalog: <n> image(s) on the microSD root
PSRAM libre antes de la casetera: <B> B (bloque mayor <B> B)
PSRAM libre despues de la casetera: <B> B (bloque mayor <B> B); RAM interna libre <B> B
cas: listo (puertas cerradas)
cas: deck en el menu (puertas cerradas)
cas: sin M3a/H4, la cinta no se reproduce; DATA IN queda para el disco
unidades restauradas a <t> ms (...); guardado activo
```

Se anotan: `N` de «D1 ready», la PSRAM antes y después, y el bloque mayor.

**Nunca debe aparecer:** `DATA IN: cambio`, `uart_driver_delete`, `cas: no arranca`, `cas: sin memoria`,
`panic`, `Guru Meditation`.

## 4. Casos

Las teclas se pulsan en el 65XE con el menú en pantalla. La línea de mensajes es la última del menú.
**Límite de la consola:** los eventos `menu_open`, `menu_select` y `menu_restore` (todas las líneas
`C: name=…`) salen en nivel DEBUG y este paquete no los imprime. Solo salen `menu_mount`, `menu_tnfs`,
`menu_fetch` y `menu_source_rejected`. Por eso AC1–AC6 se prueban en pantalla [P]; la columna de consola dice
lo que **no** debe aparecer.

| AC | Montaje | Teclas | Pantalla esperada [P] | Consola esperada [C] | Resultado |
|---|---|---|---|---|---|
| **AC1** | microSD con `international_karate.cas` | `1` (fuente 1, microSD) | La lista muestra `INTERNATIONAL_KARATE.CAS` con su letra, junto a las `.ATR` y `.XEX` | — | ✅ [P] testimonio del responsable (2026-11-23): «todo eso ya lo hace»; sin foto ni consola. Antes: 🟡 parcial, inferido: la cinta se eligió desde la lista de la fuente 1 (AC2 lo supone); no consta su letra junto a las `.ATR` y `.XEX` |
| **AC2** | Lista de la fuente 1 abierta | letra de la cinta, luego `C` | Principal: `C: INTERNATIONAL_KARATE.CAS` (o recortado); mensaje `CINTA EN C:, AUN NO PUEDE CARGARSE`; D1–D8 sin cambio | ningún `menu_mount` (D1–D8 no cambian) | ✅ [P] testimonio del responsable (2026-11-23): «todo eso ya lo hace»; sin foto ni consola. Antes: 🟡 parcial: [P] foto con `C: INTERNATIONAL_KARATE.CAS` y D1–D8 `SIN DISCO` sin cambio; [C] ningún `menu_mount`. Falta el mensaje de montaje |
| **AC3a** | Lista abierta | letra de la cinta, luego `1` | `UNA CINTA VA EN C:`; ninguna unidad cambia | ningún `menu_mount` | ✅ [P] testimonio del dueño: vio `UNA CINTA VA EN C:` |
| **AC3b** | Lista abierta | letra de una `.ATR`, luego `C` | `UN DISCO NO VA EN C:`; `C:` sin cambio | ningún `menu_mount` | ✅ [P] testimonio del dueño: vio `UN DISCO NO VA EN C:` |
| **AC4** | Cinta en `C:` | `U`, luego `C` | Tras `U`: `DESMONTAR: PULSA 1-8 O C, ESC CANCELA`; tras `C`: línea `C: SIN CINTA` | — | ✅ [P] testimonio del dueño: vio `C: SIN CINTA` |
| **AC5-USB** | Volver a montar la cinta (AC2). USB conectado; cable SIO de fábrica de 13 pines (montaje aceptado, §2). El guion original pedía el pin 10 aislado; ese montaje no existe | Apagar el 65XE; pulsar el reset de la FujiNet; esperar `D1 ready`; encender el 65XE | El menú sigue mostrando `C: INTERNATIONAL_KARATE.CAS`; D1–D8 iguales | `unidades restauradas …`; `D1 ready at …` | ✅ [P] testimonio del responsable (2026-11-23): «todo eso ya lo hace»; sin foto ni consola. La captura no registra su ejecución (un solo arranque del ESP32). Antes: ❓; repetir |
| **AC5** | *(bloqueado, ver abajo)* FujiNet solo por SIO, USB desconectado | Apagar el 65XE, esperar 5 s, encenderlo | Igual que AC5-USB | — (sin USB no hay consola) | ⏸ bloqueado por T1a/T1b |
| **AC6** | `mala.cas` en la raíz (paso D) y la cinta buena en `C:` | letra de `MALA.CAS`, luego `C` | `CINTA NO VALIDA`; `C:` sigue mostrando la cinta anterior | — | ✅ [P] testimonio del responsable (2026-11-23): «todo eso ya lo hace»; sin foto ni consola. Antes: 🟡 parcial, testimonio general: «reconoce archivos .cas válidos e inválidos»; no consta el texto `CINTA NO VALIDA` ni que `C:` conservara la cinta anterior |
| **AC7** | Cinta en `C:`, sin START | Encender el 65XE con el USB conectado | El menú sale en D1 como siempre | `D1 ready at N ms` con **N ≤ 280 ms** (hoy ~230 + 50) | ✅ [C] solo el tiempo: `D1 ready at 232 ms`; registro `listo_ms=232`. ATR, XEX y TNFS en AC7b–d |
| **AC7b** | Igual | Fuente 1: una `.ATR` a D1, `A` | El juego ATR corre | `menu_mount …` | ✅ [P] testimonio del responsable (2026-11-23): «todo eso ya lo hace»; sin foto ni consola. Antes: ❓ ningún `menu_mount` en la consola; repetir |
| **AC7c** | Igual | Fuente 1: una `.XEX` a D1, `A` | El programa corre | `menu_mount …` | ✅ [P] testimonio del responsable (2026-11-23): «todo eso ya lo hace»; sin foto ni consola. Antes: ❓ ningún `menu_mount` en la consola; repetir |
| **AC7d** | Igual, Wi-Fi ya conectado | Fuente 2 (TNFS): una `.ATR` a D1, `A` | El juego corre | `Wi-Fi connected in … ms`, `sources: 1 TNFS server(s)` | ✅ [P] testimonio del responsable (2026-11-23): «todo eso ya lo hace»; sin foto ni consola. Antes: ❓ ningún `menu_mount` en la consola; repetir |
| **AC8** | Todo el ensayo | — | AC2 dijo `AUN NO PUEDE CARGARSE` | `cas: sin M3a/H4 …` al arrancar; **ninguna** línea `DATA IN: cambio` en todo el ensayo; ningún `uart_driver_delete` | ✅ [P] testimonio del responsable (2026-11-23): «todo eso ya lo hace»; sin foto ni consola. Antes: 🟡 parcial: [C] `cas: sin M3a/H4 …` al arrancar; 0 `DATA IN: cambio`, 0 `uart_driver_delete`, 0 `panic` en toda la captura. Falta ver en pantalla `CINTA EN C:, AUN NO PUEDE CARGARSE` |

**Cómo crear `mala.cas` (paso D):** un archivo de texto con una línea cualquiera, por ejemplo `NO ES UNA CINTA`,
guardado como `mala.cas`. No empieza con un bloque `FUJI`, así que la casetera la rechaza. Al terminar el banco se
renombra a `mala.cas.off`; no se borra.

**AC5 está bloqueado por T1a y T1b.** AC5 pide la FujiNet alimentada solo por el pin 10. El plan
`menu-sio-wifi-tnfs` (§8, T8 y «Orden») no permite ese montaje hasta que T1a y T1b confirmen media y pico ≤ 50 mA
en cada fase. Hoy no hay registro de T1a ni de T1b en `02-DOCS/raw/`. Por eso:
- el banco hace **AC5-USB**, que prueba la restauración de `C:` tras un reinicio de la FujiNet, pero **no** cumple
  AC5;
- AC5 queda pendiente hasta que se cumpla una de estas dos condiciones:
  1. T1a y T1b están confirmados, con su registro;
  2. el responsable da una autorización escrita para un ensayo limitado solo por SIO, y esa autorización se copia
     aquí con su fecha.
- **Procedimiento original (anterior a la aceptación del montaje, §2), conservado como registro.** Suponía un cable
  de AC5-USB con el pin 10 aislado: sin USB, la FujiNet
  queda sin alimentación, así que AC5 no puede hacerse con él. Pasos:
  1. Apagar el 65XE y desconectar el USB. Los dos equipos quedan sin tensión.
  2. Cambiar al cable SIO con el pin 10 conectado. [H] El multímetro da continuidad entre el pin 10 del lado del
     65XE y J1 pin 10, y también en GND.
  3. Con el USB desconectado, encender el 65XE y hacer AC5.
  4. **Para volver a un ensayo con USB:** apagar todo, poner otra vez el cable aislado y repetir el paso 5 de §2
     (circuito abierto en el pin 10, sin tensión) antes de conectar el USB. Nunca conectar el USB con el pin 10
     conectado.
- **Excepción aceptada para T8 (2026-11-23):** el responsable aceptó el cable de fábrica de 13 pines, con USB y
  pin 10 conectados a la vez (§2). La regla del paso 4 no se aplicó en T8. La excepción vale solo para este banco:
  no aprueba la alimentación solo por SIO (AC5), ni resuelve R1 (pin 8), ni M3a/H4.
- Un ensayo limitado **no aprueba** la alimentación solo por SIO. Su resultado es solo [P]; un arranque posterior
  con USB no lo prueba, porque repite la restauración con otra alimentación.

## 5. Cierre del ensayo

1. [C] Consola: `sio arranques`. Debe dar `brownout=0` y `panic=0`. Se copian las dos líneas del último registro
   (`version=… listo_ms=… primera_trama_ms=…`).
2. [C] El `listo_ms` del registro también ≤ 280 ms (AC7).
3. [H] Renombrar `mala.cas` a `mala.cas.off` en la microSD.
4. Rellenar las tablas de §1 y §4 y la sección siguiente.

## 6. Desviaciones del montaje previsto

- **El pin 10 no estuvo aislado durante las pruebas.** [C] Al arrancar, con el 65XE apagado: `SIO +5V (pin 10) 0 mV`.
  Al encender el 65XE: `65XE on at 220827 ms (SIO +5V 4780 mV)`. El USB y el +5 V del Atari estuvieron conectados a
  la vez a las entradas de alimentación de la placa, contra §2 paso 6 (debía pararse). Si los dos entregaron
  corriente no está demostrado. No estuvieron unidos directamente: según el esquema 1.3, cada uno llega
  a VCC por su diodo Schottky (USB → D4, pin 10 → D5) [E]; el reparto de corriente no está medido. [C] Sin efecto
  visible: `brownout=0`, `panic=0`, un solo arranque en la captura; eso no prueba que el montaje sea seguro. Es la
  misma desviación que en la demo E2.
- **Segunda sesión (2026-11-23), parada al empezar.** [C] `65XE on at 21301 ms (SIO +5V 4780 mV)` con el cable
  «revisado». El responsable aclaró que la FujiNet usa los 13 pines del SIO: no hay cable con el pin 10 aislado, y el
  pin 8 (MOTOR → IO36) también pasa. Ninguna prueba AC. La propuesta de montaje está en
  `docs/hardware/adaptador-banco-t8.md` (no aprobada).
- **Sin rastro en la consola de AC5-USB ni de AC7b–d:** un solo arranque del ESP32 y ningún `menu_mount` de una
  imagen. La memoria de unidades se guardó tres veces (versiones 1–3), lo que cuadra con montar, desmontar y
  volver a montar `C:`.
- **Arrancar la cinta:** el dueño observó que con un `.cas` en `C:` no hay forma de arrancarlo. Es lo esperado en
  T8 (AC8, `kTapeOutput = false`); arrancar es AC9, en T9. La línea de ayuda del menú tampoco nombra la tecla `C`.

## 7. Veredicto

**T8 aceptada por decisión del responsable, con excepción de AC5 y evidencia testimonial (2026-11-23).** No es una
validación física completa: los criterios de la lista de testimonio no tienen evidencia individual de pantalla. Decisión del responsable: «A, cierra T8 con mi
testimonio».

- **Evidencia [C] (consola):** tiempo de D1 de AC7 (`D1 ready at 232 ms`, `listo_ms=232`) y AC8 en consola
  (`cas: sin M3a/H4…`; ninguna línea `DATA IN: cambio`, `uart_driver_delete` ni `panic`). NVS igual antes y después
  del flasheo; registro con `brownout=0`, `panic=0`.
  `sio arranques` (registro acumulado, leído al cerrar la tercera sesión): tres arranques con el 65XE encendido (`sio5v_arranque_mv=4780`,
  `tramas=20`, `ordenes=1`, `cortas=0`), todos con `brownout=0` y `panic=0`.
- **Evidencia [P], foto:** la línea `C: INTERNATIONAL_KARATE.CAS`, con D1–D8 sin cambios (AC2).
- **Testimonio del responsable, sin foto ni consola:** AC1, el mensaje de montaje de AC2 y AC8 en pantalla, AC3a,
  AC3b, AC4, AC6, AC5-USB y AC7b–d. En la primera sesión: «Sí, los vi» (AC3, AC4) y «reconoce archivos .cas
  válidos e inválidos» (AC6). En la tercera: «todo eso ya lo hace». La consola de la tercera sesión solo registra un
  encendido del 65XE, sin pruebas nuevas.
- ⏸ **AC5** (solo por SIO): bloqueado por T1a/T1b.
- **Montaje:** el de fábrica, aceptado por el responsable (§2). El pin 10 marcó 4780 mV con el 65XE encendido.
  Aceptar el montaje no resuelve el riesgo eléctrico R1 (pin 8 → IO36); M3a y H4 siguen pendientes.
- **Pendiente para T9** (requisitos 3–5 del plan): arrancar `C:` con `A` y OPTION+START. El responsable confirmó que
  hoy no lo hace; es lo esperado sin M3a/H4.
