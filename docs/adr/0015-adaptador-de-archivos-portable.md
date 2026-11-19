# ADR-015 — Adaptador de archivos portable sobre stdio y la VFS de ESP-IDF

- Estado: Aprobada (2026-10-23), decisiones H3-1 a H3-4
- Contexto: arquitectura v0.2, §5.2; hito H3

## Contexto

H3 debe leer imágenes ATR de una microSD FAT32 en el ESP32 reutilizando los puertos `ImageRepository` e
`ImageByteSource`. Se evaluaron tres vías para acceder a los archivos:

| Vía | Ventaja | Inconveniente |
|---|---|---|
| API nativa de FatFs (`f_open`/`f_read`) | Sin capa intermedia | No se puede probar en el host |
| POSIX `pread` sobre la VFS | Lectura posicional | No existe en MinGW (host Windows); `vfs_fat_pread` toma igualmente el lock del volumen |
| **stdio de C** (`fopen`/`fseek`/`fread`) | Mismo código en el host (archivos reales) y en el ESP32 (VFS → FATFS) | Una capa más (la VFS), de costo bajo |

## Decisión

1. **`rb_file_storage`**, un componente de infraestructura portable, sin cabeceras de ESP-IDF:
   - `StdioImageRepository<Slots>`, con slots fijos;
   - `StdioByteSource`, un `FILE*` por slot.

   `rb_esp_storage` (H3b/H3c) solo inicializa la tarjeta y monta FAT en la VFS; no contiene lógica de archivos.
2. **Flujo sin búfer:** `setvbuf(_IONBF)` evita el búfer de newlib en el heap y la doble caché sobre FATFS.
3. **Política de rutas (H3-2):** la `ImageLocation` es relativa a la base (`/sd` en el ESP32). Se rechazan con
   `InvalidLocation`:
   - una `/` o `\` inicial;
   - cualquier `\` o `:`;
   - segmentos vacíos, `.` o `..`;
   - rutas que no caben en el búfer fijo (387 caracteres: base de hasta 259, `/` y ubicación de hasta 127).
   Una base vacía o de más de 259 caracteres (MAX_PATH de Windows menos el NUL) no aborta: `baseValid()` es
   falso y cada `open()` devuelve `InvalidArgument`.
4. **Mapeo de errores:**
   - `ENOENT`/`ENOTDIR`, o algo que no es un archivo regular (comprobado con `stat`) → `NotFound`;
   - otros errores de apertura → `IoError`;
   - sin slot libre → `NoFreeSlot`.
5. **Lecturas (H3-3):**
   - se comprueban los límites antes de cualquier E/S, y `OutOfBounds` no escribe nada;
   - `fread` se repite hasta completar la lectura;
   - un fin de archivo inesperado (archivo truncado, tarjeta retirada) o `ferror` → `IoError`;
   - **ante `IoError`, el contenido del búfer queda sin especificar.** Los consumidores no lo usan: `DiskImage`
     devuelve el error y el despachador rellena con ceros.
6. **`size()`** se lee una vez al abrir (`fseek`/`ftell`) y queda fija.
7. **Cierre:** `close()` es idempotente. Un `fclose` que falla igual libera el slot.
8. **Slots (H3-4):** 3 en el ESP32 (disco montado, reemplazo durante un remontaje y diagnóstico) y `max_files = 4`
   al montar FAT.

## Concurrencia (ADR-014)

- FATFS se compila con `FF_FS_REENTRANT 1` (`ffconf.h`), y las operaciones de la VFS FAT toman un lock por volumen
  (`vfs_fat.c`).
- Los metadatos de los slots solo los toca el contexto de almacenamiento.
- Cada `FILE*` lo usa un solo contexto a la vez: el de la unidad si la fuente está vinculada, y el de almacenamiento
  si no lo está.

## Consecuencias

- Las suites de contrato de H2a corren contra archivos reales en el host y en CI (`test_stdio_storage`).
- La Meta A corre en el host como fixture de CTest (`test_meta_a`), con imágenes generadas por `tools/make_atr.py`.
- Abrir puede reservar memoria dentro de la biblioteca de C (FILE de newlib, LFN en el heap); ocurre en el contexto
  de almacenamiento, fuera de la ruta SIO. **Leer no debe reservar memoria**; en el ESP32 se verifica en H3c (H3-13,
  ADR-011).
- Requiere Python 3 para las pruebas de host (generación de imágenes).

## Precisión H3b: solo lectura

ESP-IDF v6.1 **no ofrece montaje de solo lectura para tarjetas SD**. `esp_vfs_fat_spiflash_mount_ro` es solo para
particiones de la flash interna, y `esp_vfs_fat_bdl_mount` detecta dispositivos de solo lectura a nivel de bloque.
La lectura sin escritura se garantiza en nuestra capa:

- `StdioByteSource` solo abre con `"rb"`;
- ningún componente usa APIs de escritura sobre `/sd`;
- `rb_esp_storage` monta siempre con `format_if_mount_failed = false` (H3-5), así que una tarjeta nunca se formatea.

## Fuentes

- ESP-IDF v6.1, *FAT Filesystem Support* y *Virtual Filesystem Component*.
- Fuentes de ESP-IDF v6.1 (Apache-2.0, solo consultadas): `components/fatfs/vfs/vfs_fat.c` (`vfs_fat_pread` con
  lock del volumen) y `components/fatfs/src/ffconf.h` (`FF_FS_REENTRANT 1`).
