# Registro de fuentes consultadas (ADR-012)

Cada PR agrega aquí las fuentes nuevas que consultó. Solo se registra documentación pública de protocolos,
especificaciones de hardware y documentación oficial. No se consulta código fuente de FujiNet ni de otros
emuladores de periféricos SIO.

| Fecha | Hito | Fuente | Para qué se usó |
|---|---|---|---|
| 2026-10-23 | Diseño | Atari, *Home Computer Technical Reference Notes* (C016555, 1982), sec. 9 y apéndice K — https://ftp.mirrorservice.org/sites/www.bitsavers.org/pdf/atari/400_800/CO16555_Atari_Home_Computer_Technical_Reference_Notes_1982.pdf | Conector SIO, parte eléctrica, trama, checksum, respuestas, t0–t5, comandos 810 |
| 2026-10-23 | Diseño | ESP-IDF — GPIO: https://docs.espressif.com/projects/esp-idf/en/stable/esp32/api-reference/peripherals/gpio.html | Pines de arranque, flash/PSRAM, solo entrada |
| 2026-10-23 | Diseño | ESP-IDF — Running Applications on Host: https://docs.espressif.com/projects/esp-idf/en/stable/esp32/api-guides/host-apps.html | Evaluación del objetivo `linux` (descartado, ADR-010) |
| 2026-10-23 | Diseño/H0 | ESP-IDF v6.1 — C++ Support: https://docs.espressif.com/projects/esp-idf/en/v6.1/esp32/api-guides/cplusplus.html | Estándar por defecto `gnu++26`, cambio de estándar por componente, excepciones y RTTI, vtables en flash |
| 2026-10-23 | Diseño | ESP-IDF — Concurrency Constraints for Flash on SPI1: https://docs.espressif.com/projects/esp-idf/en/stable/esp32/api-reference/peripherals/spi_flash/spi_flash_concurrency.html | Reglas de ISR en IRAM |
| 2026-10-23 | Diseño | Wikipedia, "Atari SIO" — https://en.wikipedia.org/wiki/Atari_SIO | Contexto general |
| 2026-10-23 | Diseño | Atarimax, SIO2PC cap. 1 — https://atarimax.com/sio2pc/documentation/chapter1.html | Contexto de pinout |
| 2026-10-23 | Diseño | Descripción del formato ATR — https://ftp.pigwa.net/upload/mirror%20z%2085.11.68.68/85.11.68.68/stuff/collections/nir_dary_cds/Tech%20Info/file%20formats/afmtatr.htm | Cabecera ATR |
| 2026-10-23 | Diseño | bw-atari8-tools, notas ATR — https://slackware.uk/~urchlay/repos/bw-atari8-tools/plain/ksiders/atr.txt | Variantes de ATR |
| 2026-10-23 | H0 | Etiquetas de ESP-IDF en GitHub (`git ls-remote`) — https://github.com/espressif/esp-idf | Selección de v6.1 como última v6.x estable |
| 2026-10-23 | H0 | Unity v2.7.0 — https://github.com/ThrowTheSwitch/Unity | Marco de pruebas vendorizado (MIT) |
| 2026-10-23 | H2 (diseño) | Atari, C016555 (1982): §5 (fig. 5-19, DSKTIM: timeout de 7 s para operaciones de disco salvo el formateo), §9 (manejo de errores, reintentos, timeout de ACK de 1/60 s), listado del SO (`WATCOM`/`WAIT`: la trama de datos se recibe tras ERROR solo en comandos de recepción; códigos `DNACK` $8B y `DERROR` $90) | Decisiones H2-1 a H2-8 y precisión del ADR-005. El listado se leyó para conocer el comportamiento esperado del computador; no se copia código. |
| 2026-10-23 | H2 (diseño) | Avery Lee, *Altirra Hardware Reference Manual*, edición 2026-01-02, §9, §10.2 (tabla 44), §10.8 (tablas 54 y 55), §10.9 — https://www.virtualdub.org/downloads/Altirra%20Hardware%20Reference%20Manual.pdf | NAK ante parámetros inválidos (H2-4), alcance de ERROR (H2-5), campos y valores de STATUS (H2-6/H2-7). Documentación de hardware, no código de emulador. |
