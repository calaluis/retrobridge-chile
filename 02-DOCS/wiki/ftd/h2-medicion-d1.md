---
type: ftd
title: Propuesta — Medir el ancho y el ruido de COMMAND en el 65XE (D1 de H2, issue #125)
description: Plan de medición, solo documental, para obtener la evidencia física que pide la decisión D1 de la spec h2-command-pulse. No se ejecuta nada sin autorización expresa.
tags: [ftd, medicion, sio, command, h2, issue-125]
timestamp: 2026-11-28T06:30:00Z
branch: test/issue-125-command-pulse
status: propuesta
---

# Propuesta — Medir el ancho y el ruido de COMMAND en el 65XE (D1 de H2)

**Estado:** propuesta para revisión. **No** está autorizada. No se escribe código, no se flashea, no se
usa COM3, no se toca el cableado ni se hace ninguna prueba física con este documento.

## Intent

La spec `02-DOCS/wiki/sdd/specs/h2-command-pulse.md` (`4740c75`, `draft`) deja abierta la **D1**:
qué ancho mínimo de COMMAND debe contar como «COMMAND visto», y si hace falta un filtro de ruido. El
responsable decidió que D1 solo se cierra con una **medición física**. Este documento propone cómo
obtenerla.

## Qué hay que saber (preguntas de medición)

- **Q1.** Ancho real de COMMAND en las órdenes SIO del 65XE: mínimo, máximo y distribución.
- **Q2.** Si hay pulsos espurios en COMMAND (bajadas que no son órdenes) y de qué ancho, en los
  estados que importan para H2: menú, carga de cinta con MOTOR encendido, y juego cargado con MOTOR
  apagado.
- **Q3.** Si existe una separación clara entre Q1 y Q2. Esa separación es la que permitiría fijar un
  umbral, o decidir que no hace falta filtro.

## Qué no mide

- El estado de MOTOR en el instante de la aserción (D2). Leer MOTOR es GPIO36, que **sigue fuera de
  alcance** y sin aprobación.
- La causa de «EL HOST NO RESPONDE». Esta medición **no** atribuye H2 al error físico.
- Las cuestiones eléctricas de GPIO21 y H4, la #126 y la #127.

## Estado actual: lo que el firmware ya puede ver

- COMMAND entra por **GPIO39**, un pin solo de entrada (`main/menu_sio.cpp:822`).
- `EspSioUart::waitCommandLevel()` muestrea el nivel en un bucle y espera con `vTaskDelay` cuando
  quedan 1 ms o más. Su resolución práctica ronda 1 ms, y solo mira mientras la sesión espera una
  orden.
- `SioLineTransport` guarda `commandAssertedAtUs` y `commandReleasedAtUs` de cada ventana. No miden
  pulsos fuera de la espera y no se registran en la consola.
- **Conclusión:** el firmware actual no basta para Q1–Q3. Hace falta un instrumento nuevo.

## Alternativas

| | A. Analizador lógico pasivo | B. Firmware de medición | C. Datos actuales |
|---|---|---|---|
| Qué es | Sonda en COMMAND y masa del conector SIO, muestreo de 1 MHz o más | Un paquete aparte que marca la hora de cada flanco de GPIO39 por interrupción y lo publica por la consola | `commandAssertedAtUs` y `commandReleasedAtUs` de hoy |
| Resolución | µs, independiente de la placa | µs, pero con la latencia de la interrupción | unos 1 ms, solo durante la espera |
| Ve ruido real | sí, en el conector | sí, **en la entrada del ESP32**, que es lo que el firmware detectaría | no |
| Exige | tocar el conector SIO (cambio de cableado), un analizador que tolere 5 V | código nuevo en una rama aparte, flasheo y COM3 | nada |
| Riesgo eléctrico | cortocircuito o masa mal puesta en el SIO | ninguna salida GPIO; GPIO39 solo es de entrada | ninguno |
| Riesgo de medición | ninguno propio | **a verificar:** la errata de Espressif para ESP32 describe pulsos espurios breves en GPIO36 y GPIO39 con ciertos periféricos (ADC, Wi-Fi en ahorro). Hay que comprobar el texto vigente antes de diseñar, y registrarlo como resultado si aparece. | no responde a Q1–Q3 |

**C se descarta:** no responde a las preguntas.

## Recomendación

**B, con A como control si el responsable dispone de un analizador lógico que tolere 5 V.**

- B mide lo que importa para D1: lo que **ve la entrada del ESP32** después del circuito de la placa,
  sin tocar el cableado y sin ninguna salida GPIO.
- B se construiría como un **paquete de medición aparte**, en su propia rama, sin cambiar el
  firmware de producción ni `main` hasta una revisión.
- A solo añade independencia frente a la latencia de interrupción y a la posible errata de GPIO39, y
  exige tocar el conector, así que es opcional.

## Escenarios propuestos

Cada paso físico lo hace el responsable, con una acción explícita y una autorización expresa.

| Id | Estado del 65XE | Esperado | Responde a |
|---|---|---|---|
| S1 | Menú en reposo, 60 s, sin teclas | ninguna orden; cuenta pulsos espurios | Q2 |
| S2 | Menú navegando (listar, montar, U y C) | órdenes reales; distribución de anchos | Q1 |
| S3 | Carga de un `.cas` con MOTOR encendido | COMMAND alto; cuenta pulsos espurios | Q2 |
| S4 | Juego cargado y corriendo, MOTOR apagado, 60 s | el caso de H2; cuenta pulsos espurios | Q2 |
| S5 | Repetir S2 y S4 una segunda vez | repetibilidad | Q1, Q2 |

La propuesta no fija el número de repeticiones definitivo; se decide en la revisión.

## Evidencia y regla de decisión para D1

**Evidencia:**
- registro bruto de flancos (marca de tiempo, nivel, escenario);
- un resumen por escenario: número de pulsos, ancho mínimo, máximo y percentiles;
- copia en `D:\GitHub\Respaldos\` con `MANIFEST.sha256`.

Nada de esto contiene SSID, IP, claves ni MAC sin enmascarar.

**Regla propuesta para D1, que decide el responsable:**
1. Si no aparecen pulsos espurios y todas las órdenes reales superan un ancho W: **sin filtro**, y el
   criterio de 1 ms se revisa a la luz de W.
2. Si hay espurios, todos por debajo de G, y G < W con un margen claro: un **filtro** entre G y W. El
   criterio 1 y la matriz se revisan en la spec **antes** de implementar, sin debilitar pruebas.
3. Si los espurios y las órdenes se solapan: la detección de H2 no es fiable con esa señal. Esto
   refuerza la alternativa de **no construir H2**.

## Seguridad y puertas

- Ninguna salida GPIO. GPIO39 solo se lee, y GPIO36 y GPIO21 no se tocan.
- Flasheo solo por USB, con el 65XE apagado y el SIO desconectado. Nunca borrar NVS ni ejecutar
  `erase_flash`.
- La clave Wi-Fi nunca va al repositorio, la consola ni los registros.
- H4 y GPIO36 no se aprueban por esta medición, y no se amplía ninguna aceptación.
- #125, #126 y #127 siguen abiertas. La serie de 10 cargas de la #126 no forma parte de este plan.

**Autorizaciones necesarias, por separado y en orden:**
1. Aprobar este plan.
2. Implementar el paquete de medición B en una rama nueva, con pruebas de host y revisión.
3. Flashearlo.
4. Usar COM3.
5. Cada escenario físico.
6. Opcional: conectar un analizador (A).

## Checklist

- [ ] Revisión independiente de esta propuesta.
- [ ] Verificar contra la errata vigente de Espressif el comportamiento de GPIO39.
- [ ] Decisión del responsable: B, B con A, o no medir.

## Next

Revisión independiente en la issue #125. Hasta tener autorización expresa no se ejecuta nada.
