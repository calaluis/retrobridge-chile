# ADR-011 — Memoria estática en la ruta crítica SIO

- Estado: Aprobada (2026-10-23)
- Contexto: arquitectura v0.2, §9.1

## Decisión

- Después de la inicialización no se usan `new`, `malloc`, `std::vector` ni `std::string` en la ruta SIO.
- Los búferes tienen tamaño fijo y un propietario declarado: `SioSession` tiene 257 B para datos de respuesta; el
  repositorio tiene slots fijos.
- Los datos se pasan con `Span<T>` no propietario.

## Verificación

En H7, tras 10 000 lecturas de sector, el heap libre y el bloque libre mayor deben coincidir con los valores
iniciales.
