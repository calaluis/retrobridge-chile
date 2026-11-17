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

### rsc 4.1.7 — `.claude/rsc-bootstrap.mjs`

- Origen: paquete npm `@ericrisco/rsc` versión `4.1.7` (repositorio https://github.com/ericrisco/rsc-harness),
  archivo `targets/clone-bootstrap.mjs`.
- Copia sin modificar, escrita por `rsc onboard`. SHA-256 de ambos archivos:
  `0ecb6ca5ed347d102a3b5df3f71fe99ab3b1951babbd571cbf13c5ec9dc33dce`.
- Uso: arnés de asistentes (Claude Code); se ejecuta con Node en los hooks de `.claude/settings.json`. No forma
  parte del firmware, del host ni de la CI.
- Licencia: MIT. Texto del archivo `LICENSE` del paquete, reproducido sin cambios:

```text
MIT License

Copyright (c) 2026 Eric Risco

Permission is hereby granted, free of charge, to any person obtaining a copy
of this software and associated documentation files (the "Software"), to deal
in the Software without restriction, including without limitation the rights
to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
copies of the Software, and to permit persons to whom the Software is
furnished to do so, subject to the following conditions:

The above copyright notice and this permission notice shall be included in all
copies or substantial portions of the Software.

THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
SOFTWARE.
```

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
