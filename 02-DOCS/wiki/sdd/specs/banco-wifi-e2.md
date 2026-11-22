---
type: spec
title: Spec — Banco de demanda Wi-Fi de la FujiNet (T1 de menu-sio-wifi-tnfs)
description: QUÉ y POR QUÉ del firmware de banco que mide la corriente de la FujiNet al usar la radio, antes de implementar E2.
tags: [sdd, spec, firmware, wifi, banco, consumo]
timestamp: 2026-11-22T00:00:00Z
topic: sdd
slug: banco-wifi-e2
branch: test/banco-wifi-e2
status: draft
---

# Spec — Banco de demanda Wi-Fi de la FujiNet (T1 de menu-sio-wifi-tnfs)

> Slug: `banco-wifi-e2` · Estado: **borrador** · Creada: 2026-11-22
> Rama: `test/banco-wifi-e2`, desde `0a7144f` (plan E2 aprobado).
> Base: spec `menu-sio-wifi-tnfs` (criterios C1 y C10) y su plan, tareas T1a y T1b. Esta spec solo cubre el banco;
> la spec E2 sigue vinculada a su propia rama.

## Problema y por qué

E2 exige que la FujiNet funcione alimentada solo por el pin 10 del SIO, con un presupuesto documentado de 50 mA. La
radio Wi-Fi del ESP32 puede pedir bastante más. Nadie ha medido cuánto pide esta placa. Sin esa cifra, implementar E2
puede acabar en un firmware que no arranca con el USB desconectado.

## ¿Qué pasa si no lo construimos?

T1a se mediría con el firmware `menu-sio` actual, que no usa la radio. Solo daría el reposo. La búsqueda, la conexión
y la transferencia quedarían sin medir, y la decisión de E2 se tomaría a ciegas.

## La alternativa más barata

Usar las cifras de la hoja de datos del ESP32. No basta: dan la corriente del chip, no la de la placa entera (microSD,
regulador, LED), y no separan las fases que pide C1.

## Objetivos

- Medir por separado la demanda de cada fase de la radio en esta placa.
- Dejar la NVS igual que antes del ensayo.
- No usar ningún pin del SIO, para que T1a no dependa del Atari.

## Usuarios y contexto

El responsable, en el banco, con la FujiNet alimentada desde una fuente externa (T1a) o desde el 65XE (T1b).

## Qué podrá hacer el usuario

- Grabar el paquete `bench-wifi` (nunca publicado) con su aprobación.
- Poner `retrobridge-wifi.txt` en la microSD (red y clave) y encender la placa.
- Leer en la consola USB o en `retrobridge-banco-wifi.txt` qué fase está activa y desde cuándo.

## Qué resultado obtiene

Un ciclo repetido de cuatro fases de 30 s: radio apagada, búsqueda de redes, conectado sin tráfico, y descarga TNFS
continua de la ATR de ensayo (`tnfs.calaluis.cl:16384`, `NumenPAL.atr`, 196 496 bytes) con su SHA-256 comprobado.
Cada cambio de fase queda anunciado con el tiempo desde el arranque.

## Qué debe respetar

- **NVS:** el firmware no la lee ni la escribe. Ni el Wi-Fi ni la calibración de la radio guardan datos en ella, y no
  hay volcado de núcleo a flash. El empaquetador rechaza el paquete si alguna de esas opciones está activa.
- **Clave:** nunca en el repositorio, la consola ni ningún registro. Solo se registra su longitud.
- **SIO:** ningún pin del SIO se configura.
- **Otros paquetes:** el código del banco y el arranque de la radio no entran en los paquetes normal ni `menu-sio`.
- **Procedencia:** sin código de FujiNet ni de terceros.
- **Aprobaciones:** cada flash y cada uso de COM3 necesitan la aprobación del responsable.

## Cómo lo demuestra

- **CI:** el paquete `bench-wifi` compila y pasa sus comprobaciones. Los paquetes normal y `menu-sio` no contienen la
  marca del banco ni el arranque de la radio (`esp_wifi_init`).
- **NVS (C10 de E2):** se compara la NVS antes de grabar el banco y después del ensayo. Solo se registra el hash o el
  resultado de la comparación.
- **Medida:** la tabla de abajo, en `02-DOCS/raw/<fecha>-consumo-e2-sin-atari.md`, con método y foto del montaje.

| Fila | Firmware | Qué se mide |
| --- | --- | --- |
| Reposo E1 | `menu-sio` actual | Placa encendida en el menú, sin radio |
| Fase 0 | `bench-wifi` | Radio apagada |
| Fase 1 | `bench-wifi` | Búsqueda de redes |
| Fase 2 | `bench-wifi` | Conectado sin tráfico |
| Fase 3 | `bench-wifi` | Transferencia TNFS |

El reposo E1 y la fase 0 se anotan en filas separadas: son firmwares distintos.

## Comportamiento

- Sin `retrobridge-wifi.txt` o con formato inválido: lo dice en la consola y en el registro, y solo repite la fase 0.
- Si la red o el servidor fallan: lo registra y pasa a la fase siguiente.

## Criterios de aceptación

1. Dado el paquete `bench-wifi` de la CI, cuando se revisa su informe, entonces todas sus comprobaciones pasan,
   incluida «Wi-Fi sin NVS ni volcado de núcleo a flash».
2. Dados los paquetes normal y `menu-sio` de la misma CI, cuando se buscan la marca del banco y el símbolo
   `esp_wifi_init` en sus binarios, entonces no aparecen.
3. Dada la placa con el banco, cuando termina el ensayo, entonces la NVS es igual a la de antes (hash o comparación).
4. **Criterio eléctrico (C1 de E2).**
   - **T1a:** dada la FujiNet alimentada desde una fuente externa a través de un medidor y **sin conexión eléctrica
     con el Atari**, cuando se mide la media de cada fila de la tabla, entonces cada fila queda en 50 mA o menos.
   - **T1b:** solo si T1a lo confirma. Dada la FujiNet alimentada por el pin 10 del SIO del 65XE con el USB
     desconectado y el medidor en serie, cuando se mide cada fila, entonces cada fila queda en 50 mA o menos.
   - **Comprobación funcional complementaria, que no sustituye a T1a ni a T1b:** en cada ensayo, al menos 3 ciclos
     sin `reset BROWNOUT` ni arranques nuevos a mitad del ciclo, y todas las descargas TNFS con el SHA-256
     esperado.
5. Dada cualquier salida del banco, cuando se busca la clave, entonces no aparece.

## Fuera de alcance

- Implementar E2 (`WifiStation`, fuentes, menú).
- Publicar el paquete `bench-wifi`.
- Medir picos sin osciloscopio.

## Puntos a clarificar

- **decisión diferida** — T1a y T1b esperan un medidor de corriente. *Base:* el responsable confirmó el 2026-11-22
  que no hay instrumentos. Mientras tanto solo se hace la comprobación funcional con alimentación USB
  (`02-DOCS/raw/2026-11-22-banco-wifi-e2-usb.md`).
- **suposición tomada** — 30 s por fase bastan para leer una media estable. *Base:* el multímetro promedia en pocos
  segundos. *Riesgo:* si no, se alarga la fase en el firmware.
- **suposición tomada** — el método de lectura de la NVS es el del plan E2. *Base:* C10 deja el método al plan.

## Clarificaciones

- 2026-11-22 (primera versión, **sustituida**) — Sin instrumentos, se propuso cumplir T1 con un ensayo con el SIO
  y el criterio «sin brownouts y descargas con SHA correcto».
- 2026-11-22 (vigente) — El responsable alinea el criterio 4 con C1 de E2. T1a mide la demanda con alimentación
  externa y el Atari desconectado. T1b, con el SIO del 65XE, espera a que T1a confirme 50 mA o menos. Los ciclos sin
  brownout y con SHA correcto quedan como comprobación funcional complementaria. La comprobación funcional se hizo
  con alimentación USB; el ensayo con el SIO no se ha hecho.
- **Antecedente comparativo, no evidencia de T1:** estimación hecha con el esquema público de la revisión
  **FN32ROV-1.5** (la placa ensayada es la **FN32ROV-1.3-Q24**; según el registro de cambios, la 1.5 no cambió la
  alimentación). A partir de valores típicos de hojas de datos: E1 sin radio, 55–145 mA; Wi-Fi recibiendo,
  125–195 mA; picos de transmisión, hasta ~330 mA. El vídeo del 2026-11-21 muestra E1 funcionando solo con el SIO
  del 65XE. Ninguna de las dos cosas sustituye a la medida de T1a.
