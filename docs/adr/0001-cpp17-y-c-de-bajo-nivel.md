# ADR-001 — C++17 como lenguaje principal y C para bajo nivel

- Estado: Aprobada (2026-10-23)
- Contexto: arquitectura v0.2, §8

## Contexto

El firmware necesita tipos de valor seguros (por ejemplo, `SectorNumber`), RAII y `constexpr` sin costo en
tiempo de ejecución. También debe integrarse con las APIs C de ESP-IDF y tener ISR en IRAM. Por defecto, ESP-IDF
v6.1 compila C++ con `gnu++26`.

## Decisión

- C++17 con extensiones GNU (`-std=gnu++17`) en todo el código propio, fijado por componente en
  `cmake/rb_build_flags.cmake`.
- C solo para ISR, inicialización de controladores e integración directa con APIs C de ESP-IDF.

## Alternativas descartadas

- C17 puro (v0.1): sin tipos de valor fuertes ni RAII.
- C++20/26: más superficie de lenguaje de la que necesitamos, y comportamiento menos homogéneo entre compiladores
  de host.

## Verificación

`rb/core/toolchain_checks.hpp` (incluido por cada componente) aborta la compilación si `__cplusplus != 201703L`.
La prueba de host `test_toolchain` lo hace visible en CTest.
