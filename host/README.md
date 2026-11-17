# retrobridge_host

Ejecutable de PC que sirve una ATR como unidad D1 a un Atari emulado por NetSIO (Issue #32, E1b; ADR-016).
Reutiliza `rb_netsio`, `SioSession`, el despachador SIO y el dominio de RetroBridge. Es una unidad de solo lectura.
No es firmware: no ejerce UART, GPIO ni temporización SIO real; los tiempos son del PC.

## Compilar (Windows, MSYS2 ucrt64)

```bash
export PATH="/c/msys64/ucrt64/bin:$PATH"
cmake -S host -B build/retrobridge_host -G Ninja
cmake --build build/retrobridge_host
```

## Usar con Altirra (ruta B)

1. En Altirra: *System > Configure System > Devices*, agregar el dispositivo personalizado `netsio.atdevice`.
   Dejar D1 vacía y no ejecutar FujiNet-PC.
2. Iniciar el hub NetSIO (herramienta externa de laboratorio; no forma parte de RetroBridge):
   `python -u -m netsiohub`. Escucha TCP 9996 (Altirra) y UDP 9997 (dispositivos).
3. Iniciar RetroBridge:

   ```bash
   retrobridge_host --image D:/ruta/disco.atr --peer netsiohub --log e1b.jsonl
   ```

4. Reiniciar en frío Altirra (Shift+F5). El Atari pide STATUS a D1 y lee los sectores de la ATR.

Opciones: `--port` (UDP del hub, 9997), `--peer netsiohub|fep003` (perfil explícito; por defecto FEP-003 estricto),
`--log` (JSONL con cada datagrama, cada transacción y un resumen) y `--seconds N` (0 = sin límite).

El log se abre para agregar. Si `--log` nombra el mismo archivo que `--image`, aunque se escriba con otra ruta,
otra capitalización, un nombre corto 8.3 o un enlace duro, el programa termina con código 2 sin abrir nada para
escritura. La comparación usa la identidad del archivo en disco, no el texto de la ruta.

## Arrancar la ATR de prueba de RetroBridge (E1c)

`tools/make_atr.py` genera `rb-boot.atr`. Es una ATR de arranque con un programa 6502 propio. El Atari carga los
sectores 1 a 3, el programa comprueba los sectores 2 y 3 y muestra `RETROBRIDGE D1 OK` (o `RETROBRIDGE D1 ERR` si
los datos no coinciden).

1. Generar las imágenes y su manifiesto CRC32 (fuera del repositorio o en `build/`, que git ignora):

   ```bash
   python -I tools/make_atr.py --out build/test-images
   ```

2. Preparar Altirra y el hub como en la sección anterior.
3. Servir la ATR. Usar un log nuevo por corrida, porque `--log` agrega al final:

   ```bash
   retrobridge_host --image build/test-images/rb-boot.atr --peer netsiohub --log e1c.jsonl
   ```

4. Reiniciar en frío Altirra (Shift+F5). En pocos segundos aparece `RETROBRIDGE D1 OK` en la pantalla.
   Repetir el reinicio para comprobar que vuelve a arrancar.
5. Detener `retrobridge_host` y comprobar el CRC32 de cada sector enviado contra el manifiesto:

   ```bash
   python -I tools/compare_crc.py build/test-images/manifest.csv e1c.jsonl
   ```

   Sale con 0 solo si cada READ se respondió con COMPLETE, un checksum SIO válido y el CRC32 del manifiesto.

El socket se enlaza a `127.0.0.1` y solo acepta datagramas del puerto del hub. El hub, en cambio, escucha en
`0.0.0.0:9997`: limitarlo con el firewall del laboratorio.
