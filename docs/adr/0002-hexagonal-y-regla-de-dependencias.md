# ADR-002 — Arquitectura hexagonal y regla de dependencias

- Estado: Aprobada (2026-10-23)
- Contexto: arquitectura v0.2, §2

## Contexto

La lógica del disco y del protocolo debe poder probarse sin ESP32, y TNFS se agregará después sin reescribirla.

## Decisión

Capas Dominio (`rb_domain`), Aplicación (`rb_application`) e Infraestructura (`rb_esp_*`), más el contexto Bus
SIO (`rb_sio`) como capa anticorrupción y el núcleo compartido `rb_core`. Las dependencias apuntan siempre hacia
el dominio: `rb_core ← rb_domain ← rb_application ← rb_sio ← infraestructura ← main`.

## Precisión respecto de la v0.2

ESP-IDF expone sus componentes comunes (freertos, log, esp_common, etc.) a **todos** los componentes, así que
`REQUIRES` de CMake no basta para impedir que un componente puro incluya cabeceras de plataforma. La regla se
hace cumplir con tres mecanismos:

1. **Build de host:** los componentes puros se compilan sin ESP-IDF; cualquier cabecera de plataforma rompe el
   build.
2. **`tools/check_layering.py` en CI:** prohíbe cabeceras de plataforma e inclusiones hacia capas superiores.
3. **`REQUIRES` y `target_link_libraries`:** limitan el grafo entre componentes propios.

## Enmienda H1 (2026-10-23, opción A aprobada): puertos de salida del dominio

En la v0.2, `DiskImage` (dominio) usaba `ImageByteSource`, pero esa interfaz estaba en `rb_application`, lo que
habría obligado al dominio a depender de una capa superior.

**Regla:** el dominio puede definir sus propios puertos de salida, siempre que esas interfaces dependan solo de
`rb_core`. El puerto pertenece a quien lo necesita: así se aplica la inversión de dependencias.

- `ImageByteSource` pasa a `rb_domain/include/rb/domain/image_byte_source.hpp`, con la misma firma y el mismo
  contrato.
- `ImageRepository`, `Clock` y `SioTransport` siguen en `rb_application`: abrir y cerrar medios, medir tiempo y
  transportar bytes son orquestación, no reglas del dominio.
- Los adaptadores de infraestructura implementan la interfaz del dominio desde fuera. La dependencia va de
  infraestructura hacia el dominio, nunca al revés.
- La regla de dependencias no cambia: `rb_domain` sigue sin depender de nada fuera de `rb_core`, y
  `check_layering.py` la verifica sin modificaciones.

## Verificación

El job `repository-checks` del CI, con autopruebas que inyectan violaciones deliberadas
(`tools/tests/test_checks.py`).
