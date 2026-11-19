---
type: spec
title: Spec — Menú de arranque para elegir y montar ATR desde retrobridge_host
description: QUÉ y POR QUÉ del menú que el Atari carga al encender para elegir la imagen D1 (Issue #38).
tags: [sdd, spec, host, netsio, menu]
timestamp: 2026-11-17T22:00:00Z
topic: sdd
slug: menu-arranque
branch: feat/38-menu-arranque
status: approved
---

# Spec — Menú de arranque (Issue #38)

> Slug: `menu-arranque` · Estado: aprobada por el responsable el 2026-11-17 · Creada: 2026-11-17
> Origen: Issue #38, continuación de #32 (E1c fusionada en #37, merge 7bf03bd).

## Problema y por qué

Hoy `retrobridge_host` sirve una sola ATR, fijada al lanzarlo. Para cambiar de disco hay que detener el host y
relanzarlo con otra imagen. Una disquetera usable debe dejar que el Atari elija qué arrancar desde el propio
Atari, sin tocar el PC.

## ¿Qué pasa si no lo construimos?

RetroBridge sigue sirviendo una imagen fija. Cada cambio de disco obliga a volver al PC y relanzar el host. Es
el principal obstáculo de uso de la modalidad emulada, aunque no bloquea la modalidad física.

## La alternativa más barata

Montar discos desde Altirra. Queda descartada por la issue: el objetivo es que RetroBridge sea la unidad.

## Objetivos

- Elegir la imagen D1 desde el Atari, con el teclado, sin tocar el PC.
- Cambiar de imagen las veces que haga falta con el host lanzado una sola vez.
- Que una imagen inválida no deje al usuario sin salida: error visible y nueva elección.

## Usuarios y contexto

- El responsable del proyecto, en el laboratorio emulado: Altirra 4.31, `netsio.atdevice`, hub NetSIO v0.16 y
  `retrobridge_host --peer netsiohub` en el mismo PC.
- Más adelante, quien use RetroBridge como disquetera: el mismo flujo debe poder llevarse al firmware, pero eso
  queda fuera de esta spec.

## Qué podrá hacer el usuario

1. Lanzar `retrobridge_host` una sola vez con una carpeta de imágenes (`--images <carpeta>`).
2. Encender el Atari (o reiniciarlo en frío con Shift+F5) y ver el menú de RetroBridge con los `.atr` de la
   carpeta.
3. Elegir una imagen pulsando su letra. RetroBridge la monta como D1 y el Atari arranca desde ella.
4. Volver al menú con Shift+F5 y elegir otra imagen.
5. Si la imagen elegida no es una ATR válida, ver un mensaje de error en el menú y elegir otra.

## Qué resultado obtiene

Pantalla de referencia del menú (40 columnas; la aprueba el responsable junto con esta spec):

```text
 RETROBRIDGE D1 - ELIGE UNA IMAGEN

 A  DEMO-UNO.ATR
 B  DEMO-DOS.ATR
 C  ROTA.ATR

 PULSA LA LETRA DE LA IMAGEN
 ERROR: ATR INVALIDA               <- solo tras elegir una imagen inválida
```

Recorrido esperado en Altirra, con el host lanzado una sola vez:
encender → menú → A → arranca la imagen A → Shift+F5 → menú → B → arranca la imagen B → Shift+F5 → menú →
C (inválida) → error en el menú → A → arranca la imagen A.

## Qué debe respetar

- ADR-012: código propio. No se copia, traduce ni adapta código de FujiNet ni de emuladores. El programa 6502
  del menú se escribe para RetroBridge.
- ADR-016 y las decisiones D1 a D6 de #32: el transporte NetSIO, el perfil `--peer` explícito y la carpeta
  `host/` no cambian de contrato.
- ADR-002 (capas) y ADR-011 (memoria estática en componentes): `check_layering` en verde; ninguna reserva en el
  camino SIO de los componentes.
- La unidad sigue siendo de solo lectura. Las imágenes de la carpeta nunca se escriben.
- La protección de #36: el log nunca se escribe dentro de una imagen servida.
- El juego de imágenes de la Meta A (H3) y `test_meta_a` no cambian (lección de #37).
- Sin hardware, flasheo ni GPIO. H4, H4-L y H4-T siguen cerradas; sioGateApproved = false.

## Cómo lo demuestra

- Pruebas de host en CI para la lógica nueva: catálogo de la carpeta (orden, límite, nombres), validación al
  elegir, cambio de D1 entre transacciones, vuelta al menú con FF, y los bytes exactos de los comandos del
  menú.
- Prueba del programa 6502 del menú con el intérprete de `tools/tests` (lista, selección, error), si su juego
  de instrucciones lo permite; si no, se dice.
- Demostración en Altirra del recorrido completo de «Qué resultado obtiene», con una captura por pantalla, el
  log JSONL del host y el log del hub, y una tabla que relacione cada paso con su evidencia. Se adjunta al PR.

## Comportamiento

- Al iniciar con `--images`, el host lee la carpeta una vez y forma el catálogo: solo archivos `.atr`
  (sin distinguir mayúsculas), en orden alfabético, como máximo 20.
- Tras cada reinicio en frío (FF por NetSIO), D1 vuelve a ser la imagen del menú, generada por RetroBridge.
- El menú pide el catálogo al host, lo muestra y espera una letra. Una tecla fuera de la lista no hace nada.
- Al elegir, el host abre y valida la imagen. Si es válida, la monta como D1 y responde bien; el menú reinicia el
  Atari por software y el SO arranca desde la imagen elegida.
- Si no es válida, el host no cambia D1 y responde con un motivo corto; el menú lo muestra y vuelve a esperar una
  letra.
- Un reinicio en caliente (F5) no cambia D1.
- Una carpeta vacía o ilegible: el host termina con un error claro antes de servir nada.

## Criterios de aceptación

1. `retrobridge_host --images <carpeta>` arranca y, tras un FF, sirve el menú como D1.
2. El menú lista los `.atr` de la carpeta, en orden alfabético, con una letra cada uno.
3. Al pulsar una letra válida, D1 pasa a ser esa imagen y el Atari arranca desde ella (su pantalla propia).
4. Shift+F5 devuelve el menú; una segunda imagen elegida arranca la segunda.
5. Elegir una imagen inválida muestra el error en el menú, no monta nada y permite elegir otra.
6. Nada de lo anterior relanza el host ni monta discos desde Altirra.
7. `--image <archivo>` (modo de E1b/E1c) sigue funcionando igual.
8. ctest, pruebas de `tools/`, `check_layering` y `check_notices` en verde.

## Fuera de alcance

- Paginación: se listan como máximo las primeras 20 imágenes.
- Subcarpetas, nombres largos con caracteres no ASCII (se muestran con `?`), filtros u ordenaciones.
- Escritura en imágenes, D2 en adelante, densidad doble en el menú.
- Arrancar ATR reales de terceros: la demo usa imágenes generadas por RetroBridge.
- Llevar el menú al firmware ESP32 o a la modalidad física.
- Cambiar de imagen sin reiniciar el Atari.

## Puntos a clarificar

- Ninguno bloqueante. Las cuatro decisiones de abajo cierran las preguntas abiertas de la issue.
- Se resuelve en el plan, no aquí: el formato de los comandos entre el menú y el host, y si va por un ID de
  dispositivo propio o por D1.

## Decisiones tomadas por el responsable (2026-11-17)

- Volver al menú: un reinicio en frío del emulador (Shift+F5, FF por NetSIO) devuelve siempre el menú como D1.
  El menú arranca la imagen elegida con un reinicio por software, que no envía FF. F5 (en caliente) conserva la
  imagen.
- Imagen inválida: se valida al elegirla, con el mismo código de dominio (`DiskImage::open`). Si falla, el menú
  muestra un motivo corto y vuelve a la lista; nada se monta.
- Modo: autopiloto hasta el PR. Commit, push y PR esperan un sí explícito.
- Rama: `feat/38-menu-arranque`.

## Supuestos (review-after)

- Hasta 20 imágenes, letras A a T, nombres recortados a 30 caracteres.
- Las imágenes de la demo son generadas por `make_atr.py` (dos arrancables con mensajes distintos y una
  inválida).
- La constitución sigue en borrador; se aplica como ratificación provisional.

## Riesgos

- Sin verificar: que un reinicio por software del Atari (salto al arranque en frío del SO) no envíe FF por
  NetSIO. Si lo envía, la vuelta al menú necesita otro disparador. Es la primera tarea del plan.
- El protocolo entre el menú y el host es propio y no está en FEP-003; se documenta en un ADR.
