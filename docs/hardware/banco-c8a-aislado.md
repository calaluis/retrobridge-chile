# Banco C8a aislado: especificación y plan

> **PROPUESTA NO APROBADA.** Especifica el banco; no lo construye.
>
> - Este PR no añade la variante, no genera ninguna imagen, no añade ningún job de CI y no flashea nada.
> - Cada fase de §6 necesita una decisión aparte del responsable.
> - C8a no aprueba H4, no acepta R2 ni R7 y no conecta el Atari.
>
> **F1 implementada** (PR #114): C-1 y C-2 en código, con pruebas de host.
> **F2 implementada** (PR #115): variante `bench-c8a` del empaquetador (C-3 a C-7) y sus pruebas.
> **F3 implementada** (PR #116): job de CI que compila, empaqueta y verifica `bench-c8a`.
> **F4 autorizada por escrito** por el responsable (2026-11-25, «Procede con el F4»): la imagen se sube solo en una
> ejecución manual sobre `main`, con retención de 7 días (§6, Estado de F4). La ejecución en la placa se registra en
> `docs/mediciones/`.

## 1. Para qué sirve

C8a (`h4-c-casetera.md` §3) comprueba en el ESP32 real que GPIO21 (DATA IN) pasa de UART2 al RMT y vuelve, al
terminar un tramo y al cancelarlo, y que al final queda liberado. Lee la **matriz GPIO**: qué periférico tiene el
pin. **No mide niveles eléctricos**: eso es C8b.

El código del banco sigue en el árbol (`main/bench_c8.cpp`, `sdkconfig.bench-c8`, `CONFIG_RB_BENCH_C8`), pero
desde el PR #110 ningún job de CI lo empaquetaba (`procedimiento-prueba-banco-c8.md`). Desde F2, solo la variante
`bench-c8a` del empaquetador lo acepta; desde F3, el job `firmware-package-bench-c8a` la compila y verifica sin
distribuirla.

## 2. ¿Es compatible con el estado vigente (H4 = false, M3a = true)?

**Sí, con cambios.** Lo que se comprobó en el código de `main`:

| Punto | Estado en `main` | Consecuencia |
|---|---|---|
| Concesión del banco | `validate_for_bench_data_in()` no mira las puertas | El banco funciona con H4 sin aprobar |
| Salida de casetera | `validate_for_cassette_output()` exige H4 y M3a | Con H4 = false, la casetera nunca toma DATA IN |
| Menú SIO | `RB_MENU_SIO` depende de `!RB_BENCH_C8` | El banco no incluye el menú ni responde como D1 |
| MOTOR | Con M3a aprobada, `app_main` configura GPIO36 como **entrada** | Corregido en F1: §1 de `procedimiento-prueba-banco-c8.md` ya dice que GPIO36 es una entrada |
| Empaquetador | Toda variante rechaza `CONFIG_RB_BENCH_C8` (`BENCH_CHECK`) | Hace falta una variante nueva con `bench=True` |
| Ejecución | Antes de F1, `run_c8` corría **sola, al arrancar** | Corregido en F1 (C-2): nunca al arrancar; solo con la orden `c8a run` por la consola USB, una vez por arranque |
| Atari conectado | Antes de F1, el banco solo lo avisaba por consola | Corregido en F1 (C-1): antes de tocar GPIO21 lee SIO_5V en GPIO35 y no corre si hay tensión o la lectura no es válida. **No detecta un Atari apagado pero conectado**: la regla «cable SIO desconectado» sigue vigente |

## 3. La variante propuesta

```text
"bench-c8a": Variant("retrobridge-BENCH-C8A-NO-SIO-fujinet-atari-v1x", "<título>",
                     gates=(False, True), bench=True, sio_calls=<las de la casetera>, ...)
```

- `gates=(False, True)`: el estado vigente. Si H4 se aprobara más adelante, el empaquetado de esta variante
  fallaría y obligaría a revisarla.
- `sdkconfig.bench-c8` sin cambios, más el fragmento de placa.
- Sin menú SIO, sin salida de casetera y sin ninguna concesión nueva de pines.

## 4. Controles para que no se confunda con un paquete autorizado para el SIO

| # | Control | Qué impide | Límite |
|---|---|---|---|
| C-1 | **Enclavamiento SIO_5V:** antes de tocar GPIO21, leer GPIO35 (ADC1_CH7, divisor de 2 k / 3,9 k). Si hay tensión del pin 10, abortar sin configurar GPIO21 | Ejecutar el banco con un Atari **encendido** conectado | No detecta un Atari **apagado** pero conectado. No sustituye a la regla «cable SIO desconectado» |
| C-2 | **Arranque manual:** `run_c8` solo con una orden por la consola USB (por ejemplo `c8a run`), nunca al arrancar | Que una placa enchufada al Atari por error transmita al encender | El responsable debe confirmar el montaje antes de escribir la orden |
| C-3 | **Nombre del paquete** con `BENCH-C8A-NO-SIO`; el manifiesto declara `bench: true` y `sio: no autorizado` | Confundir el binario con `menu-sio` o con la casetera | — |
| C-4 | **Marcas obligatorias** en la imagen: el texto del banco, y **ninguna** marca de `menu-sio` ni de salida de casetera | Que un binario de banco incluya el menú o la cinta | Ya existe en `FEATURE_MARKERS`; la variante nueva debe exigirlo |
| C-5 | **Puertas leídas** de `puertas.json` y del perfil, como en las demás variantes (`check_gate_record`) | Empaquetar el banco con otro estado de puertas sin revisarlo | — |
| C-6 | **Sin distribución:** sin `upload-artifact` hasta una decisión explícita. Desde F4 (decisión del responsable): solo una ejecución manual (`workflow_dispatch`) sobre `main` sube la imagen, 7 días | Que aparezca una imagen descargable sin decisión, o de código sin fusionar | Un PR o una ejecución manual en otra rama nunca suben la imagen |
| C-7 | Sin `erase_flash`, sin escribir la NVS (`NVS_CHECK`, ya en todas las variantes) | Borrar la configuración Wi-Fi del responsable | — |

## 5. Qué prueba y qué no

- **Prueba:** la ruta del pin en la matriz GPIO en cada paso (`bench_c8.cpp` §1, pasos 0–8), sin fallos de las
  llamadas de ESP-IDF, y `release()` con el pin reseteado (reforzado en `analisis-h4-r2-r7.md` §1.2).
- **No prueba:** niveles eléctricos del pin 3, comportamiento con el Atari, R2 ni R7.

## 6. Plan por fases

| Fase | Contenido | Decisión necesaria | Hecho cuando |
|---|---|---|---|
| F0 | Esta especificación | Revisión del PR | Fusionado |
| F1 | Código de C-1 y C-2 en `bench_c8.cpp` y `app_main.cpp`, con pruebas de host del enclavamiento | Del responsable, en un PR aparte | Pruebas RED→GREEN; ninguna imagen |
| F2 | Variante `bench-c8a` en el empaquetador y sus pruebas en `tools/tests` (C-3, C-4, C-5) | Del responsable | `tools/tests` en verde; ninguna imagen distribuida |
| F3 | Job de CI que compila y verifica, **sin** `upload-artifact` (C-6) | Del responsable | CI en verde |
| F4 | Distribuir la imagen y ejecutar C8a por USB, sin el Atari | Autorización escrita del responsable, una por sesión | Registro en `docs/mediciones/banco-c8-AAAA-MM-DD.md` |

### Estado de F1

- **C-1, enclavamiento SIO_5V** (`main/bench_c8_gate.hpp`, `classifySio5v`): 8 lecturas sueltas de GPIO35 (ADC1
  canal 7, 12 dB, 12 bits, como `menu-sio`). El banco corre solo si **todas** leen, están entre 0 y 3300 mV en el pin,
  difieren 200 mV o menos y quedan por debajo de 500 mV. Una lectura fallida, menos de 8 muestras, un valor fuera de
  rango o lecturas dispersas dan **no válida**: GPIO21 no se toca (fallo seguro). Un ADC que no arranca deja todas
  las lecturas como fallidas.
- **C-2, orden manual** (`C8Trigger`, `main/bench_c8_console.cpp`): `run_c8` ya no corre al arrancar. La orden
  `c8a run` de la consola USB lee GPIO35, consulta el enclavamiento y solo con «ausente» llama a `run_c8`. Una vez
  por arranque; un rechazo por tensión presente permite volver a pedirla tras desconectar el cable.
- **Casetera:** el firmware de banco ya no la arranca. Su bucle movería el mismo `EspDataIn`, que solo admite una
  tarea. Las órdenes `cas` responden que no está iniciada.
- **Pruebas de host:** `test_bench_c8_gate` (11, enclavamiento y orden), `test_bench_c8_sequence` (7, secuencia
  UART2 → RMT → UART2 → liberación de `bench_c8.cpp` sobre el modelo de la matriz GPIO) y
  `tools/tests/test_bench_c8_wiring.py` (5, cableado en `app_main.cpp` y `main/CMakeLists.txt`).
- **Límite:** `bench_c8_console.cpp` y `bench_c8.cpp` no se compilan con ESP-IDF en ningún job hasta F3. El modelo
  del host registra dueños del pin, no niveles eléctricos (eso es C8b).

### Estado de F2

La variante `bench-c8a` de `tools/ci/firmware_package.py`:

- **C-3:** paquete `retrobridge-BENCH-C8A-NO-SIO-fujinet-atari-v1x`; el título dice «NO CONECTAR AL SIO».
  `PROVENANCE.json` declara `"bench": true` y `"sio": "no autorizado"`, y `verify` rechaza el paquete si faltan.
- **C-4:** exige en la imagen las marcas del banco C8 y de la orden C8a (`C8A READY`, `start_c8_command`). Rechaza
  el menú (`CONFIG_RB_MENU_SIO` o sus marcas), el banco antiguo que corría al arrancar (sin la orden C8a) y un ELF
  con `CassetteService::start` **enlazada**: el banco no arranca la casetera. Desde F3 (revisión del PR #116) se
  lee la tabla de símbolos del ELF, no sus bytes; un ELF sin tabla legible se rechaza.
- **C-5:** puertas `(H4 = false, M3a = true)` leídas del perfil y de `puertas.json` (`check_gate_record`). Con otras
  puertas, el empaquetado falla.
- **C-6 (estado histórico, antes de F4):** ningún paso `upload-artifact` de la CI mencionaba el banco. Desde F4, la
  prueba exige que toda subida del banco corra solo en una ejecución manual sobre `main` (Estado de F4).
- **C-7:** sin `nvs_*` (`NVS_CHECK`, como todas las variantes).
- Las llamadas SIO permitidas son las de la casetera, en su número exacto. La concesión de banco sigue solo en
  `main/bench_c8.cpp`. Las demás variantes siguen rechazando la opción y el código de banco.
- **Pruebas:** 11 nuevas en `tools/tests/test_firmware_package.py` (`BenchC8aPackageTest`).
- **Límite:** las marcas y la ausencia de `CassetteService::start` se comprueban con imágenes sintéticas. Que una
  compilación real de ESP-IDF las cumpla se verifica en F3.

### Estado de F3

- **Job** `firmware-package-bench-c8a` de `.github/workflows/ci.yml`, en Pull Requests y ejecuciones manuales:
  compila con `sdkconfig.defaults;sdkconfig.board.fujinet-atari-v1x;sdkconfig.bench-c8` (ESP-IDF v6.1), comprueba
  `CONFIG_RB_BENCH_C8=y` y que el menú no está, empaqueta con `--variant bench-c8a` y verifica el paquete.
- **C-6 (estado histórico, antes de F4):** sin `upload-artifact` y sin salidas a otros pasos o jobs; el paquete
  quedaba en el runner. Desde F4, el job sube el paquete solo en una ejecución manual sobre `main` (Estado de F4).
- **Pruebas:** `BenchC8aCiJobTest` (5) en `tools/tests/test_firmware_package.py`: el job compila el banco,
  empaqueta y verifica `bench-c8a`, solo usa `actions/checkout`, no publica, no flashea, y el árbol real pasa las
  comprobaciones de fuente de la variante.
- **Casetera sin arrancar (revisión 1 del PR #116, H1):** la primera compilación real (`dee10ec`, ejecución
  `45700584507`) falló en C-4: el empaquetador buscaba el nombre `CassetteService::start` en todos los bytes del
  ELF, y la información de depuración conserva el nombre de una función que el enlazador descartó. Ahora:
  - **ELF:** la función no debe estar **definida** en `.symtab` (`STT_FUNC` con sección). Si el nombre aparece fuera
    de `.symtab`, el resumen lo dice. Un ELF sin tabla de símbolos legible falla.
  - **Fuente** (`test_bench_c8_wiring.py`): `start()` no es virtual. Solo la llaman `app_main.cpp`, fuera de la rama
    del banco, y `menu_sio.cpp`, que CMake compila solo con `CONFIG_RB_MENU_SIO`, y Kconfig exige
    `!RB_BENCH_C8`. Cualquier otra llamada en `main/` o `components/` hace fallar la prueba.
  - **Controles negativos:** volver a llamar a `g_cassette.start()` en la rama del banco, añadir otra llamada en un
    componente, buscar bytes en todo el ELF o aceptar un ELF ilegible: los cuatro mutantes fallan.
- **Límite (F3):** que la función no esté enlazada prueba que el banco no puede arrancar la casetera, pero compilar y
  verificar en la CI no demuestra que el banco funcione en la FujiNet. Eso es F4, con autorización escrita del
  responsable.

### Estado de F4

Autorización escrita del responsable, 2026-11-25: «Adelante. Procede con el F4». Montaje confirmado: la FujiNet solo
por USB (COM3), con el cable SIO desconectado y sin el Atari. Se vuelve a confirmar justo antes de flashear.

1. **Imagen.** El job `firmware-package-bench-c8a` sube el paquete `retrobridge-BENCH-C8A-NO-SIO-fujinet-atari-v1x-<sha7>`
   solo en una ejecución manual (`workflow_dispatch`) sobre `main`, con retención de 7 días y su `artifact-digest` en
   el resumen. La subida y el digest llevan `if: github.event_name == 'workflow_dispatch' && github.ref ==
   'refs/heads/main'`: un PR, un push o una ejecución manual en otra rama compilan y verifican, pero no suben nada
   (revisión 1 del PR #117, H1). Las pruebas evalúan esa condición en los cuatro casos.
2. **Instalación.** Paquete descargado en `D:\GitHub\Respaldos\FujiNet-RB-bench-c8a-<sha7>\`, verificado con
   `firmware_package.py verify`, y `write_flash` + `verify_flash` de las tres regiones, como en
   `procedimiento-prueba-banco-c8.md` §4. Sin `erase_flash` y sin escribir la NVS.
3. **Ejecución.** Reinicio, captura del arranque (`C8A READY`) y la orden `c8a run`. Pasa con `C8 RESULT PASS`, sin
   `[FAIL]`, y con la lectura SIO_5V «ausente».
4. **Restauración.** Se vuelve a instalar el paquete `menu-sio` que estaba en la placa, verificado igual.
5. **Registro** en `docs/mediciones/banco-c8a-AAAA-MM-DD.md`: imagen y digest, versión anterior, captura, resultado
   y restauración.

C8a no aprueba H4, no acepta R2 ni R7, no mide niveles (C8b) y no conecta el Atari.

## 7. Riesgos

| Riesgo | Mitigación |
|---|---|
| El banco se ejecuta con el Atari conectado y encendido | C-1 y C-2 |
| El binario de banco se instala creyendo que es el menú | C-3 y C-4 |
| El banco se tome como prueba de R2 | §5 y la cabecera de este documento: C8a no mide niveles |
