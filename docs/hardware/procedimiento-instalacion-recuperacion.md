# Procedimiento de instalación y recuperación (H3c): FujiNet Atari ESP32

> **NO EJECUTAR sin autorización explícita para cada paso** que use el puerto serie o esptool.
>
> Preparado el 2026-10-23 sin acceder al hardware: no se abrió COM3, no se ejecutó esptool y no se tocaron ni la
> flash ni los eFuses. Placa: `docs/hardware/fujinet-atari-v1x.md`, revisión **FN32ROV-1.3-Q24** con módulo
> ESP32-WROVER-B (confirmada por fotografías el 2026-10-23).
>
> **Nunca:**
> - modificar eFuses;
> - ejecutar `erase_flash`, porque no es necesario: una escritura completa reemplaza todo;
> - conectar el Atari;
> - copiar volcados o contenido de NVS al repositorio, porque pueden contener credenciales Wi-Fi.

## 1. Respaldos verificados (2026-10-23, solo lectura)

Se recalculó el SHA-256 de cada archivo y se comparó con su manifiesto. Los respaldos están en dos carpetas locales
privadas con copias idénticas (`<RESPALDOS>`); no se versionan ni se publican sus rutas.

| Archivo | Tamaño | SHA-256 | Manifiesto |
|---|---|---|---|
| `FujiNet-G5-2026-10-17/D3.bin` (ambas copias) | 16 777 216 B (16 MiB) | `512c341ad8be8d93b80175ad652b36d98199a7afcebf30ee19c9b54a1582b939` | Coincide (`MANIFEST.sha256`) |
| `FujiNet-G5-2026-10-17/D4.bin` (ambas copias) | 16 777 216 B | `512c341a…b939` (= D3: dos lecturas idénticas) | Coincide |
| `2026-10-12/D1.bin` | 16 777 216 B | `c8c378a21640f658bd5d1d9be6fdc11e0d8f59cc2f1a7874145671a1d5db35db` | Coincide |
| `2026-10-12/D2.bin` | 16 777 216 B | `c8c378a2…35db` (= D1, tras un ciclo de energía) | Coincide |
| Imagen WSS `fujinet-atari-v1_p2p3a3b_03d69475.bin` | 3 327 424 B | `e2b21c5c49e68d2afd95eb713aaa605f660580e9593b803fa30529c40da7bcdc` | Coincide (`SHA256SUMS`) |
| Tabla de particiones WSS (`…partitions.bin`) | — | `d8649a01…` | Coincide; idéntica a la instalada según `provenance.txt` |

### Hallazgo: ningún volcado completo refleja el estado actual

1. **D3/D4** se tomaron en G5-0, el 2026-10-17, **antes** de instalar el firmware WSS. Contienen la imagen anterior
   (`ba8accf0`, P4.2).
2. En **G5-1** se escribió solo la región `0x10000–0x33CFFF` con la imagen WSS `03d69475`; `verify_flash` dio OK. Las
   pruebas WSS (G5-2 a G5-6) corrieron con esa imagen.
3. En **G5-6**, FujiNet guardó configuración en la flash ("Saving config to FLASH"): la partición LittleFS
   `storage`, y posiblemente NVS, cambiaron después de D3.

**Consecuencia:** para volver *exactamente* al estado actual hace falta un volcado nuevo. **El respaldo D5/D6 es
OBLIGATORIO antes de cualquier instalación física** (decisión del equipo, 2026-10-23; paso 3.2).

## 2. Precondiciones

1. Revisión de la placa confirmada por la serigrafía y coincidente con el perfil `fujinet-atari-v1.3/1.5`.
   **Cumplida** (2026-10-23): FN32ROV-1.3-Q24. Antes de instalar, comprobar que la placa conectada es la misma unidad
   fotografiada. La 1.3 no tiene pull-ups externos en la microSD; si `sd mount` o las lecturas fallan de forma
   intermitente, registrarlo como hallazgo (ver el informe de hardware, §3) y no aplicar mitigaciones sin aprobación.
2. Integridad de los respaldos comprobada otra vez inmediatamente antes de empezar (ver §1):
   ```bash
   sha256sum -c MANIFEST.sha256
   ```
3. microSD **dedicada**, en FAT32, con las imágenes de `tools/make_atr.py`. No usar la tarjeta de FujiNet.
4. Alimentación **solo por USB**. **Atari desconectado.** Nada conectado al conector SIO.
5. **Respaldo D5/D6 del firmware WSS tomado y verificado (paso 3.2). Es obligatorio:** sin él no se instala nada.
6. Firmware compilado con el perfil de la placa, y su `idf.py size` revisado. La vía prevista es el artefacto del
   job `firmware-package` del CI, verificado con `sha256sum -c SHA256SUMS` y
   `tools/ci/firmware_package.py verify` (`docs/toolchain.md`). En local sería:
   ```bash
   idf.py -B build-fujinet -D SDKCONFIG=build-fujinet/sdkconfig \
          -D SDKCONFIG_DEFAULTS="sdkconfig.defaults;sdkconfig.board.fujinet-atari-v1x" build
   ```

## 3. Pasos (cada uno requiere autorización)

### 3.1 Identificación (solo lectura)

```bash
esptool.py --chip esp32 -p COM3 flash_id
```

Debe mostrar ESP32-D0WD v1.0, fabricante `c8`, dispositivo `4018` y 16 MB. Si algo difiere, se aborta.

### 3.2 Volcado del estado actual, firmware WSS instalado (solo lectura, OBLIGATORIO)

```bash
esptool.py --chip esp32 -p COM3 -b 460800 read_flash 0x0 0x1000000 D5.bin
esptool.py --chip esp32 -p COM3 -b 460800 read_flash 0x0 0x1000000 D6.bin
```

- Criterio: `sha256(D5) == sha256(D6)`.
- Registrar los hashes en un manifiesto local privado.
- La región `0x10000` debe coincidir con la imagen WSS (`e2b21c5c…`) en sus primeros 3 327 424 bytes.
- Si D5 y D6 no coinciden, o la región WSS no corresponde, **se aborta la instalación**.

### 3.3 Instalación de RetroBridge (escritura)

```bash
idf.py -B build-fujinet -D SDKCONFIG=build-fujinet/sdkconfig -p COM3 flash
```

| Región | Qué escribe |
|---|---|
| `0x1000` | Bootloader de RetroBridge (reemplaza el de FujiNet) |
| `0x8000` | Tabla `partitions/retrobridge-16mb.csv`: nvs 0x9000, phy 0xF000, factory 0x10000 de 4 MiB |
| `0x10000` | Aplicación de diagnóstico (reemplaza el comienzo de la aplicación FujiNet) |

- **NVS (`0x9000`):** no se escribe. Los datos de FujiNet quedan físicamente ahí, y el firmware de diagnóstico H3
  **no inicializa NVS**, así que no los lee ni los modifica.
- **Resto de la flash** (antigua `update` y LittleFS de FujiNet): no se toca.

### 3.4 Validación en consola (sin SIO)

Ejecutar `board`, `crctest`, `sd mount`, `sd ls`, `atr info`, `atr crc <imagen>` (y luego
`tools/compare_crc.py manifest.csv log.txt`), `atr bench <imagen> seq` y `rand`, y `mem` antes y después.

Registrar los resultados en `docs/mediciones/` con fecha, commit, tarjeta, interfaz y frecuencia.

## 4. Recuperación

Para un volcado crudo, esptool no debe reescribir las cabeceras de flash; por eso se usan los parámetros `keep`.

| Opción | Cuándo | Comandos | Resultado |
|---|---|---|---|
| **R1 (la normal)** | Siempre, porque D5 es obligatorio (paso 3.2) | `esptool.py --chip esp32 -p COM3 -b 460800 write_flash --flash_mode keep --flash_freq keep --flash_size keep 0x0 D5.bin`, luego `verify_flash 0x0 D5.bin` | Estado WSS exacto, incluidas la configuración y la LittleFS actuales |
| **R2 (contingencia)** | Solo si D5 se perdiera o dañara después de la instalación | `write_flash … keep 0x0 D3.bin` + `verify_flash 0x0 D3.bin`; después `write_flash … keep 0x10000 fujinet-atari-v1_p2p3a3b_03d69475.bin` + `verify_flash 0x10000 <la misma imagen>` | Programa WSS con bootloader, tabla, NVS y LittleFS de G5-0. **Se pierden** los cambios de configuración posteriores a G5-0 (G5-6); hay copias de `fnconfig.ini` en `g5-6/`. |
| **R3** | Volver al estado de G5-0 | `write_flash … keep 0x0 D3.bin` + `verify_flash` | Firmware anterior `ba8accf0` (P4.2), no la versión WSS |

- **Verificación posterior:** arranque por consola con la versión esperada en el registro; montaje de ATR desde la
  tarjeta de FujiNet.
- **Si `verify_flash` falla:** repetir la escritura. No continuar ni conectar el Atari hasta que verifique.

## 5. Criterios para abortar

- La identidad del chip o de la flash difiere de §3.1.
- Los respaldos no verifican.
- El volcado D5/D6 no es reproducible.
- `verify_flash` falla.
- La revisión de la placa no coincide con el perfil.
- Cualquier síntoma eléctrico (calor, consumo anómalo).

## 6. Firmware H4 con diagnóstico autónomo: primera validación física

> **Ejecutada** en dos campañas, solo por USB y sin el Atari:
> - pasos 1–3 (instalación y diagnóstico) el 2026-10-24: `docs/mediciones/h4-validacion-diagnostico-2026-10-24.md`;
> - paso 4 (**regresión de H3c → H4**: cinco geometrías, 4 640/4 640 CRC32, unidad D1 y benchmarks) el 2026-10-25,
>   sin volver a flashear: `docs/mediciones/h4-regresion-h3c-2026-10-25.md`.
>
> El paso 5 (reinicio controlado) se hizo el 2026-10-24 con un ciclo de USB (`PowerOn`). Pendiente: investigar la
> causa del `OtherWatchdog` de arranque (observación O1), capturando la línea `rst:` de la ROM. Cada repetición sigue
> requiriendo autorización explícita.

Paquete `retrobridge-fw-diag-fujinet-atari-v1x-15b153f` (job `firmware-package-h4`, commit `15b153f`). Cada paso
requiere una **autorización explícita**. La FujiNet se conecta **solo por USB**: **sin el Atari** y sin nada en el
SIO, porque H4 eléctrico sigue EN PREPARACIÓN.

**Precondiciones:**
1. Respaldo D5/D6 verificado (`MANIFEST.sha256` en D: y E:), que sigue siendo la recuperación R1 hacia el firmware
   WSS.
2. Copia local verificada del paquete H3c `retrobridge-fw-fujinet-atari-v1x-1b1527d`, para volver al firmware
   validado en H3c. El artefacto de GitHub caduca a los 90 días.
3. Paquete H4 descargado, con estas verificaciones:
   - el SHA-256 del zip coincide con el `artifact-digest` del resumen de la ejecución;
   - `sha256sum -c SHA256SUMS` da OK;
   - `python -I tools/ci/firmware_package.py verify <paquete>` da OK, con las comprobaciones "diagnóstico
     incorporado" y "versión = commit fijado";
   - `PROVENANCE.json` indica `variant: diag` y el commit `15b153f267cf6766978bd786eb1b463ea0e4ac50`.
4. La misma microSD de pruebas de H3c, sin modificar.

**Pasos:**
1. **Identificación (lectura):** `esptool.py --chip esp32 -p COM3 flash_id` debe dar ESP32-D0WD v1.0, `c8`/`4018`
   y 16 MB.
2. **Escritura:** las tres imágenes de `flash/flash_args.paquete` (0x1000, 0x8000 y 0x10000), con esptool.py v4.5.1
   y DIO a 40 MHz, y después `verify_flash`. No se usa `erase_flash`.
3. **Diagnóstico** (consola UART0, 115 200 baudios):

   | Comando | Esperado |
   |---|---|
   | `diag status` | `version=15b153f`, `board=fujinet-atari-v1.3/1.5`, motivo de reinicio coherente con el arranque y watchdog según el `sdkconfig` del paquete |
   | `diag memory` | Cifras de heap; anotar el valor de referencia |
   | `diag storage` | `mounted=yes`, sin fallos de montaje |
   | `diag events` | `Boot` y `StorageMounted` |
   | `diag report` | Informe completo. Se captura y se verifica con `python tools/check_diag_report.py <captura>` |

4. **Regresión de H3c** (las funciones de microSD y ATR siguen operativas):
   - `sd ls`;
   - `atr info` de las cinco imágenes;
   - `atr crc` de las cinco imágenes y `tools/compare_crc.py manifest.csv <registro>`, que debe dar **4 640/4 640**;
   - `drive mount dd-720.atr`, `drive read 1`, `4` y `721` (este último con el error esperado `SectorOutOfRange`) y
     `drive unmount`;
   - `atr bench` en modos `seq` y `rand`.

   Después, `diag storage` y `diag events` deben reflejar esas operaciones, incluido el error del sector 721, y
   `diag report` debe volver a verificar.
5. **Historial de reinicios:** un reinicio controlado (botón o auto-reset del monitor) y comprobar que `diag status`
   lo refleja.
6. **Registro:** resultados en `docs/mediciones/`, sin MAC ni datos privados.

**Volver atrás:** reinstalar el paquete H3c verificado, o R1 con D5 para volver a WSS (§4).
