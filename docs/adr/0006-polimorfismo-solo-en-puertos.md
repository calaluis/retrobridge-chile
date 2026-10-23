# ADR-006 — Polimorfismo dinámico solo en puertos y fuera de ISR

- Estado: Aprobada (2026-10-23); D-3

## Decisión

- Métodos virtuales solo en los puertos, y solo llamados desde contexto de tarea.
- La ISR de COMMAND es una función C libre con `IRAM_ATTR`, registrada con `ESP_INTR_FLAG_IRAM`, que solo
  notifica a la tarea SIO.

## Justificación

- En ESP-IDF las vtables residen en flash y no son accesibles con la caché deshabilitada.
- Una llamada virtual cuesta nanosegundos, frente a ~520 µs por byte a 19 200 baudios.

## Revisión

Se pasaría a plantillas solo si las mediciones de H6/H7 lo justifican.
