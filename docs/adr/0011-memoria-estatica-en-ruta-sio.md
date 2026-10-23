# ADR-011 — Memoria estática en la ruta crítica SIO

- Estado: Aprobada (2026-10-23)
- Contexto: arquitectura v0.2, §9.1

## Decisión

- Después de la inicialización no se usan `new`, `malloc`, `std::vector` ni `std::string` en la ruta SIO.
- Los búferes tienen tamaño fijo y un propietario declarado. El de respuesta de `SioSession` mide **258 B**: 1 byte
  de código, hasta 256 de datos y 1 de checksum (corrección en H3a; antes decía 257). El repositorio tiene slots
  fijos.
- Los datos se pasan con `Span<T>` no propietario.

## Verificación

- Host (H2c): `test_sio_session_allocations` cuenta cada `operator new` durante `SioSession::step()` y exige cero.
- ESP32 (H3c, H3-13): la lectura de sectores desde la microSD forma parte de la ruta SIO en H5.
  - Alrededor de N lecturas, `heap_caps_get_free_size` y el bloque libre mayor no deben cambiar; se confirma con
    una compilación que active el trazado del heap.
  - Abrir sí puede reservar memoria, porque ocurre en el contexto de almacenamiento.
  - Si un controlador de ESP-IDF reserva memoria al leer, se documenta y se mitiga; no se relaja el criterio.
- En H7, tras 10 000 lecturas de sector, el heap libre y el bloque libre mayor deben coincidir con los valores
  iniciales.
