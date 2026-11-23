---
type: spec
title: Spec — Unidades TNFS que sobreviven al apagado del 65XE
description: QUÉ y POR QUÉ de recordar entre encendidos las imágenes montadas desde un servidor TNFS en D1..D8.
tags: [sdd, spec, firmware, sio, menu, tnfs]
timestamp: 2026-11-23T12:00:00Z
topic: sdd
slug: menu-tnfs-persistente
branch: spec/menu-tnfs-persistente
status: approved
---

# Spec — Unidades TNFS que sobreviven al apagado del 65XE

> Slug: `menu-tnfs-persistente` · Estado: **aprobada** por el responsable el 2026-11-23 («Apruebo, commit y push») · Creada: 2026-11-23
> Rama: `spec/menu-tnfs-persistente`, desde `main` (`0a13bfc`).
> Base: `menu-unidades-persistentes`, `menu-sio-wifi-tnfs`, `menu-tnfs`. Depende del PR #85 (`menu-cinta`), que
> cambia el mismo formato de la memoria de unidades.

## Problema y por qué

El menú recuerda entre encendidos lo montado en D1..D8 desde la microSD (`menu-unidades-persistentes`). Lo montado
desde un servidor TNFS (fuentes 2..8) se olvida: se recuerda vacío, por decisión diferida en
`menu-unidades-persistentes` y en `menu-sio-wifi-tnfs`. El responsable lo observó en el 65XE el 2026-11-23: las
unidades montadas desde la fuente 2 no vuelven tras apagar y encender; las de la microSD, sí.

El objetivo del producto es que la FujiNet funcione al 100 % con Wi-Fi/TNFS. Una unidad que se pierde en cada
encendido según de dónde vino rompe esa promesa: el usuario no debería tener que recordar qué fuente usó.

## ¿Qué pasa si no lo construimos?

Cada encendido obliga a volver a montar cada imagen de red: abrir la fuente, entrar en las carpetas, elegir la
imagen y la unidad. Son más teclas que con la microSD, porque hay carpetas y espera de red. El coste se repite en
cada sesión y crece con el número de unidades de red. Además, las unidades se comportan distinto según su origen, y
eso confunde.

## La alternativa más barata

Volver a montar a mano desde la fuente 2. Funciona y ya existe. No basta porque deja dos comportamientos distintos
para la misma acción (montar) y porque, con varias unidades de red o carpetas profundas, el coste por encendido deja
de ser pequeño.

## Objetivos

- Lo montado desde un servidor TNFS en D1..D8 sigue asignado tras apagar y encender el 65XE, como lo de la microSD.
- El menú aparece en el mismo plazo que hoy; la red no retrasa D1.
- Si la red o el servidor fallan al encender, el menú lo dice por unidad y sigue operativo.

## Usuarios y contexto

El dueño del 65XE con la FujiNet en su banco, alimentada solo por el SIO o por USB, con un servidor TNFS en
`retrobridge-fuentes.txt` (hoy `tnfs.calaluis.cl`) y el Wi-Fi configurado en la microSD.

## Qué podrá hacer el usuario

- Montar en D1..D8 una ATR de una fuente TNFS, y en D1 un `.XEX` de una fuente TNFS, como hoy.
- Apagar el 65XE, encenderlo después y ver esas unidades asignadas otra vez, sin volver a elegirlas.
- Arrancar D1 con `A` cuando su imagen de red ya está cargada.
- Cambiar o desmontar cualquier unidad de red; el cambio también se recuerda.
- Si el servidor no respondió al encender, montar a mano otra imagen en esa unidad, o esperar al encendido siguiente,
  que vuelve a intentarlo.

## Qué resultado obtiene

- Al encender, D1 sirve el menú en el plazo de hoy. Las unidades de la microSD se restauran como hoy.
- Cada unidad de red recordada muestra que se está cargando (por ejemplo `D2: CARGANDO <nombre>`) hasta que su imagen
  llega; entonces muestra su nombre, como cualquier unidad montada. El texto exacto lo fija el plan, dentro de 40
  columnas.
- Si tras unos 30 s el Wi-Fi o el servidor no responden, esa unidad queda vacía en esta sesión y el menú dice por qué
  (por ejemplo `D2: SIN RED`). Su nombre se sigue recordando: el próximo encendido lo intenta otra vez.
- Si el servidor responde pero la imagen ya no está o no es válida, la unidad queda vacía y el menú lo dice, como
  con la microSD (`FALTA`, `INVALIDA`), y deja de recordarse.
- Mientras D1 recordada sea de red y no haya llegado, `A` no arranca una imagen a medio cargar.
- **Referencia aprobada:** el comportamiento ya aprobado en el 65XE para la microSD en `menu-unidades-persistentes`
  (restaurar, mostrar, `A`) y los avisos de red de `menu-sio-wifi-tnfs` (`SIN WI-FI: …`).

## Qué debe respetar

- `menu-unidades-persistentes`: D1 sirve el menú enseguida (< 1 s desde EN); la microSD se restaura igual que hoy;
  `GUARDANDO`/`GUARDADO`/`NO SE GUARDO`; un corte pierde como mucho el último cambio sin confirmar; nunca se
  borra ni se reescribe en bloque la NVS.
- `menu-sio-wifi-tnfs`: el Wi-Fi conecta en segundo plano; la clave nunca aparece en pantalla, consola ni registros;
  sin escrituras en la NVS.
- `menu-cinta` (PR #85): la memoria de unidades ya guarda `C:` (formato v2). Esta spec la extiende; no la reemplaza.
  Una memoria v1 o v2 se sigue leyendo.
- Una imagen de red vive en la PSRAM: se descarga de nuevo en cada encendido. Lo que el Atari escribió en esa copia
  en la sesión anterior no vuelve (ya es así hoy).
- Límites de memoria de `MemoryImageStore` (por imagen y total): una restauración que no cabe se trata como un fallo
  de esa unidad, con su motivo; nunca deja sin memoria al menú ni a la casetera.
- Sin código de FujiNet ni de terceros. Main cerrada: rama y PR propios.
- El banco físico necesita aprobación por separado para flashear y para abrir COM3.

## Cómo lo demuestra

- Tests de host: la memoria de unidades guarda y lee una unidad de red (servidor + ruta) junto a las de la microSD;
  una memoria v1/v2 se sigue leyendo; la restauración de red monta al llegar la imagen, avisa con el motivo si falla
  o si se agota el plazo, y no bloquea la pantalla ni `A` de las unidades de la microSD.
- Banco en el 65XE con USB (consola) y otro solo con SIO: montar una ATR de la fuente 2 en D2 y un `.XEX` de la fuente
  2 en D1, apagar, encender. Foto de la pantalla con `CARGANDO` y luego con ambas asignadas; `A` arranca el `.XEX`.
- Banco con el servidor apagado o inalcanzable: foto con el aviso de la unidad tras el plazo; el encendido siguiente,
  con el servidor de vuelta, la restaura.
- Registro de arranques: `listo_ms` igual que hoy (+50 ms como máximo).

## Comportamiento

- Montar o desmontar una unidad de red se guarda como cualquier otro cambio, con los mismos mensajes.
- Al encender: D1 sirve el menú; se restauran las unidades de la microSD; las de red quedan en `CARGANDO`.
- En segundo plano, cuando hay Wi-Fi, cada unidad de red pide su imagen al servidor; al llegar, se monta.
- Plazo agotado sin red o sin servidor: la unidad queda vacía en esta sesión, con el motivo; su nombre sigue
  recordado.
- Elegir otra imagen o desmontar una unidad que está en `CARGANDO` cancela su restauración y se guarda el cambio.
- `A` con D1 de red todavía en `CARGANDO` no arranca: dice que espere (texto en el plan).

## Criterios de aceptación

- **AC1** — Dada D2 montada desde la fuente 2, cuando se apaga y enciende el 65XE con el servidor disponible,
  entonces el menú muestra D2 en carga y, tras la descarga, D2 con su nombre; el Atari lee la imagen en D2.
- **AC2** — Dado un `.XEX` de la fuente 2 en D1, cuando se enciende y termina la carga, entonces `A` arranca ese
  programa hasta verlo correr, sin volver a elegirlo.
- **AC3** — Dadas D1 de la microSD y D2 de la fuente 2, cuando se enciende, entonces D1 queda lista en el plazo de
  `menu-unidades-persistentes` y `A` funciona con D1 aunque D2 siga cargando.
- **AC4** — Dada D2 de red recordada y el servidor inalcanzable, cuando se enciende, entonces tras el plazo el menú
  dice que D2 no se pudo cargar y por qué; el menú sigue operativo; en el encendido siguiente, con el servidor
  disponible, D2 vuelve.
- **AC5** — Dada D2 de red recordada cuya imagen ya no existe en el servidor, cuando se enciende, entonces D2 queda
  vacía con el motivo, como con la microSD, y el encendido siguiente ya no la intenta.
- **AC9** — Dada D2 recordada desde el servidor de la fuente 2, cuando se edita con `E` la fuente 2 para que apunte a
  otro servidor, entonces el encendido siguiente pide D2 al servidor original, no al nuevo.
- **AC6** — Dada D2 en `CARGANDO`, cuando el usuario monta otra imagen en D2 o la desmonta, entonces la restauración
  se cancela y se recuerda el cambio del usuario.
- **AC7** — Dada una memoria de unidades v2 (de `menu-cinta`), cuando arranca el firmware nuevo, entonces se leen
  D1..D8 y `C:` sin pérdida.
- **AC8** — Con unidades de red recordadas, cuando se enciende solo por SIO, entonces `listo_ms` no empeora más de
  50 ms respecto de hoy.

## Fuera de alcance

- Cintas `.cas` desde TNFS (decisión diferida en `menu-cinta`).
- Guardar en el servidor lo que el Atari escribe en una imagen de red.
- Recordar la carpeta por la que se navegó en una fuente.
- Detectar que la imagen cambió en el servidor entre encendidos: se carga la que haya.
- Un caché local de imágenes de red en la microSD.
- Cambios en el arranque con `A` y teclas de consola (eso es T9 de `menu-cinta`).

## Puntos a clarificar

- **suposición tomada** — alcance: cualquier imagen TNFS en D1..D8, ATR y `.XEX` en D1. *Base:* el responsable aceptó
  la recomendación el 2026-11-23 («Sí a todo»).
- **suposición tomada** — la restauración de red va en segundo plano; el menú responde enseguida y muestra la carga
  por unidad. *Base:* el responsable aceptó la recomendación el 2026-11-23. *Riesgo:* el Atari puede leer D2 antes de
  que llegue; hasta entonces responde como unidad sin disco.
- **suposición tomada** — plazo de unos 30 s; al agotarse, la unidad queda vacía en la sesión y su nombre se sigue
  recordando hasta que el usuario la cambie. *Base:* el responsable aceptó la recomendación el 2026-11-23. El valor
  exacto lo fija el plan.
- **suposición tomada** — la spec se escribe en la rama `spec/menu-tnfs-persistente` desde `main`; la implementación
  espera a que el PR #85 se fusione, porque ambas cambian el formato de la memoria de unidades. *Base:* el responsable
  aceptó la recomendación el 2026-11-23.
- **suposición tomada** — una imagen que el servidor dice que ya no existe, o que no es válida, se deja de recordar,
  igual que una imagen de la microSD que falta. *Base:* el responsable aceptó la recomendación el 2026-11-23; un
  servidor que contesta «no existe» no es un fallo pasajero. Un fallo de red o un plazo agotado sí se sigue
  recordando.
- **suposición tomada** — lo recordado identifica el servidor por su dirección (`host:puerto`) y la ruta de la imagen,
  no por el número de fuente. *Base:* el responsable aceptó la recomendación el 2026-11-23; editar o reordenar las
  fuentes con `E` no debe montar una imagen de otro servidor. Una dirección que ya no está en las fuentes se intenta
  igual.
- **área no formulable** — cuántas unidades de red pueden restaurarse a la vez sin agotar la PSRAM junto a la
  casetera (banco de ~1 MB). Se precisa en el plan con los límites de `MemoryImageStore` y las medidas del banco T8
  de `menu-cinta` (PSRAM libre tras la casetera: 1 910 980 B).

## Objeción registrada

Planteada al responsable el 2026-11-23: la alternativa barata ya existe (volver a montar desde la fuente 2); el
arranque pasa a depender de la red (Wi-Fi ~2 s, servidor que puede no responder); y el formato de la memoria de
unidades cambia otra vez justo tras `menu-cinta`, con el PR #85 aún abierto. El responsable decidió seguir («Sí a
todo, sigue»): la meta es que la FujiNet funcione al 100 % con Wi-Fi/TNFS.

## Clarificaciones

- 2026-11-23 — ¿Alcance? → D1..D8 de red, ATR y `.XEX` en D1; cintas TNFS fuera → aceptado por el responsable.
- 2026-11-23 — ¿Restaurar en segundo plano o esperar a la red? → segundo plano, `CARGANDO` por unidad → D1 no se
  retrasa.
- 2026-11-23 — ¿Servidor sin respuesta? → ~30 s, unidad vacía con motivo, nombre recordado y reintento al encender
  otra vez → un fallo de red es pasajero.
- 2026-11-23 — ¿Dónde se escribe? → rama `spec/menu-tnfs-persistente` desde `main`; implementación tras el PR #85.
- 2026-11-23 — ¿Imagen que ya no existe en el servidor? → se olvida, como en la microSD → «no existe» no es pasajero.
- 2026-11-23 — ¿Servidor por número de fuente o por dirección? → por `host:puerto` y ruta → editar fuentes con `E`
  no cambia de servidor una unidad recordada.
