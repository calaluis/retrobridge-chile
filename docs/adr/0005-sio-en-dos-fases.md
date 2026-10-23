# ADR-005 — Procesamiento SIO en dos fases

- Estado: Aprobada (2026-10-23)
- Contexto: arquitectura v0.2, §4.1

## Decisión

1. `SioCommandDispatcher::accept(frame)` decide, sin E/S, entre `Ignore`, `Nak` o `Accepted`.
2. Tras el ACK, `execute(buffer)` invoca el caso de uso y devuelve `Complete` o `Error` con su trama de datos.

## Justificación

- El manual de Atari exige el ACK dentro de t2 ≤ 16 ms.
- COMPLETE, en cambio, tiene un margen que depende del controlador (t5, hasta 255 s).

Separar las fases garantiza t2 aunque la fuente de bytes sea lenta. Es lo que habilita TNFS.

## Verificación

En H2, una prueba de sesión con `FakeRemoteByteSource` (50 ms de latencia) debe emitir el ACK dentro de t2.
