# Avisos de terceros

Este archivo enumera el software de terceros que usa RetroBridge Chile y sus licencias. Está **separado** de
[`LICENSE`](LICENSE), que contiene la licencia GPL-3.0-only del código propio (ADR-013). Cada componente de
terceros conserva su propia licencia; todas las listadas son compatibles con su incorporación a una obra
GPL-3.0.

Toda dependencia nueva, sea vendorizada o del gestor de componentes de ESP-IDF, se agrega aquí en el mismo PR.
`tools/check_notices.py` lo verifica en CI.

## Incluido en el repositorio (vendorizado)

### Unity v2.7.0 — `test/host/unity`

- Origen: https://github.com/ThrowTheSwitch/Unity, etiqueta `v2.7.0` (commit `b6763fbd9cedfacaa89e2ad9fd00d615a234e355`).
- Licencia: MIT. El texto completo está en [`test/host/unity/LICENSE.txt`](test/host/unity/LICENSE.txt).
- Uso: solo pruebas de host; no forma parte del firmware.
- Archivos incluidos sin modificar: `src/unity.c`, `src/unity.h`, `src/unity_internals.h`.

## Enlazado en el firmware (no vendorizado)

### ESP-IDF v6.1

- Origen: https://github.com/espressif/esp-idf, etiqueta `v6.1`.
- Licencia: Apache License 2.0. Algunos componentes incluidos en ESP-IDF tienen licencias propias (por ejemplo,
  FreeRTOS con MIT y FatFs con una licencia de estilo BSD). La lista completa está en la documentación de
  ESP-IDF, en la sección *Copyrights and Licenses*.
- Al distribuir binarios hay que conservar los avisos que exigen esas licencias. La lista definitiva de
  componentes enlazados se revisa en cada hito que agregue componentes de ESP-IDF (H3: FATFS y SD/MMC).

## Herramientas de desarrollo (no se distribuyen)

- `actions/checkout` (GitHub Actions, MIT), fijado por commit en `.github/workflows/ci.yml`.
- Imagen Docker `espressif/idf:v6.1`, usada solo en CI.
