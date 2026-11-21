# Casetera en solo lectura: `cas mount` y `cas status` por USB en la FujiNet FN32ROV-1.3-Q24 (2026-11-20)

> Estado: **la casetera indexa cintas reales de la microSD en la placa**, por USB y UART0.
> **H4 y M3a siguen sin aprobar.** El firmware no configura DATA IN ni MOTOR, y la placa estuvo **desconectada del
> Atari** durante todas las pruebas. Ninguna cinta se cargó en el Atari.

**Convención:**
- **[H]** hecho comprobado en el equipo, en la campaña del 2026-11-20 (hora local; el CI registra 2026-11-21 UTC);
- **[V]** verificado además en el PC;
- **[P]** pendiente.

## 1. Objetivo

Comprobar en hardware real que el firmware de `main` (`f405106`), empaquetado por CI en la variante
`cassette-lectura`, arranca sin configurar los pines del SIO y que `cas mount`, `cas status`, `cas rewind` y
`cas eject` funcionan con una cinta estándar y una turbo FSK reales. Procedimiento:
`docs/hardware/procedimiento-instalacion-recuperacion.md` §7.

## 2. Entorno

| Elemento | Valor |
|---|---|
| Placa | FujiNet Atari **FN32ROV-1.3-Q24**, ESP32-WROVER-B (`docs/hardware/fujinet-atari-v1x.md`) |
| Procesador | ESP32-D0WD, revisión 1.0; flash `c8`/`4018` de 16 MB |
| Firmware anterior | `15b153f` (H4 `diag`), leído por COM3 en solo lectura |
| Firmware instalado | `f40510642090843de6b7f83a1bb07c5521df278a`; el ESP32 reporta `App version: f405106` |
| Origen del binario | Job `firmware-package-cassette-lectura`, ejecución `44855110614` de GitHub Actions (PR #69) |
| Artefacto | `retrobridge-fw-cassette-lectura-fujinet-atari-v1x-f405106` |
| SHA-256 del zip | `c0d3f6f08c1b933a7d43962596a7ed621fa698454112d618a221fae0b284ad22` |
| SHA-256 del ELF | `8b72000b05f48d40bd6f3fd3b531036d84eead9bc3b308df2811b593126bb608` |
| Herramienta | esptool.py v4.5.1 |
| Consola | UART0, COM3, 115 200 baudios |
| Conexiones | **Solo USB y microSD.** Atari desconectado y SIO sin conectar |
| Recuperación | Respaldo D7 del firmware anterior (R0) y D5 (R1, FujiNet WSS). Se guardan de forma privada y **no se versionan** |

## 3. Metodología y resultados

1. **Paquete** [V]: el SHA-256 del zip coincide con el `artifact-digest` del CI; `sha256sum -c SHA256SUMS` da OK;
   `firmware_package.py verify` (de `main` `3822587`) da todas las comprobaciones OK, entre ellas «puertas H4 y M3a
   sin aprobar (perfil)» y «(registro)», «GPIO/UART del SIO solo en rb_esp_cassette (código)» y «casetera
   incorporada» en la aplicación y en el ELF.
2. **Identificación** [H]: `flash_id` dio ESP32-D0WD v1.0, `c8`/`4018` y 16 MB. La tabla de particiones leída era la
   de RetroBridge (nvs 0x9000, phy_init 0xF000, factory 0x10000 de 4 MiB).
3. **Respaldo D7** [H][V]: dos volcados completos de 16 MB (`read_flash 0x0 0x1000000`), con el mismo SHA-256. El
   descriptor de la aplicación en 0x10000 del volcado es `retrobridge` `15b153f`.
4. **Escritura** [H]: 0x1000 `bootloader.bin` (26 176 B), 0x8000 `partition-table.bin` (3 072 B) y 0x10000
   `retrobridge.bin` (404 000 B), en DIO a 40 MHz. «Hash of data verified» en las tres, y `verify_flash` dio
   «verify OK (digest matched)» en las tres. Sin `erase_flash` y sin escribir NVS.
5. **Arranque** [H]:
   - `App version: f405106`; PSRAM de 8 MB detectada (4 MB mapeados, como en H4);
   - perfil `fujinet-atari-v1.3/1.5`; microSD montada;
   - `cassette ready: MOTOR not read (M3a not signed), DATA IN not driven (H4 or M3a not signed)`.
6. **Casetera** [H] (cintas de la microSD, copias sin modificar de las de referencia):

   | Comando | Respuesta |
   |---|---|
   | `cas status` (sin cinta) | `no tape, motor off` |
   | `cas mount international_karate.cas` | `mounted international_karate.cas: 106 data, 107 fsk` |
   | `cas status` | `international_karate.cas: segment 1/213, gap, motor off` |
   | `cas rewind` | `rewound international_karate.cas` |
   | `cas eject` | `ejected international_karate.cas` |
   | `cas mount NTSC_ALIEN_AMBUSH_TURBO_SOFTWARE.cas` | `mounted NTSC_ALIEN_AMBUSH_TURBO_SOFTWARE.cas: 51 data, 51 fsk` |
   | `cas status` | `NTSC_ALIEN_AMBUSH_TURBO_SOFTWARE.cas: segment 1/102, gap, motor off` |
   | `cas eject`, luego `cas status` | `ejected …`, luego `no tape, motor off` |

   Cada respuesta añade `(MOTOR not read: M3a not signed; DATA IN not driven: H4)`.

## 4. Observaciones

- **O1 (repetida):** tras el reinicio por RTS aparece `rst:0x10 (RTCWDT_RTC_RESET)` después de `POWERON_RESET`, como
  en H4. Sigue pendiente su causa [P].
- **O2:** `international_karate.cas` dio 106 bloques `data` y 107 `fsk `. Es el **resultado esperado**: la spec
  `cassette-cas` ya indica que esta cinta estándar tiene 107 `fsk ` y 106 `data` [V].
- **O3:** el mensaje de arranque todavía dice «diagnostic firmware (H3b)». Es texto antiguo y no afecta al
  funcionamiento.

## 5. Conclusiones

1. El paquete `cassette-lectura` `f405106` arranca en la FN32ROV-1.3-Q24 y no configura DATA IN ni MOTOR, como
   exige su variante.
2. `cas mount`, `cas status`, `cas rewind` y `cas eject` funcionan por USB con una cinta estándar y una turbo FSK
   reales (criterio AC8 de `cassette-cas`, en la consola).
3. **AC9 sigue pendiente** [P]: pide que `cas status` muestre MOTOR con el Atari encendido, y en esta prueba la
   lectura de MOTOR estaba deshabilitada (M3a sin aprobar) y el Atari desconectado.
4. **No demuestra** ninguna carga en el Atari: AC10–AC12 siguen pendientes de H4, M3a y la adaptación de MOTOR [P].
