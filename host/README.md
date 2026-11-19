# retrobridge_host

Ejecutable de PC que sirve una ATR (o un menú para elegir imágenes de D1 a D8, `--images`) como unidad D1 a un Atari emulado por NetSIO (Issue #32, E1b; ADR-016).
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

`tools/make_atr.py --boot` genera `rb-boot.atr`. Es una ATR de arranque con un programa 6502 propio. El Atari carga los
sectores 1 a 3, el programa comprueba los sectores 2 y 3 y muestra `RETROBRIDGE D1 OK` (o `RETROBRIDGE D1 ERR` si
los datos no coinciden).

1. Generar la imagen y su manifiesto CRC32 (fuera del repositorio o en `build/`, que git ignora). `--boot` escribe
   solo `rb-boot`, en su propio directorio; el juego de la Meta A (sin `--boot`) no cambia:

   ```bash
   python -I tools/make_atr.py --out build/boot-image --boot
   ```

2. Preparar Altirra y el hub como en la sección anterior.
3. Servir la ATR. Usar un log nuevo por corrida, porque `--log` agrega al final:

   ```bash
   retrobridge_host --image build/boot-image/rb-boot.atr --peer netsiohub --log e1c.jsonl
   ```

4. Reiniciar en frío Altirra (Shift+F5). En pocos segundos aparece `RETROBRIDGE D1 OK` en la pantalla.
   Repetir el reinicio para comprobar que vuelve a arrancar.
5. Detener `retrobridge_host` y comprobar el CRC32 de cada sector enviado contra el manifiesto:

   ```bash
   python -I tools/compare_crc.py build/boot-image/manifest.csv e1c.jsonl
   ```

   Sale con 0 solo si cada READ se respondió con COMPLETE, un checksum SIO válido y el CRC32 del manifiesto.

## Elegir imágenes desde el Atari: menú de RetroBridge (Issues #38 y #40, specs `menu-tnfs` y `menu-navegacion`)

Con `--images <carpeta>`, en lugar de `--image`, RetroBridge sirve su propio menú como D1 tras cada reinicio en
frío y atiende las unidades D1 a D8. La pantalla principal muestra las fuentes (1 = la carpeta, rotulada `SD`;
2 a 8 = las del archivo `--sources`) y el estado de D1 a D8 y C:. Los avisos y errores aparecen en la última
línea, debajo del pie de teclas. El protocolo está en ADR-017; el cliente TNFS, en ADR-018.

Todo se puede hacer con el cursor: las flechas (Control + `-` `=` `+` `*`) o el joystick del puerto 1 lo
mueven, y RETURN o el botón rojo confirman lo marcado. La fila marcada sale en vídeo inverso. Las letras y los
dígitos siguen valiendo. El cursor se mueve en el Atari, sin hablar con el host: solo RETURN, el botón rojo y
cada cambio de página van al host.

En la lista, el título es siempre `RETROBRIDGE CHILE`. Debajo, una línea muestra la fuente y la carpeta, por
ejemplo `FUENTE 2: /ATARI_CHILE/OMS_ATR`; una ruta larga se recorta por la izquierda con `..`.

| Tecla | Pantalla | Qué hace |
|-------|----------|----------|
| arriba, abajo | principal | mueve el cursor por las fuentes 1 a 8; RETURN o el botón rojo la abren, como su dígito |
| arriba, abajo | lista o carpeta | mueve una fila; en el borde pasa a la página siguiente o anterior |
| izquierda, derecha | lista o carpeta | cambia de página (20 filas) y deja el cursor en la primera fila |
| arriba, abajo | elegir unidad | mueve el cursor por D1 a D8; RETURN o el botón rojo eligen la marcada |
| `1` | principal | abre la fuente local: la lista de `.atr` de la carpeta (como máximo 999, en páginas de 20) |
| `A`..`T` | lista | elige una imagen de la página visible; después se elige la unidad (`1`..`8` o el cursor), Esc vuelve a la lista |
| `A` | principal | arranca la imagen asignada a D1 |
| `U` | principal | pide una unidad (`1`..`8`) y la desmonta; Esc cancela |
| `2`..`8` | principal | abre la raíz del servidor TNFS de esa fuente, de solo lectura (`FUENTE n SIN CONFIGURAR` si no hay) |
| `A`..`T` | carpeta TNFS | entra en una carpeta (`/`) o elige una `.ATR` de la página visible; después se elige la unidad |
| Esc | carpeta TNFS | vuelve a la carpeta anterior, en la página y fila desde donde se entró; en la raíz, a la principal; durante «CARGANDO nn%», cancela |
| `E` | principal | pendiente: muestra `PENDIENTE: E NECESITA ENTRADA DE TEXTO` |

Con más de 20 entradas, debajo del pie aparece `PAGINA n DE m (t)`; una carpeta guarda como máximo 999 y entonces
muestra `(999 DE t)`. Durante «CARGANDO nn%», moverse y RETURN no hacen nada.

Una imagen elegida para D1 se valida y queda asignada; D1 sigue sirviendo el menú hasta pulsar `A`. Una imagen
elegida para D2..D8 se monta enseguida. Una ATR inválida se rechaza al elegirla, con el motivo en pantalla, y la
unidad conserva lo que tenía. Shift+F5 vuelve al menú y conserva las unidades; F5 conserva la imagen de D1.

Una `.ATR` de un servidor TNFS se trae entera a la memoria del PC (`CARGANDO nn%`) y después se valida y se
monta como una local. Si el servidor no contesta o el nombre no se resuelve, aparece `ERROR: SERVIDOR NO
RESPONDE` o `ERROR: SERVIDOR NO ENCONTRADO` y las unidades no cambian. Cada línea de `--sources` es un nombre o una
IPv4; el puerto es el 16384. Al cerrar, el host termina cada sesión TNFS con UMOUNT.

Pendiente (no implementado): editar un servidor con `E` (falta la entrada de texto en el Atari y escribir `--sources`), casete (sin soporte) y
escritura en disco (unidades de solo lectura).

1. Generar las imágenes de demostración (dos arrancables con mensajes distintos y una inválida):

   ```bash
   python -I tools/make_atr.py --out build/menu-demo --demo
   ```

2. Preparar Altirra y el hub como en «Usar con Altirra».
3. Opcional: un archivo de fuentes, una por línea (como máximo 7, hasta 32 caracteres ASCII; `#` comenta):

   ```text
   tnfs.ejemplo.cl
   localhost
   ```

4. Servir la carpeta:

   ```bash
   retrobridge_host --images build/menu-demo --sources fuentes.txt --peer netsiohub --log menu.jsonl
   ```

5. Reiniciar en frío Altirra (Shift+F5). Aparece la pantalla principal. Recorrido de ejemplo: `1`, `A`, `1`
   (DEMO-DOS.ATR asignada a D1), `A` (arranca y muestra `DEMO DOS OK`), Shift+F5 (vuelve con D1 en pantalla).

El log registra el catálogo (`menu_entry`, numerado desde 1), las fuentes (`menu_source`), cada elección (`menu_select`), cada cambio
aplicado a una unidad (`menu_mount`), cada desmontaje (`menu_unmount`), cada arranque (`menu_boot`), las teclas
pendientes (`menu_pending`), cada vuelta al menú (`menu_return`), cada carpeta TNFS abierta (`menu_open`), cada
imagen traída con su SHA-256 (`menu_fetch`), cada error TNFS con su causa (`menu_tnfs`) y cada orden enviada al
servidor, con los indicadores de cada OPEN (`tnfs_request`). `--log` no puede nombrar ninguna imagen de la
carpeta ni el archivo de fuentes. Una carpeta sin `.atr` o un archivo de fuentes inválido termina con código 2.

El programa 6502 del menú se genera con `python -I tools/make_menu.py --out host/src/menu_program.inc`; las
pruebas de `tools/` fallan si el archivo versionado no coincide.

El socket se enlaza a `127.0.0.1` y solo acepta datagramas del puerto del hub. El hub, en cambio, escucha en
`0.0.0.0:9997`: limitarlo con el firewall del laboratorio.
