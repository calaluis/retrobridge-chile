# retrobridge-chile
Independent firmware for Atari retro systems, initially supporting Atari XL/XE, with future Atari 2600 support planned.

## Licencia

Copyright © 2026 @calaluis.

El código fuente y el firmware propios de RetroBridge Chile se ofrecen bajo la [licencia de uso personal](LICENSE). Para autorizaciones comerciales, contacta a [@calaluis](https://github.com/calaluis).

Los componentes de terceros conservan sus licencias, detalladas en [THIRD_PARTY_NOTICES.md](THIRD_PARTY_NOTICES.md). Las contribuciones se realizan mediante [pull requests](CONTRIBUTING.md).

Consulta la [guía de compilación](docs/toolchain.md) para construir el firmware y ejecutar las pruebas. Los informes del laboratorio documentan sus compilaciones históricas.

- Architecture: [docs/arquitectura-inicial.md](docs/arquitectura-inicial.md) and [docs/adr/](docs/adr/)
- Toolchain and build/test commands: [docs/toolchain.md](docs/toolchain.md)
- Sources consulted: [docs/fuentes.md](docs/fuentes.md)
- Hardware notes: [docs/hardware/](docs/hardware/); measurements and validation reports: [docs/mediciones/](docs/mediciones/)
  (first physical validation, H3c: [2026-10-24](docs/mediciones/h3c-validacion-fisica-2026-10-24.md))
- H4 SIO hardware gate (in preparation, not approved): [analysis](docs/hardware/h4-puerta-sio.md),
  [passive measurement procedure](docs/hardware/procedimiento-medicion-pasiva-sio.md); H5 receive-only design
  proposal: [docs/diseno/h5-escucha-solo-recepcion.md](docs/diseno/h5-escucha-solo-recepcion.md)
- Autonomous firmware diagnostics (`diag` console command, `rb_hw_diag`):
  [docs/diseno/h4-diagnostico-autonomo.md](docs/diseno/h4-diagnostico-autonomo.md); validated on the board over USB
  (H4 software, not the SIO hardware gate): [2026-10-24](docs/mediciones/h4-validacion-diagnostico-2026-10-24.md);
  H3c storage regression with the H4 firmware: [2026-10-25](docs/mediciones/h4-regresion-h3c-2026-10-25.md)
- Host boot menu over NetSIO (Altirra): [ADR-017](docs/adr/0017-menu-de-arranque.md); read-only TNFS sources
  (`--sources`), loaded into memory and mounted from the Atari menu:
  [ADR-018](docs/adr/0018-cliente-tnfs-de-solo-lectura.md); sector writes to `--images` ATRs:
  [ADR-019](docs/adr/0019-escritura-de-sectores.md)
