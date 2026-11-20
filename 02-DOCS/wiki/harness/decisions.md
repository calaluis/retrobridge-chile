# Decisiones del responsable

Una línea por decisión, con fecha y de dónde sale.

## 2026-11-20 — menu-xex

- El cargador de `.xex` solo apaga el BASIC por `PORTB`; no ajusta `RAMTOP` ni reabre `E:` («Solo PORTB»).
- Un segmento en la página 1 (`$0100`-`$01FF`) da ` XEX NO COMPATIBLE`.
- Se acepta el relleno XMODEM al final de un `.xex` (regla A: múltiplo de 128, último byte `$1A`, resto que no es
  segmento). Tolerancia sin fuente, anotada en ADR-017 enmienda 11.
- En una demo sin capturas, vale la observación del responsable si él la acepta (menu-presentacion y menu-xex).

Detalle: `02-DOCS/raw/worklog/2026-11-20-menu-xex.md`.
