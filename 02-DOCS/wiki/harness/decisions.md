# Harness decisions

- Accepted plan `13b94b1e90b3581f31cf0aec01257e033de3d48c97336f47f20f2e5cde92767e`.
- Project kind: mixed.
- SDD: selected.

## Decisiones del responsable sobre el arnés (2026-11-17)

Alcance: solo el arnés de asistentes (rsc 4.1.7 para Claude Code y Codex). Las decisiones del producto
siguen en `docs/adr/` y en las issues; este archivo no las duplica (HUMENGIA DEC-002 §6).

Cada decisión indica qué propuso la IA (Claude) y qué aprobó el responsable humano.

| # | Decisión | Fundamento | Procedencia |
|---|---|---|---|
| H1 | Se acepta Node (≥ 18) como herramienta del arnés: hooks `.rsc/*.mjs` y `.claude/rsc-bootstrap.mjs`. No se usa en el build, las pruebas ni la CI de RetroBridge. | Excepción explícita a CT-1 (HUMENGIA DEC-003). Node solo corre en eventos de los asistentes. | Claude identificó el conflicto con CT-1; el responsable lo aceptó. |
| H2 | Instalación en la rama `chore/rsc-harness` desde `origin/main` (`935ce1c`), separada del trabajo del producto, sin commit hasta revisar el diff. | Una PR, un propósito; revisión humana antes de integrar (N2). | Propuesta de Claude, aprobada. El SHA-256 del plan cambió de `6995e74f…` a `13b94b1e…` al avanzar `origin/main`, con contenido idéntico; se aprobó el nuevo antes de aplicar. |
| H3 | Desactivados antes de instalar, con `.rsc/.no-*` y registrados en `optOuts` de `.rsc.json`: `auto-update`, `knowledge-sync`, `ftd-close` y `worktree-cleanup`. | Evitan cambios sin revisión: actualización automática del arnés, commits con `--no-verify` y push a `rsc/knowledge`, cierre automático de documentos y borrado de worktrees tras un merge. | Hallado por Claude leyendo el código de rsc 4.1.7 y probado en una copia temporal; aprobado. Estos marcadores no forman parte del hash del plan. |
| H4 | `rsc git-permissions off` (`gitPermissions: false`): el asistente vuelve a pedir aprobación para commit, push y PR. | Regla del proyecto: cada commit, push y PR requiere aprobación previa. | Hallado por Claude en un clon limpio, donde el permiso regía aunque los guards no estuvieran construidos; aprobado. |
| H5 | `.claude/settings.json` usa `includeCoAuthoredBy: true` en lugar de `attribution` vacía, que rsc pone por defecto. | Procedencia identificable del aporte de la IA (HUMENGIA DEC-002 §6). Si solo se quita la clave, rsc la vuelve a escribir vacía en cada `sync`. | Propuesta de Claude, verificada tras `sync` en una copia temporal; aprobada. |
| H6 | `tools/check_notices.py` revisa solo lo versionable: archivos seguidos más archivos no ignorados por un `.gitignore` versionado (`--exclude-per-directory=.gitignore`). | Las copias locales de rsc, ignoradas y con licencias MIT de terceros, hacían fallar el control en local. `.git/info/exclude` no puede ocultar código de terceros. | Corrección de Claude; el responsable pidió `--exclude-per-directory` en lugar de `--exclude-standard`. Probada con controles negativos. |

## Observaciones pendientes

- `skill/asdd` fue seleccionada por "complejidad de autenticación", pero RetroBridge no tiene autenticación. Es una
  inferencia de rsc no confirmada; se conserva la skill.
- `.git/hooks/post-merge` no se instaló porque Git 2.27 no admite `rev-parse --path-format` (requiere 2.31 o
  posterior). Con un Git más nuevo, una instalación o un `sync` posterior sí lo crearía; H3 lo deja sin efecto útil.
  Hay que revisarlo tras actualizar Git.
- `02-DOCS/wiki/sdd/constitution.md` es un borrador de rsc. No se completa en esta rama.

## Activación en otro clon

El PR no basta para dejar configurados los asistentes, porque `.rsc/`, `.codex/hooks.json`,
`.claude/settings.local.json` y el perfil personal no se versionan. En cada clon hay que:

1. Ejecutar `npx @ericrisco/rsc@4.1.7 sync`, que reconstruye `.rsc/`, los marcadores de H3 y la configuración de
   Claude Code y Codex.
2. En Codex, confiar una vez en los hooks del proyecto con `/hooks`.

Sin el `sync`, Claude Code solo avisa de que el arnés falta y sus guards no bloquean nada; Codex queda sin hooks.
