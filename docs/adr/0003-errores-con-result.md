# ADR-003 — Manejo de errores con `Result<T>` y `ErrorCode`

- Estado: Aprobada (2026-10-23); D-2: tipo propio, sin dependencia externa
- Contexto: arquitectura v0.2, §9.2

## Decisión

- Toda función que puede fallar devuelve `Result<T>` marcado `[[nodiscard]]`, con una única
  `enum class ErrorCode : uint8_t` en `rb_core`.
- `RB_ASSERT` solo para invariantes internas (errores de programación). Aborta en el equipo y hace fallar la
  prueba en el host.
- Los datos externos (tramas, ATR, archivos) siempre producen `Result`, nunca `RB_ASSERT`.
- `SioCommandDispatcher` es el único lugar que traduce `ErrorCode` a una respuesta SIO.

## Alternativas descartadas

- Excepciones (ADR-004).
- Códigos `int` sin tipo.
- `std::optional` sin causa.
- `tl::expected`: dependencia externa innecesaria para un tipo pequeño.

## Consecuencias

El enum se divide por contexto si supera unas 30 entradas. `Result` se implementa en H1.
