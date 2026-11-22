---
type: spec
title: Spec — Wi-Fi y fuentes TNFS en la FujiNet (menu-sio E2)
description: QUÉ y POR QUÉ de navegar servidores TNFS desde el 65XE con la FujiNet alimentada solo por el SIO.
tags: [sdd, spec, firmware, sio, menu, tnfs, wifi]
timestamp: 2026-11-21T23:59:00Z
topic: sdd
slug: menu-sio-wifi-tnfs
branch: docs/spec-menu-sio-wifi-tnfs
status: approved
---

# Spec — Wi-Fi y fuentes TNFS en la FujiNet (menu-sio E2)

> Slug: `menu-sio-wifi-tnfs` · Estado: **aprobada** por el responsable el 2026-11-21, con los ajustes de C1 y C10 ·
> Creada: 2026-11-21
> Rama: `docs/spec-menu-sio-wifi-tnfs`, desde `main` (861ddb7).
> Base: `menu-sio-fisico` (etapa E2, criterio 8, y sus dos decisiones diferidas), `menu-tnfs`,
> `menu-editar-servidor`, `menu-sio-arranque-temprano` (PR #77).

## Problema y por qué

El firmware `menu-sio` ya sirve el menú y las ATR de la microSD al 65XE físico, solo con el SIO. Las fuentes 2–8
del menú no funcionan en la FujiNet: el firmware no se conecta a ninguna red, no sabe qué servidores hay y la tecla
`E` responde `SIN ARCHIVO DE FUENTES`. Navegar un servidor TNFS hoy solo es posible con el PC (`retrobridge_host`
con `--sources`) y Altirra. Para usar una imagen de la red en el 65XE hay que pasar por el PC y copiarla a la
microSD.

## ¿Qué pasa si no lo construimos?

La FujiNet queda como lector de microSD. Cada juego nuevo exige sacar la tarjeta, copiar en el PC y volver a
insertarla. La parte de red del producto, que ya existe y funciona en el PC, nunca llega al Atari real.

## La alternativa más barata

Copiar las ATR a la microSD desde el PC. Funciona hoy y no cuesta nada. No basta porque obliga a tener el PC al
lado y a manipular la tarjeta; el objetivo del producto es usar el Atari solo.

## Objetivos

- Con la FujiNet alimentada solo por el Atari, navegar un servidor TNFS desde el menú, asignar a D1 una `.ATR` o
  un `.XEX` y arrancarlo con `A`.
- Configurar la red y los servidores sin recompilar el firmware y sin datos sensibles en el repositorio.
- No perder nada de lo que ya funciona: el arranque en menos de 1 s y la microSD siguen igual aunque la red falle.

## Usuarios y contexto

El responsable, con un 65XE físico y una FujiNet FN32ROV-1.3 conectada solo por el SIO (USB desconectado). Prepara
la microSD una vez en el PC con dos archivos de texto; después usa solo el Atari.

## Qué podrá hacer el usuario

- **Configurar la red.** Escribir el nombre de la red y la clave en `retrobridge-wifi.txt`, en la raíz de la
  microSD. *Condición:* la tarjeta está en la FujiNet al encender.
- **Configurar servidores.** Escribir hasta 7 servidores en `retrobridge-fuentes.txt`, con el mismo formato que
  `--sources` del PC (uno por línea; las líneas vacías y las que empiezan con `#` se saltan).
- **Editar servidores en el 65XE.** Con `E` en la pantalla principal, cambiar o añadir el servidor de las fuentes
  2–8. El cambio queda guardado en `retrobridge-fuentes.txt` y sigue ahí tras apagar.
- **Navegar y montar.** Abrir una fuente 2–8, recorrer sus carpetas y montar una imagen en cualquier unidad D1–D8,
  como en el PC. La lista muestra carpetas, `.ATR` y `.XEX`, como en el PC (`menu-xex`). D1 acepta una `.ATR` o un
  `.XEX` y lo arranca con `A`; D2–D8 aceptan `.ATR` y se montan al momento.
- *Quién:* cualquiera con el Atari delante; no hay cuentas ni permisos.

## Qué resultado obtiene

- **Referencia aprobada:** el comportamiento del menú TNFS en el PC con Altirra (`menu-tnfs`, `menu-editar-servidor`,
  `menu-xex`): mismas pantallas, teclas, mensajes de error y `CARGANDO nn%`. En el 65XE físico se ve igual.
- Al encender: el menú en D1 en menos de 1 s desde EN, como hoy. El Wi-Fi se conecta en segundo plano.
- Con red y servidor: la fuente lista las carpetas del servidor; una `.ATR` o un `.XEX` elegido para D1 arranca con
  `A` y el programa queda corriendo en el 65XE.
- Un `.XEX` que el cargador no puede arrancar da el mismo aviso que en el PC (`XEX NO VALIDO`, `XEX NO COMPATIBLE`);
  ninguna unidad cambia.
- Sin red, sin archivo de Wi-Fi o con el servidor caído: el menú y la fuente 1 (SD) funcionan igual; abrir una
  fuente 2–8 dice el motivo en la línea de mensajes (por ejemplo `SIN WI-FI`, `SIN ARCHIVO WI-FI`,
  `SERVIDOR NO ENCONTRADO`). El texto exacto de cada motivo se fija en el plan.
- Una imagen que no cabe en la memoria disponible se rechaza con un aviso; ninguna unidad cambia.

## Qué debe respetar

- **Alimentación:** E2 funciona con el USB físicamente desconectado y la FujiNet alimentada por el Atari. Cambiar
  este requisito a «con USB» es una decisión del responsable, no del plan.
- **Plazo de D1:** menos de 1 s desde EN (PR #77). El Wi-Fi no lo retrasa.
- **Clave:** se carga en RAM al arrancar. Nunca aparece en el repositorio, la consola, el registro de arranques
  (`retrobridge-arranques.txt`) ni ningún otro registro. Queda en texto plano en la tarjeta: aceptado por el
  responsable.
- **NVS:** se conserva la existente. Ni se borra ni se escribe la red o la clave en ella.
- **Procedencia:** sin código de FujiNet ni de terceros (ADR de procedencia del proyecto).
- **Lo ya construido:** `rb_net_tnfs`, `MenuController` y `MenuSources` son los mismos del PC. El arranque de `.XEX`
  desde TNFS reutiliza el flujo del PC (`menu-xex`: el mismo cargador 6502 propio y la misma imagen de D1); no se
  escribe un cargador nuevo para el ESP32. Sus pruebas de host y la regresión en Altirra siguen en verde.
- **Unidades persistentes** (`menu-unidades-persistentes`, en `main` desde `b3bd374`, T6 cerrada en el banco): las
  imágenes TNFS se recuerdan vacías; esta spec no cambia eso. Tampoco cambia la vuelta de D1 al menú al apagar el
  65XE (PR #84).
- **Aprobaciones:** cada flash, escritura por COM3, commit y PR necesita la aprobación del responsable.

## Cómo lo demuestra

- **Primero, la viabilidad eléctrica:** el presupuesto es el documentado para el pin 10 del SIO, 50 mA. El plan
  caracteriza en banco el consumo real de la FujiNet durante la búsqueda de red, la conexión y una transferencia
  TNFS, con el USB desconectado. El resultado (cifras, método, foto del montaje) queda en `02-DOCS/raw/`. Si supera
  el presupuesto, el trabajo se detiene y el responsable recibe las opciones, todas manteniendo E2 solo por SIO.
- **Host:** pruebas de lectura de `retrobridge-wifi.txt` y `retrobridge-fuentes.txt`, de los motivos sin red y de
  que la clave no sale por ningún registro (clave centinela buscada en toda la salida).
- **Banco físico:** registro de arranque (`listo_ms`, `brownout`) de 10 encendidos solo con SIO; foto del 65XE con
  la carpeta del servidor, con la ATR arrancada y con el `.XEX` corriendo; captura completa de la consola sin la
  clave.
- **Criterio final del responsable:** en el 65XE físico, con el USB desconectado: menú al primer encendido solo con
  SIO; abrir `tnfs.calaluis.cl:16384` desde RetroBridge, navegar una carpeta, elegir un `.XEX` real de su biblioteca,
  asignarlo a D1 y pulsar `A` hasta ver el programa corriendo. Lo mismo con una `.ATR`.
- **Orden:** la medición eléctrica (criterio 1) sigue siendo el paso previo a integrar el Wi-Fi por SIO. Nada de
  lo anterior se prueba en el 65XE antes de que el criterio 1 se cumpla.

## Comportamiento

- **Arranque:** igual que hoy hasta servir D1. Después, con la microSD montada, se leen los dos archivos. Si el de
  Wi-Fi es válido, la conexión empieza en segundo plano y se reintenta sola si se pierde.
- **Abrir una fuente 2–8:** si hay red, como en el PC (listado, `CARGANDO nn%`, Esc cancela). Si no, el motivo y la
  pantalla principal sigue operativa.
- **`E`:** edita y guarda como en el PC, sobre `retrobridge-fuentes.txt`. Si el archivo no existe, lo crea. Si no se
  puede escribir, lo dice y la fuente no cambia.
- **Errores de archivo:** un `retrobridge-wifi.txt` mal formado se trata como «sin Wi-Fi» con su motivo; un
  `retrobridge-fuentes.txt` con una línea inválida da el motivo con el número de línea, como `--sources` hoy.
- **Sin microSD:** el menú arranca y dice el motivo (como hoy); no hay Wi-Fi porque no hay configuración.

## Criterios de aceptación

1. **Viabilidad eléctrica.** Dado el USB desconectado, cuando la FujiNet busca la red, se conecta y trae una imagen
   TNFS, entonces la corriente **media de cada fase** que toma del pin 10 del SIO no supera los 50 mA documentados
   para ese pin, y la caracterización en banco (método y cifras de cada fase) está documentada. Los picos se
   registran si hay osciloscopio; si no, constan como «no medidos». Los picos no deciden el criterio. Si los supera, la ejecución se detiene y
   se presentan opciones al responsable, todas manteniendo el requisito de E2 solo por SIO.
2. **Arranque intacto.** Dado `retrobridge-wifi.txt` válido y solo el SIO, cuando se enciende el 65XE 10 veces,
   entonces el menú aparece cada vez, `listo_ms` < 1000 y `brownout` = 0 en el registro de arranques.
3. **Navegar.** Dado un servidor TNFS alcanzable en `retrobridge-fuentes.txt`, cuando se abre su fuente en el 65XE,
   entonces se listan sus carpetas y se puede entrar y salir de ellas.
4. **Arrancar desde la red, ATR y XEX.** Con el USB desconectado y solo el SIO:
   - a. Dada una `.ATR` del servidor elegida para D1, cuando se pulsa `A`, entonces la ATR arranca en el 65XE físico.
   - b. Dado un `.XEX` real de la biblioteca del responsable en `tnfs.calaluis.cl:16384`, elegido en una carpeta del
     servidor y asignado a D1, cuando se pulsa `A`, entonces el programa queda corriendo en el 65XE físico.
5. **D2–D8.** Dada una imagen TNFS montada en D2, cuando un programa lee D2, entonces recibe los sectores de esa
   imagen durante la sesión (prueba de host y Altirra).
6. **Editar en el 65XE.** Dada la tecla `E` sobre la fuente 3, cuando se escribe un servidor y se guarda, entonces
   `retrobridge-fuentes.txt` lo contiene y, tras apagar y encender, la fuente 3 lo muestra.
7. **Sin red.** Dado que falta `retrobridge-wifi.txt`, o la red no existe, o la clave es incorrecta, cuando se abre
   una fuente 2–8, entonces se ve el motivo, la fuente 1 sigue funcionando y D1 cumple el criterio 2.
8. **Servidor caído.** Dada la red conectada y el servidor sin responder, cuando se abre su fuente, entonces se ve el
   motivo dentro del mismo plazo que usa el menú TNFS en el PC (`menu-tnfs`) y la pantalla principal sigue operativa.
9. **La clave no se filtra.** Dada una clave centinela en `retrobridge-wifi.txt`, cuando se arranca, se conecta y se
   ejecutan las órdenes de consola, entonces la clave no aparece en la consola, en `retrobridge-arranques.txt` ni en
   ningún archivo del repositorio.
10. **NVS conservada.** Dada la partición NVS leída antes del primer arranque del firmware candidato, cuando termina
    el ensayo (arranques, conexiones y transferencias), entonces la NVS leída de nuevo es idéntica a la primera. Solo
    se registra el hash de cada lectura o el resultado de la comparación; el contenido de la NVS no se guarda ni se
    muestra. El método de lectura lo fija el plan.
11. **Imagen demasiado grande.** Dada una imagen que no cabe en la memoria disponible, cuando se elige, entonces se
    rechaza con un aviso y ninguna unidad cambia.
12. **Regresión.** Dadas las pruebas de host existentes y la regresión en Altirra, entonces siguen en verde.
13. **XEX que no arranca.** Dado un `.XEX` del servidor que el cargador rechaza, cuando se elige para D1, entonces se
    ve el mismo aviso que en el PC (`XEX NO VALIDO` o `XEX NO COMPATIBLE`) y ninguna unidad cambia (prueba de host).

## Fuera de alcance

- Recordar montajes TNFS después de apagar.
- Configurar el Wi-Fi desde el menú del Atari.
- Varias redes, redes empresariales (WPA-Enterprise) o cifrar la clave en la tarjeta.
- Escribir de vuelta al servidor los cambios hechos a una imagen TNFS.
- Cambiar el requisito de alimentación a USB sin decisión del responsable.
- Alta velocidad SIO, la casetera y cualquier función de red distinta de TNFS.

## Puntos a clarificar

- **suposición tomada** — el presupuesto de corriente del pin 10 del SIO es de 50 mA. *Base:* el valor documentado
  para ese pin, fijado por el responsable el 2026-11-21. *Riesgo:* si el consumo real lo supera, el criterio 1
  detiene el trabajo y se presentan opciones solo por SIO.
- **suposición tomada** — el formato de `retrobridge-wifi.txt` (cómo se escriben red y clave) lo fija el plan;
  debe admitir espacios en la red y en la clave. *Base:* es un detalle de cómo, no de qué. *Riesgo:* ninguno para
  el contrato.
- **suposición tomada** — una imagen TNFS vive en RAM, como en el PC; la placa tiene PSRAM. *Base:*
  `MemoryImageStore` y el arranque de PR #77. *Riesgo:* el límite de tamaño real se mide en el plan; el criterio 11
  cubre el exceso.
- **suposición tomada** — la consulta de nombres (DNS) de los servidores funciona con la red que da el router.
  *Base:* el PC resuelve los mismos nombres. *Riesgo:* si no, el motivo lo dirá (criterio 8).
- **suposición tomada** — el `.XEX` del banco es uno que el flujo del PC ya arranca en Altirra. *Base:* ningún
  cargador arranca el 100 % de los `.xex` (`menu-xex`); el banco prueba el transporte por Wi-Fi y SIO, no la
  compatibilidad del cargador. *Riesgo:* si ese `.XEX` corre en Altirra y no en el 65XE, es un hallazgo del banco,
  no se cambia de archivo para ocultarlo.
- **decisión diferida** — recordar montajes TNFS entre encendidos (`menu-unidades-persistentes`).
- **área no formulable** — qué pasa con el consumo si el Wi-Fi queda activo mientras corre un juego; se verá con la
  medida del criterio 1.

## Decisiones tomadas por el responsable (2026-11-21)

- Wi-Fi en `retrobridge-wifi.txt` de la microSD; clave en texto plano en la tarjeta, aceptado; en RAM al arrancar;
  fuera del repositorio, la consola y los registros; NVS existente conservada.
- Fuentes en `retrobridge-fuentes.txt`, formato de `--sources`; `E` edita y guarda 2–8 desde el 65XE; TNFS en D1–D8
  durante la sesión.
- Alimentación solo por el Atari, USB desconectado; D1 en menos de 1 s; Wi-Fi en segundo plano; la microSD sigue
  si falla la red. Viabilidad eléctrica como primera comprobación del plan; pasar a USB requiere su decisión.
- Rama `docs/spec-menu-sio-wifi-tnfs` desde `main`; aprobación por fase.
- 2026-11-22: el arranque desde TNFS se valida con `.ATR` y `.XEX`. Banco T8 con el 65XE físico y el USB
  desconectado: abrir `tnfs.calaluis.cl:16384`, navegar una carpeta, elegir un `.XEX` real, asignarlo a D1 y pulsar
  `A` hasta ver el programa corriendo. Se reutiliza el flujo TNFS/XEX del PC. La medición eléctrica (T1) sigue
  siendo el paso previo a integrar el Wi-Fi por SIO.

## Clarificaciones

- 2026-11-21 · ¿Contra qué presupuesto se mide C1? → los 50 mA documentados del pin 10; el plan caracteriza en banco
  búsqueda, conexión y transferencia → el criterio debe ser binario. Si se supera, las opciones mantienen E2 solo
  por SIO.
- 2026-11-21 · ¿Cómo se demuestra C10 sin exponer la NVS? → igualdad entre la lectura previa al primer arranque del
  candidato y la posterior al ensayo, registrando solo hash o resultado → la NVS puede tener datos de la placa que
  no deben salir de ella. El método de lectura es del plan.

- 2026-11-22 · ¿El arranque desde TNFS se valida solo con ATR? → no: con `.ATR` y con un `.XEX` real (criterio 4b),
  reutilizando el flujo del PC; el rechazo de un `.XEX` se cubre en host (criterio 13) → el objetivo del producto
  incluye `.XEX` y el servidor del responsable los mezcla con las ATR.

- 2026-11-22 · ¿Los 50 mA de C1 son media o pico? → media de cada fase (reposo, búsqueda, conexión,
  transferencia); picos registrados si hay osciloscopio, sin decidir el criterio → el multímetro da la media y el
  criterio debe ser binario con los instrumentos del banco.

## Objeción registrada

Al abrir la spec se objetó que el Wi-Fi del ESP32 pide picos de cientos de mA y que la corriente del pin 10 nunca
se midió (banco de PR #77): solo con SIO, la FujiNet podría reiniciarse por caída de tensión al conectarse. La
alternativa barata era copiar las ATR a la microSD. El responsable decidió seguir con el requisito de solo SIO y
poner la medida eléctrica como primera comprobación, con parada y opciones si no alcanza.
