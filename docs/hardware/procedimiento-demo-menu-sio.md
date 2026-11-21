# Demo E1: menú de RetroBridge en el 65XE por el SIO físico (paquete `menu-sio`)

> **Estado: preparado, no ejecutado.** Cada paso marcado con **[CONFIRMAR]** requiere la confirmación explícita del
> responsable: construir el adaptador, flashear, conectar el SIO y encender el 65XE.
> **H4 y M3a siguen sin aprobar.** Esta demo no aprueba ninguna puerta. Se hace solo con la aceptación de riesgo
> firmada (`docs/mediciones/menu-sio-fisico-registro.md` §1) y con el adaptador sin el pin 8.

**Objetivo:** demostrar en el 65XE real los criterios 1–4 de E1 (`02-DOCS/wiki/sdd/specs/menu-sio-fisico.md`):
1. al encender el 65XE aparece el menú;
2. la navegación muestra las mismas pantallas que Altirra;
3. **A** arranca una ATR de la microSD hasta ver `DEMO DOS OK`;
4. el botón A de la FujiNet o la orden `menu` devuelven el menú tras apagar y encender el 65XE.

**Fuera del alcance:**
- las mediciones eléctricas de H4 (niveles de DATA IN, reposo de DATA OUT, corriente del pin 10);
- E2 (TNFS);
- la casetera, que en `menu-sio` no arranca.

## 1. Qué se instala

| Dato | Valor |
|---|---|
| Paquete | `retrobridge-fw-menu-sio-fujinet-atari-v1x-e65c2bd` (PR #74, ejecución de CI 44941501900) |
| SHA-256 del zip | `b1b7455da60583d7f0387b314a6d56c34d1262fc0dab1eee61af88c66ecaa902` |
| SHA-256 de `firmware/retrobridge.bin` | `5794655f19cdba1c53bcfa62daccdd9b0d5389b19b18c43dd9d064c909aa4928` |
| SHA-256 del ELF | `bdea655d6b843d4e2d58dd4e37fdafedfd57bd9d6cb213ae0c35f9afffd83158` |
| Versión (esp_app_desc) | `e65c2bd`; aplicación de 432208 B |
| Línea SIO | UART2 19200 8N1: TX en DATA IN (GPIO21), RX en DATA OUT (GPIO33), COMMAND en GPIO39 (solo entrada) |
| Otras entradas | botón A (GPIO0, solo entrada, leído tras el arranque); +5 V del SIO por GPIO35 (ADC) |

El firmware lee al arrancar el catálogo de la **raíz** de la microSD y sirve el menú como D1. La casetera no arranca:
MOTOR (GPIO36) no se configura.

## 2. Condiciones (obligatorias)

- **Aceptación de riesgo firmada** por el responsable en `docs/mediciones/menu-sio-fisico-registro.md` §1.
- **Adaptador SIO sin el pin 8** (`mitigacion-c-motor-pin8.md`):
  - contacto 8 retirado físicamente;
  - foto y rótulo «SIN PIN 8 (MOTOR) — SOLO FUJINET AL FINAL — J2 LIBRE».
  Sin multímetro, el adaptador queda como riesgo aceptado, no como medición (spec, «Evidencia para la demo física»).
  Si hay multímetro, se registra §5.2 de la mitigación.
- **Topología:** 65XE → adaptador → J1 de la FujiNet. La FujiNet va al final de la cadena y **J2 queda libre**. No hay
  otros equipos SIO.
- **Alimentación:** la FujiNet, por USB al PC (COM3). El 65XE, por su fuente.
- **Vuelta atrás disponible:** el paquete `cassette-lectura` `f405106` verificado en
  `D:\GitHub\Respaldos\FujiNet-RB-15b153f-D7\retrobridge-fw-cassette-lectura-fujinet-atari-v1x-f405106\`.
- **Herramienta:** el mismo entorno de esptool de la prueba C8:

  ```powershell
  $py = "C:\Users\calal\AppData\Local\Temp\claude\6ceb9387d1847330\564fcfbb-bd9a-4a82-8fe3-20459514add9\scratchpad\venv-esptool\Scripts\python.exe"
  ```

  Si ese entorno ya no existe, se crea uno nuevo con esptool v4.5.1 y se anota en el registro.

## 3. Preparación (sin escribir en la placa ni conectar el Atari)

1. **Paquete.**
   - Descargar el artefacto de la ejecución 44941501900.
   - Guardarlo en `D:\GitHub\Respaldos\FujiNet-RB-menu-sio\` y descomprimirlo allí.
   - Comprobar el zip:

     ```powershell
     (Get-FileHash D:\GitHub\Respaldos\FujiNet-RB-menu-sio\retrobridge-fw-menu-sio-fujinet-atari-v1x-e65c2bd.zip -Algorithm SHA256).Hash
     ```

     Debe dar `B1B7455DA60583D7F0387B314A6D56C34D1262FC0DAB1EEE61AF88C66ECAA902`.
   - En la carpeta del paquete, ejecutar `verify`:

     ```powershell
     & $py -I D:\GitHub\retrobridge-chile\tools\ci\firmware_package.py verify .
     ```

     Debe terminar con «RESULTADO: todas las comprobaciones OK». Entre ellas debe estar «paquete de prueba menu-sio
     (sdkconfig, aplicación y ELF)».
2. **Imágenes de prueba.** Son las mismas de la regresión en Altirra (`D:\rb-e0\m50-sio-fisico\images`):

   ```powershell
   python -I D:\GitHub\retrobridge-chile\tools\make_atr.py --out D:\GitHub\Respaldos\FujiNet-RB-menu-sio\atr --demo
   ```

   `demo-dos.atr` debe medir 92176 B y su SHA-256 debe ser
   `aa96d7ddceb750c0fefb77be8c0683c17247643fb3e6d6981fef7aea22c4d1f1`. Comprobado el 2026-11-21: `make_atr.py --demo`
   genera la misma imagen que usó Altirra.
3. **microSD.**
   - Copiar en la **raíz** `demo-dos.atr`, `demo-uno.atr` y `rota.atr`, y nada más.
   - Así la lista coincide con la de Altirra y la secuencia de teclas es la misma.
   - Si es la microSD de la casetera, antes se mueve a una carpeta todo lo que haya en su raíz. Se anota qué se movió,
     para devolverlo en §8.
   - Si hay otra microSD, se prefiere usar esa.
4. **[CONFIRMAR] Adaptador.**
   - Construirlo según §2.
   - Fotografiarlo con el rótulo.
   - Guardar la foto en `D:\GitHub\Respaldos\FujiNet-RB-menu-sio\`.

## 4. Instalación (FujiNet solo por USB, SIO desconectado)

**[CONFIRMAR] Flashear.**
- Se ejecuta desde la carpeta del paquete `menu-sio`, con la misma disposición de flash que `cassette-lectura`.
- No se usa `erase_flash`. No se escribe la NVS.

```powershell
& $py -m esptool --chip esp32 --port COM3 --baud 460800 write_flash --flash_mode dio --flash_freq 40m --flash_size 16MB 0x1000 firmware\bootloader.bin 0x8000 firmware\partition-table.bin 0x10000 firmware\retrobridge.bin
```

```powershell
& $py -m esptool --chip esp32 --port COM3 --baud 460800 verify_flash --flash_mode dio --flash_freq 40m --flash_size 16MB 0x1000 firmware\bootloader.bin 0x8000 firmware\partition-table.bin 0x10000 firmware\retrobridge.bin
```

Pasa si las tres regiones dan «Hash of data verified» y «verify OK (digest matched)».

## 5. Banco por USB (sin el Atari)

Este paso comprueba el arranque. También comprueba que la captura de consola **no reinicia** la placa, porque la
demo depende de ello.

1. Abrir la consola sin tocar RTS ni DTR. La salida queda también en un archivo:

   ```powershell
   & $py -m serial.tools.miniterm COM3 115200 --rts 0 --dtr 0 --raw 2>&1 | Tee-Object -FilePath D:\GitHub\Respaldos\FujiNet-RB-menu-sio\consola-banco.txt
   ```

   **Pendiente de comprobar:** que este comando no reinicie la placa y que el archivo reciba la salida. Si falla,
   se usa cualquier monitor serie con registro a archivo, sin reinicio al abrir, y se anota cuál.
2. Reiniciar la placa con su botón de reset (EN). En el arranque deben aparecer, en este orden:

   ```text
   W (…) rb_menu_sio: menu-sio TEST PACKAGE: transmits on SIO DATA IN. H4 and M3a NOT approved. Use only with the owner's written risk acceptance and the SIO adapter without pin 8.
   I (…) rb_menu_sio: boot: SIO +5V (pin 10) 0 mV (GPIO35 … mV): no power from the Atari
   I (…) rb_menu_sio: catalog: 3 image(s) on the microSD root
   I (…) rb_menu_sio: idle levels before the UART: COMMAND …, DATA OUT …
   I (…) rb_sio_line: SIO line: UART2 19200 TX GPIO21 RX GPIO33, COMMAND GPIO39
   I (…) rb_menu_sio: D1: menu. Switch the 65XE on.
   ```

   Las marcas de tiempo varían.
3. Escribir `sio status`. Debe mostrar:
   - `D1: menu`;
   - los niveles de COMMAND y DATA OUT;
   - contadores en 0;
   - `SIO +5V (pin 10): 0 mV (GPIO35 … mV) sin alimentacion del Atari`.
4. Pulsar el **botón A** de la FujiNet.
   - Como D1 ya es el menú, puede no aparecer ninguna línea; no es un fallo.
   - `sio status` debe seguir mostrando `D1: menu`.

**Detener aquí** si:
- falta alguna línea;
- aparece `board profile has no SIO pins`, `no memory`, `SIO task not created` o un reinicio;
- `catalog` no da 3.

Si se detiene, se guarda la captura y se restaura `f405106` (§8).

Sin el Atari, COMMAND y DATA OUT pueden leerse en cualquier nivel. Se anotan, pero no deciden el resultado.

## 6. Conexión y demo en el 65XE

1. Apagar el 65XE. Dejar la FujiNet por USB, con la consola de §5 abierta.
2. **[CONFIRMAR] Conectar** el cable SIO del 65XE al adaptador y el adaptador a J1. J2 queda libre.
3. Retirar cualquier cartucho. Para que no interfiera el BASIC, encender el 65XE con **OPTION** pulsado.
   - *Suposición:* la regresión en Altirra no registró el estado del BASIC.
   - Si con OPTION no aparece el menú, se repite sin OPTION y se anota.
4. **[CONFIRMAR] Encender el 65XE.** Fotografiar cada pantalla con el número de la captura de Altirra
   (`D:\rb-e0\m50-sio-fisico\`):

   | Paso | Tecla | Pantalla esperada | Captura de Altirra |
   |---|---|---|---|
   | 1 | (encender) | Menú principal | `1-menu.png` |
   | 2 | `1` | Lista de la fuente 1 (microSD): las tres ATR | `2-lista.png` |
   | 3 | `A` | `DEMO-DOS.ATR` elegida | `3-elegida.png` |
   | 4 | `1` | `DEMO-DOS.ATR` asignada a D1 | `4-asignada-d1.png` |
   | 5 | `A` | El 65XE arranca la ATR y muestra **`DEMO DOS OK`** | `5-arranca-a.png` |

   En la consola debe aparecer `D1: DEMO-DOS.ATR` (o el nombre que muestre el menú).
5. Escribir `sio status`. Anotar:
   - tramas;
   - errores;
   - liberaciones;
   - latencia última y máxima;
   - órdenes, reintentos y NAK del menú;
   - +5 V del SIO.
6. **Vuelta al menú (criterio 4).**
   - Pulsar el **botón A** de la FujiNet. Debe aparecer `D1 back to the menu (button A)`.
   - Apagar y encender el 65XE. Debe aparecer el menú (`6-vuelve-menu.png`).
   - Repetir con la orden `menu` en lugar del botón. Debe aparecer `D1 back to the menu (console)`.
7. Apagar el 65XE. Desconectar el SIO. Cerrar la consola con Ctrl+].

### Pasa si

- El menú aparece al encender (paso 1).
- Las pantallas 2–4 coinciden con Altirra.
- Aparece `DEMO DOS OK` (paso 5).
- El menú vuelve con el botón A y con `menu` (paso 6).
- La consola no muestra reinicios, brownouts ni errores de la línea SIO.

### No pasa si

- No aparece el menú.
- La pantalla queda en negro o en el BASIC.
- El 65XE muestra `BOOT ERROR`.
- La FujiNet se reinicia.
- `sio status` muestra errores en aumento.

En ese caso: se apaga el 65XE, se desconecta el SIO, se guarda la captura y se sigue con §8.

**Detener de inmediato** si hay olor, calor anormal en la FujiNet o en el adaptador, o si el 65XE no enciende con
el SIO conectado.

## 7. Qué no prueba esta demo

- No mide niveles eléctricos. Si el menú carga, DATA IN funciona, pero eso no aprueba H4.
- No mide la corriente del pin 10 (M6 sigue pendiente). La tensión se lee por GPIO35 y se anota.
- No prueba la casetera ni TNFS.

## 8. Restaurar `cassette-lectura` (`f405106`)

Al terminar, pase o no, con el SIO **desconectado**. Desde
`D:\GitHub\Respaldos\FujiNet-RB-15b153f-D7\retrobridge-fw-cassette-lectura-fujinet-atari-v1x-f405106\`:

**[CONFIRMAR] Flashear.**

```powershell
& $py -m esptool --chip esp32 --port COM3 --baud 460800 write_flash --flash_mode dio --flash_freq 40m --flash_size 16MB 0x1000 firmware\bootloader.bin 0x8000 firmware\partition-table.bin 0x10000 firmware\retrobridge.bin
```

```powershell
& $py -m esptool --chip esp32 --port COM3 --baud 460800 verify_flash --flash_mode dio --flash_freq 40m --flash_size 16MB 0x1000 firmware\bootloader.bin 0x8000 firmware\partition-table.bin 0x10000 firmware\retrobridge.bin
```

Al arrancar deben aparecer:
- `App version: f405106`;
- `cassette ready: MOTOR not read (M3a not signed), DATA IN not driven (H4 or M3a not signed)`.

Si se movieron archivos de la microSD en §3, se devuelven a la raíz.

Si la placa no arranca, el procedimiento general de recuperación (`procedimiento-instalacion-recuperacion.md` §7)
restaura el volcado completo D7.

**Adaptador:** mientras siga puesto, el firmware FujiNet original no tendrá casete (mitigación C §4). Se retira al
volver a ese firmware.

## 9. Registro

En `docs/mediciones/menu-sio-fisico-registro.md`:
- la aceptación de riesgo firmada (§1);
- el resultado de cada paso (§2);
- la salida de esptool, las capturas de consola, las fotos y la restauración.

Al ejecutar la demo, el registro se copia con la fecha (`menu-sio-fisico-AAAA-MM-DD.md`, spec «Cómo lo demuestra»
paso 5).
