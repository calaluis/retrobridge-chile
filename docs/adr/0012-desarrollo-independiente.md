# ADR-012 — Desarrollo independiente con trazabilidad de fuentes

- Estado: Aprobada (2026-10-23); D-13. Enmendada el 2026-11-20: ver «Enmienda 1», excepción para el código propio
  del responsable.

## Decisión

**No** se afirma un procedimiento formal de sala limpia, porque existe experiencia previa con el firmware original
de FujiNet y no podemos demostrar aislamiento. En su lugar:

1. No se copia, traduce ni adapta código fuente de FujiNet ni de otros emuladores de periféricos SIO.
2. El diseño se basa en documentación pública de protocolos y hardware y en la documentación oficial de ESP-IDF.
3. Las fuentes se registran en `docs/fuentes.md`, y cada PR declara las suyas (plantilla de PR obligatoria).
4. Las decisiones quedan en ADR con su justificación propia.

## Enmienda 1 (2026-11-20, aprobada por el responsable): código propio del responsable

El punto 1 tiene una sola excepción: el código que el propio responsable escribió y aportó a FujiNet. Su autor
conserva los derechos: FujiNet es GPL-3.0 y no exige acuerdo de contribución (CLA) ni cesión de derechos
(revisados `CONTRIBUTING.md` y `.github/`; no existe `CLA.md`).

Alcance: solo los PRs de FujiNetWIFI/fujinet-firmware #1722, #1756, #1773 y #1804 (cassette y FSK A8CAS),
publicados juntos en la release `calaluis/fujinet-firmware` `v1.6.2-atari-fsk-beta.1`. Todos sus commits son del
responsable (algunos con un asistente de IA como coautor); ninguno tiene otro autor humano.

Condiciones:

1. Entra solo lo que el responsable añadió: los archivos que creó y las líneas `+` de sus commits. No entra el
   código de FujiNet que esos PRs modifican, ni el contexto ajeno de los diffs, ni nada copiado de FujiNet.
2. Cuarentena: los diffs viven fuera del repositorio (`D:\rb-e0\fsk-propio`, con su `LEEME.md` de procedencia,
   no versionado). Ahí se separan las líneas propias antes de que algo entre a RetroBridge.
3. Se adapta, no se pega. Lo que depende de clases o cabeceras de FujiNet (`global_types.h`, `fnio.h`,
   `debug.h`, `bus.h`, `pinmap.h`, `cassette.*`, `disk.*`) se reescribe. Rigen las capas (ADR-002) y el ADR-011.
4. Cada PR que use este código registra en `docs/fuentes.md` qué PR y commit de origen usó.

Consecuencia: el punto 1 sigue vigente para todo el código de terceros. Esta excepción no permite consultar otro
código de FujiNet ni de emuladores.
