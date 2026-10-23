# ADR-005 — Procesamiento SIO en dos fases

- Estado: Aprobada (2026-10-23). Precisión H2 aprobada el 2026-10-23 (H2-4, H2-5).
- Contexto: arquitectura v0.2, §4.1

## Decisión

1. `SioCommandDispatcher::accept(frame)` decide, sin E/S, entre `Ignore`, `Nak` o `Accepted`.
2. Tras el ACK, `execute(buffer)` invoca el caso de uso y devuelve `Complete` o `Error` con su trama de datos.

## Justificación

- El manual de Atari exige el ACK dentro de t2 ≤ 16 ms.
- COMPLETE, en cambio, tiene un margen que depende del controlador (t5, hasta 255 s).

Separar las fases garantiza t2 aunque la fuente de bytes sea lenta. Es lo que habilita TNFS.

## Precisión H2 (2026-10-23)

**Fase 1, sin E/S.** Puede consultar estado en memoria, como `VirtualDrive::validateRead` (H2b), pero nunca hace
E/S.

| Situación | Decisión | Fuente |
|---|---|---|
| Checksum de la trama de comando inválido | `Ignore` (silencio) | [ATARI §9], [ALT §9] |
| ID de dispositivo ajeno | `Ignore`, línea liberada | [ATARI §9], [ALT §9] |
| Comando no soportado | `Nak` | [ALT §9, §10.9] |
| Parámetros inválidos detectables sin E/S (sector 0 o fuera de la geometría montada) | `Nak`; no hay fase 2 | [ALT §10.9] para la 810; generalizado a la geometría del ATR |
| Resto | `Accepted` | — |

**Fase 2.**

- Si la ejecución tiene éxito: `Complete`.
- Si hay un fallo de ejecución: `Error`.
- **Trama de datos tras `Error`:** solo si el comando es de **lectura**, es decir, el periférico entrega datos y el
  computador espera esa trama. Va con el tamaño que define el comando y su checksum. Los comandos inmediatos o de
  escritura envían `Error` sin datos.
  - Fuente: [ATARI, listado del SO, `WATCOM`]: tras COMPLETE o ERROR, el SO recibe la trama solo en los comandos
    de recepción (`BVS MODATA`) y luego aplica `DERROR`.
  - Fuente: [ALT §9]: "For commands that are read commands, the Error byte is still followed by a data frame".

**Fuentes:**

- [ATARI] Atari, C016555 (1982): §9 y listado del SO.
- [ALT] *Altirra Hardware Reference Manual*, edición 2026-01-02, §9 y §10.9.

## Verificación

- H2b: tabla de decisiones verificada byte a byte en `SioCommandDispatcher`.
- H2c: prueba de sesión con `FakeRemoteByteSource` (50 ms de latencia). El ACK debe salir dentro de t2 y antes de la
  lectura.
