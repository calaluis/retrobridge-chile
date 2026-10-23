# ADR-013 — `LICENSE` y `THIRD_PARTY_NOTICES.md` separados

- Estado: Aprobada (2026-10-23)

## Decisión

- `LICENSE` cubre solo el código y la documentación propios (todos los derechos reservados).
- `THIRD_PARTY_NOTICES.md` enumera dependencias, licencias y obligaciones de atribución.

## Verificación

`tools/check_notices.py` en CI exige que aparezcan en `THIRD_PARTY_NOTICES.md`:

- todo directorio vendorizado con licencia propia;
- toda dependencia declarada en `idf_component.yml`.

## Pendiente

Validar con asesoría legal la redacción de `LICENSE` y el nombre del titular de los derechos.
