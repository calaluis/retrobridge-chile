# ADR-013 — Licencia de uso personal y avisos de terceros

## Decisión para la edición pública

- El código, el firmware y la documentación propios se publican bajo la [licencia de uso personal](../../LICENSE) aprobada para esta edición.
- El titular del código propio es **Luis Rodrigo Carrasco Lagos**. El contacto para autorizaciones comerciales es [@calaluis](https://github.com/calaluis).
- Los componentes de terceros se identifican en [THIRD_PARTY_NOTICES.md](../../THIRD_PARTY_NOTICES.md) y conservan sus respectivas licencias.
- Las contribuciones se proponen por [pull request](../../CONTRIBUTING.md).

## Verificación

`tools/check_notices.py` verifica que los directorios con licencias vendorizadas y las dependencias declaradas en `idf_component.yml` figuren en los avisos de terceros.
