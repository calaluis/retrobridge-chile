# ADR-013 — Licencia GPL-3.0-only y avisos de terceros separados

- Estado: Aprobada (2026-10-23). Revisión pendiente de aprobación: cambia la licencia propietaria por
  GPL-3.0-only.

## Decisión

- El código y la documentación propios se distribuyen bajo la **GNU General Public License, versión 3
  únicamente** (SPDX: `GPL-3.0-only`). `LICENSE` contiene el texto oficial completo, sin modificar, obtenido de
  https://www.gnu.org/licenses/gpl-3.0.txt (SHA-256 `3972dc9744f6499f0f9b2dbf76696f2ae7ad8af9b23dde66d6af86c9dfb36986`).
- Se elige "solo versión 3" y no "versión 3 o posterior", para no aceptar por adelantado los términos de
  versiones futuras de la GPL.
- `THIRD_PARTY_NOTICES.md`, **separado** de `LICENSE`, enumera las dependencias, sus licencias y las
  obligaciones de atribución.
- El aviso de copyright aparece en `README.md`. El titular de los derechos queda **pendiente de confirmación**.

## Justificación

Es un proyecto personal de afición y aprendizaje. Las dependencias actuales son compatibles con incorporarse a
una obra GPL-3.0:

- ESP-IDF: Apache-2.0, compatible con GPLv3.
- FreeRTOS: MIT.
- FatFs: licencia de estilo BSD.
- Unity: MIT.

## Consecuencias

- Quien distribuya binarios del firmware debe ofrecer el código fuente correspondiente bajo GPL-3.0 y conservar
  los avisos de terceros.
- El repositorio sigue siendo **privado**. Publicarlo es una decisión posterior e independiente de esta.
- La política de desarrollo independiente (ADR-012) se mantiene sin cambios.
- Las dependencias nuevas deben tener licencias compatibles con GPL-3.0. Se revisa en cada PR, junto con
  `THIRD_PARTY_NOTICES.md`.

## Verificación

`tools/check_notices.py` en CI exige que aparezcan en `THIRD_PARTY_NOTICES.md`:

- todo directorio vendorizado con licencia propia;
- toda dependencia declarada en `idf_component.yml`.

## Pendiente

- Confirmar el nombre de la persona natural o jurídica titular de los derechos y reemplazar la línea de
  copyright de `README.md`.
- Decidir si se agregan cabeceras SPDX (`SPDX-License-Identifier: GPL-3.0-only`) a los archivos fuente. Eso
  modifica código y queda fuera de este ajuste documental.
