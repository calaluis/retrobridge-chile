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

## Enmienda 2 (2026-11-26, aprobada por el responsable): archivos propios tal cual y lectura de `cassette.cpp`

Motivo: la tarea `cassette-motor-fujinet` pide ejecutar en RetroBridge el mismo motor FSK que la beta, con el
código original siempre que se pueda.

1. **Archivos propios tal cual.** Cambia la condición 3 de la enmienda 1 solo para los archivos que el
   responsable creó en los PRs #1722, #1756 y #1773 (`casTape.h`, `casFSK.*`, `casFSKLoader.*`,
   `cassetteFSK.*`, `cassetteFSKLoader.*`, `cassetteFSKRead.*`), en el commit
   `cf3382fafeed9b57e307b8618d7ba7173e7e6ec9` (etiqueta `v1.6.2-atari-fsk-beta.1`). Pueden entrar sin
   reescribir, en `components/rb_fujinet_fsk/lib/`. Un cambio solo se admite si es imprescindible para compilar;
   cada uno lleva nombre y motivo en `FUENTE.md`. `tools/check_fujinet_fsk.py` compara cada archivo con el
   SHA-256 del commit en la CI.
2. **Cabeceras de FujiNet.** Las que esos archivos incluyen (`global_types.h`, `fnio.h`, `bus.h`, `debug.h`,
   `pinmap.h`, `soc/uart_periph.h`) no se copian: se escriben adaptadores propios con el mismo nombre.
3. **`cassette.cpp`, solo lectura.** `lib/device/sio/cassette.cpp` es código ajeno (GPL-3.0). Puede leerse en el
   commit fijado, solo para describir su comportamiento. No se copia nada de él. Su papel lo cumplen
   adaptadores escritos para RetroBridge. Esta enmienda no permite leer `sio.cpp` ni otro código de FujiNet.
4. **Licencia.** No cambia la licencia de RetroBridge. Los archivos que entran son del responsable, que conserva
   sus derechos (enmienda 1); no se trata como código de terceros ni se añade a `THIRD_PARTY_NOTICES.md`. Si un
   revisor considera que hace falta otra licencia para esos archivos, ese cambio necesita una aprobación aparte.

Consecuencia: el resto de la enmienda 1 sigue igual. Todo código de terceros sigue fuera (punto 1).
