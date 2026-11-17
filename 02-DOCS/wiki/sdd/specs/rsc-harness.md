---
type: spec
title: Spec — Arnés rsc para Claude Code y Codex
description: QUÉ y POR QUÉ del arnés de asistentes rsc 4.1.7 en RetroBridge, con controles humanos preservados.
tags: [sdd, spec, harness]
timestamp: 2026-11-17T17:15:00Z
topic: sdd
slug: rsc-harness
branch: chore/rsc-harness
status: clarified
---

# Spec — Arnés rsc para Claude Code y Codex

> Slug: `rsc-harness` · Estado: aprobada por el responsable el 2026-11-17 · Creada: 2026-11-17
> Decisiones del arnés: [decisions.md](../../harness/decisions.md) (H1–H6). Esta spec no las repite.

## Problema y por qué

Claude Code y, más adelante, Codex trabajan en RetroBridge en sesiones largas. Sin un arnés común, cada sesión
reconstruye a mano el método de trabajo: carriles, ramas y verificación. Los controles del proyecto (aprobación
previa de commit, push y PR, y procedencia del aporte de la IA) dependen solo de la memoria de la sesión. El
responsable pidió configurar rsc y contrastarlo con HUMENGIA.

## ¿Qué pasa si no lo construimos?

No se pierde nada del producto: el firmware, el host y la CI no dependen del arnés. El coste es de proceso. Cada
sesión repite instrucciones y Codex no comparte el método de Claude Code. Los controles humanos siguen siendo
convención, no hooks. Es un coste moderado, no crítico.

## La alternativa más barata

Escribir las reglas del proyecto a mano en `CLAUDE.md` y `AGENTS.md`, sin hooks ni Node. Resuelve la parte de
instrucciones, pero no da guards de rama, ni carriles, ni la verificación que rsc aporta. Es insuficiente para el
objetivo pedido, que fue configurar rsc, no reemplazarlo.

## Objetivos

- Los dos asistentes del proyecto (Claude Code y Codex) operan con el mismo arnés versionado y fijado a rsc 4.1.7.
- Ningún automatismo del arnés hace commit, push, PR, actualización ni borrado sin aprobación humana.
- El control de licencias de terceros sigue cubriendo todo lo que el repositorio distribuye.

## Fuera de alcance

- Cambios en el firmware, `rb_netsio`, el host ni la CI de RetroBridge.
- Completar `constitution.md` (queda como borrador de rsc).
- Instalar el hook `post-merge` o actualizar Git.
- Conectar MCP externos (context7 excluido por el plan).
- Activar el arnés en otros clones: cada persona ejecuta `sync` por su cuenta.

## Usuarios y contexto

El responsable humano del proyecto, que aprueba cada commit, push y PR, y los asistentes Claude Code y Codex,
que trabajan en ramas con PR. `main` está cerrado para el agente.

## Qué podrá hacer el usuario

En un clon con el arnés construido (instalación o `npx @ericrisco/rsc@4.1.7 sync`), el responsable puede pedir
trabajo a Claude Code o a Codex y recibir el carril clasificado, la pregunta de rama antes de cada cambio de código
y la petición de aprobación antes de commit, push y PR. Solo el responsable decide aprobaciones y la desactivación
de controles. El asistente no puede.

## Qué resultado obtiene

- El arnés instalado según el plan aceptado `13b94b1e…92767e`.
- `.rsc.json` declara `gitPermissions: false` y los opt-outs `auto-update`, `knowledge-sync`, `ftd-close` y
  `worktree-cleanup`.
- `.claude/settings.json` no concede permisos de Git y conserva la coautoría (`includeCoAuthoredBy: true`).
- `check_notices` pasa con el arnés presente.

## Qué debe respetar

- ADR-012 (sin código FujiNet), ADR-013 (avisos de terceros) y las capas de ADR-002, que `check_layering` verifica.
- La regla del proyecto: aprobación previa de cada commit, push y PR; ni flasheo ni salida GPIO sin aprobación.
- HUMENGIA: CT-1 (Node aceptado solo como excepción explícita, H1) y procedencia del aporte de la IA (DEC-002 §6).
- Las decisiones H1–H6 de [decisions.md](../../harness/decisions.md).

## Cómo lo demuestra

- `python -I tools/check_notices.py` sale en OK con las copias locales de rsc presentes, y en FALLO con una licencia
  no declarada agregada a Git o oculta solo por `.git/info/exclude` (autoprueba
  `test_only_committed_gitignore_hides_local_copies`).
- `python -I tools/check_layering.py` sale en OK; `python -I -m unittest discover -s tools/tests` da 69 pruebas en OK.
- `.rsc.json` y `.claude/settings.json` muestran los valores de «Qué resultado obtiene».
- La CI del PR (host tests, layering, ESP-IDF) pasa sin cambios.
- La evidencia queda en el commit, en la CI del PR y en el ensayo en un clon limpio registrado en rsc-brain.

## Comportamiento

- Camino principal: en un clon con el arnés construido, los hooks de Claude Code clasifican el carril, guardan la
  rama y piden aprobación de Git. En Codex, los hooks quedan activos tras confirmar `/hooks`.
- Casos límite: en un clon sin `sync`, Claude Code solo avisa de que el arnés falta y no bloquea nada; Codex no
  tiene hooks. Sin Node, los hooks no hacen nada.
- Errores: si rsc se publicara en una versión nueva, no se instala sola (opt-out `auto-update`). El aviso de
  versión solo informa.

## Criterios de aceptación

- Dado el árbol de esta rama con rsc construido, cuando se ejecuta `check_notices`, entonces termina con OK.
- Dado un `LICENSE` no declarado agregado a Git o oculto solo por `.git/info/exclude`, cuando se ejecuta
  `check_notices`, entonces termina con código 1 y nombra su carpeta.
- Dado `.rsc.json` de esta rama, cuando se lee, entonces `gitPermissions` es `false` y `optOuts` contiene
  `auto-update`, `knowledge-sync`, `ftd-close` y `worktree-cleanup`.
- Dado `.claude/settings.json`, cuando se lee, entonces no tiene `permissions` ni `attribution`, e
  `includeCoAuthoredBy` es `true`.
- Dado el PR de esta rama, cuando corre la CI, entonces todos sus jobs pasan.

## Puntos a clarificar

- **suposición tomada** — La rama es de riesgo 2 por «harness, secrets», probablemente por la plantilla
  `01-TOOLS/_TEMPLATE` (`.env.example` y `CREDENTIALS.md`, sin valores). *Base:* mensaje del guard de rsc.
  *Riesgo:* si fuera otro archivo, la clasificación no cambia el contenido de esta spec.
- **decisión diferida** — Completar `constitution.md` y revisar `skill/asdd`, que rsc seleccionó por una
  autenticación que RetroBridge no tiene.
- **decisión diferida** — Revisar el hook `post-merge` cuando Git sea 2.31 o posterior.
- **suposición tomada** — La spec se aprueba tal como está. *Base:* aprobación explícita del responsable
  («Apruebo y commitea», 2026-11-17). *Riesgo:* si la revisión de 3 lentes encuentra un defecto, se corrige y se
  vuelve a pedir aprobación.
