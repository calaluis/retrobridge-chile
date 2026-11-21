---
type: spec
title: Spec — Menú de RetroBridge por SIO físico desde la FujiNet FN32ROV-1.3-Q24
description: QUÉ y POR QUÉ de servir el menú de arranque como D1 por el bus SIO real, sin retrobridge_host ni Altirra.
tags: [sdd, spec, firmware, sio, menu]
timestamp: 2026-11-21T20:00:00Z
topic: sdd
slug: menu-sio-fisico
branch: feat/menu-sio-fisico
status: approved
---

# Spec — Menú de RetroBridge por SIO físico

> Slug: `menu-sio-fisico` · Estado: **aprobada** por el responsable el 2026-11-21 («Apruebo desarrollar en
> `feat/menu-sio-fisico` con dos entregas en la misma spec») · Creada: 2026-11-21
> Rama: `feat/menu-sio-fisico` desde `main` `6e2cdca` (PR #73 fusionado).
> Base: `menu-arranque`, `menu-navegacion`, `menu-tnfs`, `menu-xex` (el menú que ya funciona en Altirra) y
> ADR-011, ADR-012 y ADR-017.

## Problema y por qué

El menú de RetroBridge solo existe dentro de `retrobridge_host`. Para verlo hace falta un PC con el host y Altirra
por NetSIO. El objetivo del proyecto es una disquetera usable en el Atari real: al encender el 65XE con la FujiNet
conectada por SIO debe aparecer el menú, y desde él se elige y arranca una imagen, sin PC.

## ¿Qué pasa si no lo construimos?

RetroBridge sigue sin servir nada al Atari real. La FujiNet solo puede cargar programas con el firmware FujiNet del
responsable, y todo el trabajo del menú (catálogo, TNFS, escritura, formateo) solo se usa en un emulador.

## La alternativa más barata

Seguir con `retrobridge_host` + Altirra, o usar el firmware FujiNet del responsable en el 65XE. La primera no llega
al hardware real; la segunda abandona RetroBridge en el Atari. Ninguna cumple el objetivo del producto.

## Entregas

| Entrega | Contenido | Demo |
|---|---|---|
| **E1** | Menú al encender el 65XE, navegación con teclado, fuente local (microSD) y arranque con A de una ATR de la microSD | El menú y una ATR arrancando en el 65XE |
| **E2** | El mismo flujo con fuentes TNFS por Wi-Fi | Una ATR de un servidor TNFS arrancando en el 65XE |

E2 empieza cuando E1 está demostrada. Sus puntos propios (Wi-Fi, fuentes) se aclaran al empezarla.

## Objetivos

1. Al encender el 65XE con la FujiNet ya alimentada por USB, el Atari arranca el menú de RetroBridge desde D1.
2. El menú responde al teclado como en Altirra.
3. Una ATR elegida para D1 arranca al pulsar A (E1: microSD; E2: TNFS).
4. El mismo código del menú sirve a `retrobridge_host` y al firmware: no se duplica.

## Usuarios y contexto

El responsable, con su 65XE y su FujiNet FN32ROV-1.3-Q24, sin PC al lado. La FujiNet se alimenta por USB y arranca
antes que el Atari. La microSD es la misma de las pruebas de casetera.

## Qué podrá hacer el usuario

- **Encender el 65XE** con la FujiNet ya alimentada: el Atari carga el menú desde D1.
- **Navegar** con el teclado las pantallas que ya existen.
- **Elegir una ATR** de la microSD para D1 y **arrancarla con A**.
- **Volver al menú** después de arrancar una ATR: pulsar el **botón A de la FujiNet** (o dar la orden `menu` por la
  consola USB) y apagar y encender el 65XE.
- **Condición previa para la demo física:** el paquete de prueba aparte, la aceptación escrita del responsable y el
  adaptador sin el pin 8 (ver «Evidencia para la demo física»).

## Comportamiento de D1

- **Al arrancar la FujiNet:** D1 tiene el disco del menú. Cuando el 65XE se enciende, el sistema operativo lee D1 y
  arranca el menú. Si el 65XE se apaga y enciende sin elegir nada, vuelve a aparecer el menú.
- **Al pulsar A en el menú:** D1 pasa a tener la ATR elegida y el Atari arranca desde ella (como hoy en Altirra).
- **Después:** D1 se comporta como una disquetera con el disco puesto. Apagar y encender el 65XE arranca la misma
  ATR otra vez.
- **Volver al menú:** el botón A de la FujiNet, o la orden `menu` por la consola, devuelve el menú a D1 sin reiniciar
  la FujiNet; el siguiente encendido del 65XE muestra el menú. Reiniciar la FujiNet también lo devuelve.
- La consola indica qué tiene D1 en cada momento.

## Qué resultado obtiene

- **Referencia aprobada:** las pantallas del menú tal como se ven hoy en Altirra con `retrobridge_host` (mismo
  programa 6502, mismos textos y teclas). El 65XE debe mostrar las mismas pantallas para la misma secuencia de teclas.
- La consola USB registra cada orden SIO servida (dispositivo, orden, resultado), la latencia entre el flanco de
  COMMAND y el ACK, el contenido de D1 y la tensión del +5 V del SIO (pin 10) leída por el divisor de GPIO35.

## Qué debe respetar

- **H4 y M3a siguen sin aprobar.** Ningún PR los marca como aprobados. El firmware normal (`cassette-lectura` y los
  demás paquetes) no transmite por SIO.
- **Paquete de prueba aparte**, como `bench-c8`: una variante propia del empaquetador, rechazada por las demás
  variantes. Solo transmite por DATA IN en ese paquete, y la demo física exige la aceptación escrita del responsable.
- **DATA IN siempre liberada** en reposo, tras cualquier error y al terminar cada respuesta (criterio C8 de H4-C).
- **GPIO21 y UART2 compartidos** con la casetera: en este paquete la casetera no maneja DATA IN ni lee MOTOR.
- **Sin salidas nuevas:** el botón A (GPIO0) y GPIO35 solo se leen. Las únicas salidas del paquete son DATA IN y las
  que ya usa la microSD.
- **ADR-011:** sin reservas de memoria dinámica en la ruta SIO.
- **ADR-012:** sin código de FujiNet ni de terceros; el fork del responsable solo como evidencia.
- **ADR-017:** protocolo del menú (dispositivo `$5A`, órdenes `K` y `T`) sin cambios; `retrobridge_host` y sus
  pruebas siguen pasando igual.

## Evidencia para la demo física

| Tema | Lo que hay | Lo que falta antes de conectar | Cómo se obtiene sin instrumentos |
|---|---|---|---|
| **Adaptador sin pin 8** (MOTOR) | Mitigación C especificada (`mitigacion-c-motor-pin8.md`). El menú de disco no usa MOTOR | Que el adaptador no conecte el pin 8. §5.2 pide multímetro | Construirlo **sin el contacto 8** (retirado físicamente), con foto y rótulo. Queda como riesgo aceptado por escrito, no como medición |
| **DATA IN** (pin 3) | C8 superada en el ESP32 (`37ced64`): GPIO21 pasa por UART2 y RMT y queda liberada. GPIO21 llega a un 74LS07 de colector abierto [E] | Nivel real de la línea en el 65XE (la 1.3 no tiene pull-up propio) | Funcional: si el menú carga, DATA IN funciona. El firmware registra cada respuesta y cada liberación |
| **COMMAND y DATA OUT** (pines 7 y 5) | Llegan al ESP32 a través de 74LS07 [E]: el ESP32 no recibe 5 V directos | Reposo de DATA OUT (sin pull-up en el esquema) | El firmware registra el nivel en reposo de GPIO39 y GPIO33 al arrancar |
| **Alimentación por SIO** (pin 10) | USB y SIO entran por diodos Schottky [E]: no hay retroalimentación entre ellos. GPIO35 lee el +5 V del SIO por un divisor | Cuánta corriente toma la FujiNet del pin 10 (máximo 50 mA según Atari) | La tensión, sí: el firmware la mide por GPIO35. La corriente, no (M6 sigue pendiente): queda como riesgo aceptado por escrito |

## Cómo lo demuestra

1. **Pruebas de host:** el menú, ya como componente común, pasa las pruebas actuales; el transporte SIO físico se
   prueba contra un UART y un COMMAND simulados, incluido que DATA IN queda liberada tras cada respuesta y error; el
   comportamiento de D1 (menú, A, botón, orden `menu`) se prueba en host.
2. **Regresión en Altirra:** `retrobridge_host` con el componente común muestra las mismas pantallas que hoy.
3. **CI:** el paquete de prueba se compila, se empaqueta y se verifica; las demás variantes lo rechazan.
4. **Banco por USB, sin el Atari:** el firmware arranca, monta la microSD, lista la fuente local, muestra el
   contenido de D1, el estado del botón A y la tensión de GPIO35; la consola lo registra.
5. **65XE real** (con la aceptación escrita y el adaptador): captura de la consola y foto del menú en pantalla,
   navegación y arranque de una ATR con A. Registro en `docs/mediciones/menu-sio-fisico-AAAA-MM-DD.md`.

## Criterios de aceptación

**E1**
1. **Dado** el paquete de prueba, el adaptador sin pin 8 y la aceptación escrita, **cuando** se enciende el 65XE con
   la FujiNet ya alimentada por USB, **entonces** aparece la pantalla principal del menú.
2. **Dado** el menú en pantalla, **cuando** se pulsan las teclas de navegación, **entonces** el 65XE muestra las
   mismas pantallas que Altirra para la misma secuencia.
3. **Dada** una ATR de la microSD elegida para D1, **cuando** se pulsa A, **entonces** el 65XE arranca esa imagen.
4. **Dado** que D1 tiene una ATR, **cuando** se pulsa el botón A de la FujiNet o se da la orden `menu` y se apaga y
   enciende el 65XE, **entonces** aparece el menú.
5. **Dado** cualquier paquete distinto del de prueba, **cuando** se empaqueta o verifica, **entonces** no contiene
   la transmisión por SIO físico.
6. **Dada** cualquier respuesta o error del transporte, **entonces** DATA IN queda liberada (prueba de host).
7. **Dado** el host con el componente común, **entonces** sus pruebas actuales y la regresión en Altirra pasan.

**E2**
8. **Dado** un servidor TNFS configurado y alcanzable, **cuando** se abre su fuente en el 65XE, **entonces** se
   listan sus carpetas y una ATR elegida arranca con A.

## Fuera de alcance

- Aprobar H4, H4-T o M3a, o declarar C5 cerrado.
- La casetera (DATA IN y MOTOR) en este paquete.
- Detección automática del reinicio en frío del Atari.
- D2–D8, escritura de sectores, formateo y edición de servidores desde el 65XE, salvo que vengan sin esfuerzo con
  el componente común (no se prueban en el 65XE en este hito).
- Alta velocidad SIO (solo 19200 baudios).
- Configurar el Wi-Fi desde el menú del Atari.

## Puntos a clarificar

- **decisión diferida** — (E2) cómo se configura el Wi-Fi (red y clave) en la FujiNet; la clave no debe aparecer en
  el registro ni en el repositorio.
- **decisión diferida** — (E2) dónde se guardan las fuentes TNFS en la FujiNet (hoy `--sources` en el PC).
- **suposición tomada** — 19200 baudios y los tiempos de `SioSession` bastan para el 65XE. *Base:* son los del
  sistema operativo y ya funcionan por NetSIO. *Riesgo:* si la latencia física no entra en la ventana del sistema
  operativo, el registro de latencia de la consola lo mostrará (no hace falta otra spec).
- **suposición tomada** — el menú de 17 sectores arranca igual en el 65XE que en Altirra. *Base:* usa SIOV
  estándar. *Riesgo:* diferencias del hardware real; se ve en la primera prueba.
- **suposición tomada** — el botón A (GPIO0) se puede leer como entrada después del arranque. *Base:* esquema 1.3
  (`fujinet-atari-v1x.md` §4) y el uso de FujiNet. *Riesgo:* GPIO0 es pin de arranque; pulsarlo durante un reinicio
  entra en modo de descarga. El plan confirma su pull-up.
- **decisión diferida** — detección automática del reinicio en frío; alta velocidad SIO.
- **área no formulable** — cómo convive este paquete con la casetera cuando H4-C se apruebe.

## Decisiones tomadas por el responsable (2026-11-21)

- Dos entregas en la misma spec: E1 (microSD) y después E2 (TNFS).
- Alcance: ver y navegar el menú, y arrancar con A la imagen elegida.
- Vuelta al menú: botón de la FujiNet y orden de consola (detección automática diferida).
- Transmisión física: paquete de prueba aparte con aceptación escrita y adaptador sin pin 8; H4 sigue sin aprobar.

## Objeción registrada

La aceptación depende de la conexión física, que hoy no tiene mediciones (H4-T). El código se verifica antes en
host y Altirra; el bus real se prueba al final y puede obligar a retocar el transporte. El responsable decidió
avanzar con el código y las pruebas de host mientras tanto.

## Riesgos

- Sin mediciones, la primera conexión expone el bus del 65XE y la FujiNet a niveles no medidos. El adaptador sin
  pin 8 elimina MOTOR; COMMAND y DATA OUT llegan por 74LS07.
- La FujiNet puede tomar del pin 10 más de 50 mA si la tensión del Atari supera a la del USB (diodos Schottky). La
  tensión se registra; la corriente no.
- El alcance (E1 + E2) es mayor que ver el menú: el plan lo divide en etapas verificables.

## Revisiones

- 2026-11-21: borrador → aprobada con cambios del responsable: dos entregas (E1 microSD, E2 TNFS), comportamiento de
  D1 al encender y vuelta al menú definidos, y tabla de evidencia para la demo física (adaptador sin pin 8, DATA IN,
  alimentación por SIO). Wi-Fi y fuentes TNFS pasan a decisión diferida de E2.
