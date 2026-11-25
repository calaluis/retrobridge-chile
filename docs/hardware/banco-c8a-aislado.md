# Banco C8a aislado: especificación y plan

> **PROPUESTA NO APROBADA.** Especifica el banco; no lo construye.
>
> - Este PR no añade la variante, no genera ninguna imagen, no añade ningún job de CI y no flashea nada.
> - Cada fase de §6 necesita una decisión aparte del responsable.
> - C8a no aprueba H4, no acepta R2 ni R7 y no conecta el Atari.
>
> **F1 implementada** (rama `feat/banco-c8a-f1`): C-1 y C-2 en código, con pruebas de host. Sin variante, sin imagen
> y sin job de CI: F2, F3 y F4 siguen sin aprobar.

## 1. Para qué sirve

C8a (`h4-c-casetera.md` §3) comprueba en el ESP32 real que GPIO21 (DATA IN) pasa de UART2 al RMT y vuelve, al
terminar un tramo y al cancelarlo, y que al final queda liberado. Lee la **matriz GPIO**: qué periférico tiene el
pin. **No mide niveles eléctricos**: eso es C8b.

El código del banco sigue en el árbol (`main/bench_c8.cpp`, `sdkconfig.bench-c8`, `CONFIG_RB_BENCH_C8`), pero
desde el PR #110 ningún paquete lo compila (`procedimiento-prueba-banco-c8.md`).

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
| C-6 | **Sin distribución:** sin `upload-artifact` hasta una decisión explícita | Que aparezca una imagen descargable sin decisión | — |
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

## 7. Riesgos

| Riesgo | Mitigación |
|---|---|
| El banco se ejecuta con el Atari conectado y encendido | C-1 y C-2 |
| El binario de banco se instala creyendo que es el menú | C-3 y C-4 |
| El banco se tome como prueba de R2 | §5 y la cabecera de este documento: C8a no mide niveles |
