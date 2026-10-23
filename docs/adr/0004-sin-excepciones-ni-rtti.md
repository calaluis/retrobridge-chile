# ADR-004 — Sin excepciones ni RTTI

- Estado: Aprobada (2026-10-23)

## Decisión

- `-fno-exceptions -fno-rtti` en el firmware **y** en las pruebas de host.
- `sdkconfig.defaults` deja `CONFIG_COMPILER_CXX_EXCEPTIONS` y `CONFIG_COMPILER_CXX_RTTI` deshabilitados, que
  también es el valor por defecto de ESP-IDF.

## Justificación

- Determinismo en la ruta crítica.
- Según la documentación de ESP-IDF, RTTI agrega decenas de kB y lanzar una excepción es órdenes de magnitud más
  lento que devolver un código.
- Usar los mismos flags en el host evita que una prueba pase en el PC y falle al compilar para el equipo.

## Verificación

`toolchain_checks.hpp` aborta la compilación si `__cpp_exceptions` o `__cpp_rtti` están definidos.
