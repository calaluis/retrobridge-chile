# ADR-012 — Desarrollo independiente con trazabilidad de fuentes

- Estado: Aprobada (2026-10-23); D-13

## Decisión

**No** se afirma un procedimiento formal de sala limpia, porque existe experiencia previa con el firmware original
de FujiNet y no podemos demostrar aislamiento. En su lugar:

1. No se copia, traduce ni adapta código fuente de FujiNet ni de otros emuladores de periféricos SIO.
2. El diseño se basa en documentación pública de protocolos y hardware y en la documentación oficial de ESP-IDF.
3. Las fuentes se registran en `docs/fuentes.md`, y cada PR declara las suyas (plantilla de PR obligatoria).
4. Las decisiones quedan en ADR con su justificación propia.
