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

El socket se enlaza a `127.0.0.1` y solo acepta datagramas del puerto del hub. El hub, en cambio, escucha en
`0.0.0.0:9997`: limitarlo con el firewall del laboratorio.
