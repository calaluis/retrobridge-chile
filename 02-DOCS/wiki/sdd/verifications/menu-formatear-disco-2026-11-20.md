---
type: verification
title: Verificación — menu-formatear-disco — 2026-11-20
description: Veredicto de cierre de menu-formatear-disco: gate local, tareas F001-F006, criterios AC1-AC10 y revisión de cierre.
tags: [sdd, verification, menu-formatear-disco]
timestamp: 2026-11-20T05:00:00Z
topic: sdd
slug: menu-formatear-disco
source_state: b1546cb (clean); el commit de cierre solo agrega documentación
---

# Verificación — menu-formatear-disco — 2026-11-20

Rama `feat/menu-formatear-disco` frente a main `c21cbdf`. Main no avanzó: no hubo nada que fusionar. 34 archivos,
1685 líneas agregadas y 29 quitadas.

## Gate (corrida nueva en carpetas de compilación limpias, sobre b1546cb)

- `cmake -S test/host` + `cmake --build`: MinGW ucrt64, `-Werror`, 0 avisos.
- `ctest`: PASS, 65 de 65.
- `cmake -S host` + build: `retrobridge_host.exe` compila.
- `python -I tools/check_layering.py`: OK.
- `python -I tools/check_notices.py`: OK.
- `python -I -m unittest discover -s tools/tests`: OK, 141 pruebas.
- `rsc spec gate`: PASS, 6 puntos abiertos, los 6 con tipo.
- `rsc risk`: tier 1, un revisor.

## Tareas

- [x] F001 dominio y app — 15850fd.
- [x] F002 formateo por SIO — 6c61485.
- [x] F003 crear ATR y lista — 02c3981.
- [x] F004 menú «Crear» y registro — 5e178a0.
- [x] F005 demo en Altirra — de9825b; evidencia en `D:\rb-e0\m46-formateo`.
- [x] F006 documentación — b1546cb.

Controles de cada tarea en `progress/menu-formatear-disco.md`.

## Criterios de aceptación (spec, líneas 112-125)

- [x] AC1 formatear 720 en simple — `test_disk_write`, `test_sio_dispatcher`,
  `test_format_single_and_medium_write_zeros_and_are_logged`; demo F005 (`cmd=F result=complete`).
- [x] AC2 formatear 1040 en media — las mismas pruebas; demo F005 en D3 (`cmd=FM result=complete`, después el DOS
  escribe el sector 1024). La spec dice D2; la prueba y la demo usan D3. La unidad no cambia el camino.
- [x] AC3 720 en media da ERROR sin cambios — `test_format_with_the_other_density_is_an_error` (bytes iguales);
  demo F005 (2 x 8 `cause=density`, ninguna escritura después).
- [x] AC4 protegida, TNFS o menú — `test_format_refused_on_protected_and_empty_drives` (ATR de solo lectura y menú en
  D1); `test_sio_dispatcher` (fuente protegida). TNFS usa la misma fuente de solo lectura (`test_tnfs_image_is_protected`).
- [x] AC5 crear `NUEVO` en D2 — `test_create_mounts_a_new_single_density_atr`; demo F005.
- [x] AC6 nombre existente — `test_create_refuses_an_existing_name`, `test_an_existing_file_is_never_overwritten`.
- [x] AC7 nombre inválido — `test_create_refuses_an_invalid_name`, `test_name_rule`.
- [x] AC8 lista llena — `test_create_refuses_a_full_list`, `test_insert_refuses_a_duplicate_a_bad_name_and_a_full_list`.
- [x] AC9 crear, formatear, guardar y relanzar — demo F005 (directorio de D2 y D3 con `PRUEBA` tras relanzar; SHA
  igual).
- [x] AC10 pruebas anteriores en verde — 65 de 65, incluidas las de `menu-escritura-disco` y de los menús.

## Revisión de cierre

- Un revisor (refuter-correctness) sobre toda la rama frente a la spec. Sin must-fix.
- **Decidido por el responsable** (should-fix, fuera de la spec): `host/src/main.cpp:383` sale con «no .atr images»
  si la carpeta `--images` está vacía, así que nunca se llega a `N`. Opción elegida: documentarlo. `host/README.md`
  dice ahora que la carpeta necesita al menos una ATR y que el primer disco se copia desde el PC.
- **Corregido** (nit): la enmienda 1 de ADR-019 no decía cuánto duran D2 y D3 de STATUS. Línea agregada.
- **Aceptado** (nit): `menu_create name=` lleva la extensión (`NUEVO.ATR`); con `invalid_name`, el nombre rechazado
  no se registra (`name=-`).
- Atacado sin hallazgo: criterios contra código y pruebas, formateo de punta a punta, ADR-011 (sin reservas),
  firmware (`allowWrites = false`, NAK), deriva entre documentos y código, posiciones tras insertar, transporte.

## Plan de vuelta atrás

- `git revert` del merge del PR. No hay migraciones ni datos. Una ATR formateada no vuelve atrás: se recupera de una
  copia. Las ATR creadas con `N` quedan en la carpeta; se borran a mano.

## Capas no ejecutadas

- **HERRAMIENTA-AUSENTE** — build del firmware ESP-IDF: sin `idf.py` en este PC; lo corre el CI.
- **SUSTITUIDA** — sanitizers (`-DRB_SANITIZE=ON`): corrió el build normal, que no detecta accesos fuera de rango ni
  conducta indefinida. Los corre el CI.

## Hallazgos descartados

- «Un índice del catálogo puede apuntar a otro archivo tras crear»: `shiftEntries` corre todas las posiciones
  guardadas; `test_create_keeps_the_names_of_mounted_images` lo cubre con una imagen montada detrás de la nueva.
- «El formateo puede escribir en una ATR de solo lectura»: `DiskImage::format` comprueba `readOnly` antes de
  escribir; `test_format_refused_on_protected_and_empty_drives` deja el SHA igual.

## Veredicto: FALLA por ahora — faltan el build del firmware y los sanitizers, que solo corre el CI. Todos los
criterios de aceptación pasan. Con el CI en verde tras el push, el veredicto pasa a PASA.
