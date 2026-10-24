# retrobridge-chile
Independent firmware for Atari retro systems, initially supporting Atari XL/XE, with future Atari 2600 support planned.

## License

Copyright (C) 2026 — copyright holder pending confirmation.

RetroBridge Chile is free software: you can redistribute it and/or modify it under the terms of the
GNU General Public License as published by the Free Software Foundation, **version 3 only**
(SPDX: `GPL-3.0-only`). It is distributed in the hope that it will be useful, but WITHOUT ANY WARRANTY;
without even the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the full
text in [LICENSE](LICENSE).

Third-party components keep their own licenses, listed in [THIRD_PARTY_NOTICES.md](THIRD_PARTY_NOTICES.md).
The repository is private for now; making it public is a separate, later decision.

- Architecture: [docs/arquitectura-inicial.md](docs/arquitectura-inicial.md) and [docs/adr/](docs/adr/)
- Toolchain and build/test commands: [docs/toolchain.md](docs/toolchain.md)
- Sources consulted: [docs/fuentes.md](docs/fuentes.md)
- Hardware notes: [docs/hardware/](docs/hardware/); measurements and validation reports: [docs/mediciones/](docs/mediciones/)
  (first physical validation, H3c: [2026-10-24](docs/mediciones/h3c-validacion-fisica-2026-10-24.md))
- H4 SIO hardware gate (in preparation, not approved): [analysis](docs/hardware/h4-puerta-sio.md),
  [passive measurement procedure](docs/hardware/procedimiento-medicion-pasiva-sio.md); H5 receive-only design
  proposal: [docs/diseno/h5-escucha-solo-recepcion.md](docs/diseno/h5-escucha-solo-recepcion.md)
- Autonomous firmware diagnostics (`diag` console command, `rb_hw_diag`):
  [docs/diseno/h4-diagnostico-autonomo.md](docs/diseno/h4-diagnostico-autonomo.md)
