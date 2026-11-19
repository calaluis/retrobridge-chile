---
type: progress
feature: menu-tnfs
branch: feat/menu-tnfs-protocolo
timestamp: 2026-11-18
---
# Progreso: menu-tnfs

## T001 — 2026-11-18
- status: complete
- files: docs/fuentes.md (protocolo TNFS de spectranet, FIPS 180-4)
- risk: tier 0 (docs)
- controls: check_notices OK
- commit: hecho (ver git log de feat/menu-tnfs-protocolo)

## T002 — 2026-11-18
- status: complete
- red: ctest test_tnfs_codec contra el stub, 15 de 16 pruebas fallan por aserción (encoders y decoders devuelven error)
- green: test_tnfs_codec pasa (16 pruebas; 9 órdenes byte a byte, OPEN = 01 00 00 00, 10 000 datagramas aleatorios)
- files: components/rb_net_tnfs/**, test/host/net_tnfs/test_tnfs_codec.cpp, test/host/CMakeLists.txt, tools/check_layering.py
- risk: tier 1 (rsc risk --base 170a411), un revisor
- review: 8 reported -> 8 unique -> 1 a verificar (F1: PLAUSIBLE, no defecto del codec; pasa a T005). Ronda 1.
  - arreglados: mutantes supervivientes (le32 byte alto, count == pedido, datagrama de 1024 exacto), buffers de prueba que se pisaban, orden en CMake
  - intencionales: kStale/kInvalidHandle (los usa T005), UMOUNT con secuencia 0 (lo dice el documento), contrato de MalFormada y de "una sola apertura" (lo comprueba la prueba de T005)
- controls: tests OK (50 host, 106 tools) · build OK sin avisos · layering OK · notices OK · security: ASan no ejecutado en local (no disponible en Windows; lo corre la CI) · traceability OK (AC12, AC21, M41-4)
- decisiones: id de conexión opaco de 2 bytes (el documento no fija el orden); reconectar una vez ante 0xFF o 0x20 (el documento no nombra el código de sesión desconocida)
- commit: hecho (ver git log de feat/menu-tnfs-protocolo)

## Requisitos que pasan a T005 (de la revisión de T002)
- Una respuesta READ con estado OK y 0 bytes no es progreso: la lectura debe terminar con error tras un número acotado de respuestas vacías, o al llegar al tamaño esperado. Prueba: servidor falso que responde OK/0 siempre, y la llamada termina con error.
- Un datagrama MalFormada o Ajena se descarta y se sigue esperando hasta el plazo; no aborta la operación.

## Commits — 2026-11-18
- T001: abe78db
- T002: c66b3fa

## T003 — 2026-11-18
- status: complete
- red: ctest test_fake_tnfs_server contra un esqueleto vacío, 12 de 12 fallan por aserción ("expected a datagram")
- green: test_fake_tnfs_server pasa (20 pruebas tras las tres rondas)
- files: test/host/fakes/fake_tnfs_server.hpp, test/host/net_tnfs/test_fake_tnfs_server.cpp, test/host/CMakeLists.txt
- risk: tier 1 (rsc risk --base c66b3fa), un revisor por ronda
- review: 3 rondas. R1: 10 hallazgos (2 should-fix, 4 question, 4 nit), todos corregidos. R2: 6 (4 should-fix, 2 nit), todos corregidos. R3: pass con 2 nit, corregidos.
  - finding-verifier no se usó para los should-fix: eran huecos de prueba y lógica del doble de prueba, corregidos directamente con pruebas nuevas que fallarían sin la corrección.
  - cambio de fondo: el byte 2 es "retry number" en el documento. Un reintento repite la secuencia; el servidor falso lo reconoce y no ejecuta dos veces (un READ perdido nunca salta un bloque). log() marca cada reintento.
- controls: tests OK (51 host) · build OK sin avisos · layering OK · notices OK · security: ASan no ejecutado en local (lo corre la CI) · traceability OK (AC12, AC15 a AC21, M41-5)
- commit: 3b0c92a

## Requisitos que pasan a T005 (de la revisión de T003)
- Cada reintento de una orden repite su número de secuencia; una orden nueva lo incrementa. La prueba lo comprueba con log().retry.
- MOUNT no se deduplica en el servidor falso: un MOUNT reintentado puede dejar una sesión huérfana; el cliente usa la respuesta que llegue y la prueba no debe exigir una sola sesión.
- La reconexión silenciosa se prueba con forgetSession(0xFF) y con forgetSession(0x20).

## T004 — 2026-11-18
- status: complete
- fuente: FIPS 180-4 descargado con permiso del responsable (833 KB, scratchpad, fuera del repo); secciones 4.1.2, 4.2.2, 5.1.1, 5.3.3, 6.2
- red: test_sha256 contra un esqueleto vacío, 5 de 5 fallan por aserción (digest vacío)
- green: test_sha256 pasa (5 pruebas): ejemplos NIST (vacío, "abc", 448 bits, un millón de "a"), relleno en 55/56/63/64/65/119/120 bytes, partición incremental, ATR de referencia 9f21ef9a...99c96 (92 176 bytes)
- oráculos: cada valor esperado comprobado con Python hashlib y certutil; el código no los usa
- files: host/src/sha256.hpp, host/src/sha256.cpp, test/host/menu/test_sha256.cpp, test/host/CMakeLists.txt
- risk: tier 1 (rsc risk --base 3b0c92a), un revisor
- review: ronda 1, pass: 1 nit (uso tras finish() daba un hash erróneo: ahora RB_ASSERT, corregido), 1 question (intencional, ver abajo)
- evidencia de la ATR: la prueba se ejecuta solo con RB_REF_ATR (la ATR es dato de laboratorio, no va al repo). Ejecutada en local con RB_REF_ATR="D:/rb-e0/m41/ref/Atari Logo.atr": pasa. En la CI queda ignorada, no superada. Se vuelve a comprobar en T013 y en la demo.
- controls: tests OK (52 host) · build OK sin avisos · layering OK · security: ASan no ejecutado en local (lo corre la CI) · traceability OK (AC4)
- commit: 9edd713

## T005 — 2026-11-18
- status: complete
- commit: 078394f
- red: test_tnfs_client contra un esqueleto que no hace nada: 30 de 31 fallan por aserción
- green: test_tnfs_client pasa (39 pruebas tras las tres rondas); host 53 de 53
- triangulación: cada corrección de revisión entró con su prueba en rojo primero (cancelación de READDIR, READ y STAT, techo de EAGAIN y de espera anunciada, reconexión rechazada, MOUNT repetido)
- corrección de una prueba: el contador de bloques del sumidero no se reiniciaba en begin(); la prueba de doble pérdida de sesión no podía provocar la segunda. Sin aserciones debilitadas.
- files: components/rb_net_tnfs/include/rb/net_tnfs/tnfs_client.hpp, components/rb_net_tnfs/src/tnfs_client.cpp, components/rb_net_tnfs/CMakeLists.txt, test/host/net_tnfs/test_tnfs_client.cpp, test/host/CMakeLists.txt, test/host/fakes/fake_tnfs_server.hpp (replyWith, unreachable), 02-DOCS/wiki/sdd/decisions.md (M41-8 a M41-13)
- risk: tier 1 (rsc risk --base 9edd713), un revisor por ronda
- review: 3 rondas.
  - R1: 6 hallazgos (2 should-fix, 3 nit, 1 question). finding-verifier: los 2 should-fix CONFIRMED. 4 corregidos; 2 intencionales (fallo de recepción termina el intento; forma de ReadSink).
  - R2: 3 (2 should-fix, 1 nit). finding-verifier: los 2 CONFIRMED. Todos corregidos; el techo de 5 s lo decidió el responsable.
  - R3: pass, 1 nit (el plan no recogía el techo): corregido en el plan.
  - aviso del registro: objeciones distintas de seguridad sobre AC17 en R1 y R2 (esperas largas que Esc no corta). Resuelto con M41-9 y M41-10; queda para comprobar en T011 que el cargador pasa el CancelCheck a stat().
- controls: tests OK (53 host) · build OK sin avisos · layering OK · notices OK · tools/tests OK (106) · security: ASan no ejecutado en local (lo corre la CI) · traceability OK (AC12, AC16, AC17, AC20, AC21)
- decisiones: M41-8 a M41-13 en wiki/sdd/decisions.md; el plan recoge el techo de 5 s (§0 y §3)

## Requisitos que pasan a T011 (de T005)
- El cargador pasa su CancelCheck a stat() en cada STAT del listado y a readWholeReadOnly().
- Progreso de la carga: offset + tamaño de cada data() contra el tamaño de begin(); begin() puede llegar dos veces (reconexión).
- Rejected solo lo da mount(); las operaciones dan NotOpened si el primer MOUNT es rechazado y NoResponse si falla la reconexión.

## T008 — 2026-11-18
- status: complete
- commit: f2834af
- red: test_udp_tnfs_port contra un esqueleto que siempre falla: 3 de 3 fallan por aserción
- green: test_udp_tnfs_port pasa (4 pruebas tras dos rondas); host 54 de 54; retrobridge_host compila sin avisos y su prueba pasa
- mutante a mano: openTo conectando siempre a 127.0.0.1 hace fallar la prueba de MOUNT y READ (el servidor escucha en 127.0.0.2); original restaurado desde copia
- files: host/src/udp_tnfs_port.hpp, host/src/udp_tnfs_port.cpp, host/src/udp_datagram_port.hpp, host/src/udp_datagram_port.cpp, test/host/net_tnfs/test_udp_tnfs_port.cpp, test/host/CMakeLists.txt, 02-DOCS/wiki/sdd/decisions.md (M41-14, M41-15)
- risk: tier 1 (rsc risk --base 078394f), un revisor por ronda
- review: 2 rondas. R1: pass con 3 nit (dirección resuelta sin probar; aserciones con el hilo vivo; corte silencioso en POSIX), corregidos. R2: pass con 2 nit (comentario viejo; prueba ignorada si no hay 127.0.0.2), corregidos: ahora recurre a 127.0.0.1 y falla si no hay loopback.
- controls: tests OK (54 host, 1 retrobridge_host) · build OK sin avisos · layering OK · notices OK · security: ASan no ejecutado en local (lo corre la CI); la rama POSIX de receive no se compiló en local (Windows, WSL sin compilador): la compila y prueba la CI de Linux · traceability OK (AC7)
- red sin tráfico externo: todo por loopback; el resolutor del sistema solo se probó con localhost

## Cierre del PR A — 2026-11-18
- status: correcciones hechas (falta el commit, pendiente de aprobación)
- revisión de cierre: rama entera contra 170a411, ronda 1. 1 should-fix, 3 nit, 1 pregunta (registro L44 a L48).
  - L44 should-fix: solo 7 de 39 pruebas del cliente comprobaban el registro de solo lectura. Corregido: las 40 llaman a assert_read_only al final.
  - L45 nit: el número de secuencia volvía a 0, el de UMOUNT. Corregido: nextSequence salta el 0. Rojo visto antes (test_sequence_numbers_skip_zero_which_umount_uses), luego verde.
  - L46 nit: faltaba #include <initializer_list> en tnfs_codec.cpp. Corregido.
  - L47 nit: línea en blanco de más en test/host/CMakeLists.txt. Quitada.
  - L48 pregunta: pasa a T011 (abajo). Estado en el registro: parked.
- controls: tests OK (54 host, test_tnfs_client 40 de 40; retrobridge_host 1 de 1) · build OK sin avisos · layering OK · notices OK · tools/tests 106 OK · security: ASan, POSIX y ESP-IDF no ejecutados en local (los corre la CI) · traceability OK (AC12, AC21, AC17)

## Requisitos que pasan a T011 (de la revisión de cierre del PR A)
- MOUNT, OPEN, OPENDIR, CLOSE, CLOSEDIR y UMOUNT no se pueden cancelar: en el peor caso el trabajo sigue ocupado unos 30 s. El cargador no debe bloquear key() ni el SIO esperando al trabajo anterior. Una cancelación da la pantalla por cancelada al momento, aunque el trabajo termine después; su resultado se descarta.
- commit: be5cc58 (aprobado 2026-11-18). Ronda 2 de la revisión de cierre: pass, sin hallazgos.
- PR A abierto: https://github.com/calaluis/retrobridge-chile/pull/42 (2026-11-18), tras la CI #136 en verde y la verificación r2 con PASS.

## Comprobación previa a la fusión del PR A — 2026-11-18
- pregunta: ¿cancelar un listado o una descarga puede bloquear el hilo si el servidor no responde a CLOSE o CLOSEDIR?
- reproducción (prueba temporal, retirada; sin cambio de código): servidor simulado mudo para CLOSEDIR (0x12) o CLOSE (0x23), cancelación tras la primera entrada o el primer bloque, tiempo medido con el reloj falso desde la llamada hasta que vuelve Cancelled.
  - 0 silencios: 0 s. 1: 1 s. 3: 3 s. 10: 3 s (solo se gastan 3 intentos). Espera anunciada de 1 s.
  - servidor que anuncia 60 000 ms y nunca responde: 15 s en los dos casos (techo de 3 intentos de 5 s, M41-9).
- conclusión: no hay bloqueo indefinido. La llamada siempre vuelve con Cancelled; el hilo que llama queda ocupado como mucho 3 x 5 s = 15 s (6 s con la espera por defecto de 2 s). Que el menú no espere ese tiempo ya es requisito de T011 (registro L48).
- PR #42: la descripción decía «Issue #41»; #41 es el PR de menu-principal (Issue #40). Corregida: sin issue propia, continúa Issue #40 / PR #41.

## PR B — rama feat/menu-tnfs-menu desde 2a64dcc (aprobada 2026-11-18), worktree .worktrees/feat-menu-tnfs-menu

## T006 — 2026-11-18
- status: complete (falta el commit, pendiente de aprobación)
- red: 5 pruebas de test_menu_program fallan por aserción (normalización, acción 2, tecla durante la espera, arranque tras espera; la prueba de #40 cambiada a propósito: Shift+A sale como $3F)
- green: test_menu_program 20 de 20; menu_program.inc regenerado y --check al día
- mutantes a mano: WAIT_TICKS 5, 14 y 16, sin máscara, otro código de sondeo y ACTION_WAIT 3: todos hacen fallar alguna prueba
- files: tools/make_menu.py, tools/tests/test_menu_program.py, host/src/menu_program.inc, 02-DOCS/wiki/sdd/decisions.md (M41-16)
- risk: tier 1 (rsc risk --base 2a64dcc), un revisor por ronda
- review: 2 rondas. R1: pass con 3 nit y 1 pregunta (ventana de tiempo floja; nombre kMenuPollKey del plan; acciones desconocidas ya no arrancaban; ADR-017 con 10 sectores). Los tres primeros corregidos; el de la ADR pasa a T014. R2: pass sin hallazgos.
- controls: tests OK (tools 112, host 54, retrobridge_host 1) · build OK sin avisos · layering OK · notices OK · security: sin entrada nueva fuera del protocolo del menú · traceability OK (AC15, AC17, AC19, M41-3)
- intérprete de la prueba: RTCLOK avanza cada 20 instrucciones, como un cuadro, en lugar de en cada lectura

## Requisitos que pasan a T014 (de T006)
- ADR-017 dice que el programa ocupa 10 sectores y que la pantalla está en $081F-$0BDE. Ahora son 11 sectores y $0879-$0C39.
- commit: 6aaa56c (aprobado 2026-11-18)

## T007 — 2026-11-18
- status: complete (falta el commit, pendiente de aprobación)
- red: 9 pruebas de test_sector_probe contra un esqueleto: 8 fallan por aserción, 1 por etiqueta ausente
- green: test_sector_probe 9 de 9; test_menu_program 20 de 20 con el intérprete ampliado (acarreo, ADC, LSR, ASL, TXA, TAY, INX, STA abs,Y, STY, BCC/BCS, parada en JMP a sí mismo)
- mutantes a mano: sector 65534, unidad 1, dígito hexadecimal mal, SDMCTL distinto: todos hacen fallar alguna prueba
- files: tools/make_sector_probe.py, tools/tests/test_sector_probe.py, tools/tests/test_menu_program.py
- risk: tier 1 (rsc risk --base 6aaa56c), un revisor
- review: 1 ronda, pass sin hallazgos. Dos detalles sin informe formal: una aserción con constantes (corregida: ahora usa probe.BUFFER) y la falta de CLD (no se añade: el SO deja D a 0 al arrancar y el menú tampoco lo usa).
- controls: tests OK (tools 121) · build: no aplica (solo Python) · layering OK · notices OK · security: sin entrada nueva · traceability OK (AC14, M41-7)
- el disco no se versiona: se genera con `python tools/make_sector_probe.py --out <carpeta --images>`
- commit: 08cf342 (aprobado 2026-11-18)

## T009 — 2026-11-18 (en curso, sin commit)
- hecho: host/src/memory_image_store.hpp (9 huecos, mutex, no libera abierto ni adjunto, *tnfs*0-8), ruteo en host_image_repository.hpp (memory()), test/host/menu/test_memory_image_store.cpp (9 pruebas), registro en test/host/CMakeLists.txt
- red: esqueleto; la primera prueba falla por aserción y las demás abortan con .value() de un adopt fallido (0xc0000409 = abort de UCRT)
- green: 9 de 9; host 55 de 55; retrobridge_host 1 de 1; layering y notices OK
- mutantes: release sin mirar adjunto muere; doClose sin memoria muere (aborto); aceptar '9' en el nombre es equivalente (open(9) da NotFound)
- falta: revisión de tier 1 (ronda 1 abierta en el registro, revisor no lanzado; diff en scratchpad t009-r1.diff), entrada final y commit con aprobación

## T009 — 2026-11-18 (revisado, pendiente de commit)
- status: complete (sin commit)
- risk: tier 1 (rsc risk --base 08cf342), un revisor
- review: 1 ronda, 1 hallazgo nit (L5): close() de un hueco borra `open` sin mirar quién lo tiene; un segundo close tardío de un poseedor anterior podría anular la apertura de otro. Resultado: intentional. Exige violar el contrato (cerrar dos veces tras cambiar de dueño) y `*menu*` sigue el mismo patrón.
- controls (ejecución nueva tras la revisión): tests host 55 de 55 · retrobridge_host 1 de 1 · build OK · layering OK · notices OK · security: sin entrada externa nueva · traceability OK (M41-2)
- aviso de la sesión de rutas largas: main está en acb166a (PR #43 fusionado); conviene integrar main en la rama antes del PR B, porque host_image_repository.hpp usa StdioImageRepository

## T009 — 2026-11-18 (L5 revisado aparte, sin cambio de implementación)
- pregunta: ¿puede darse un cierre duplicado o tardío por cancelación TNFS, reconexión o reutilización de huecos?
- respuesta: no por un flujo válido. Los cinco puntos que cierran fuentes las cierran una sola vez en cada camino:
  - ScopedImage::~ScopedImage, que cierra `source_`, asignado solo por open;
  - MountDiskImage::execute, que cierra solo en sus fallos y después devuelve el fallo; si tiene éxito, la fuente pasa a la unidad;
  - CollectDriveReply::execute, que cierra `displaced` y `rejected`; takeReply vacía la respuesta y pasa a Idle;
  - MenuController::assignD1, que abre y cierra dentro de la misma función;
  - VirtualDrive::mount y unmount, que devuelven la fuente desplazada una sola vez.
- ADR-014: «toda fuente que deba cerrarse vuelve una sola vez, ya desvinculada».
- carga TNFS (T011, según el plan): solo usa adopt y release; nunca cierra un ImageByteSource. CLOSE y CLOSEDIR de TNFS son asas del protocolo dentro de TnfsClient y no tocan el repositorio. release rechaza un hueco abierto o adjunto.
- conclusión: L5 solo ocurre si alguien rompe el contrato y reutiliza una referencia ya cerrada. La limitación queda documentada en el comentario de MemoryImageStore::close(); `*menu*` y los huecos de archivo tienen la misma. Resultado en el registro: intentional (L5).
- controles: host 55 de 55, retrobridge_host 1 de 1, notices OK (tras el cambio de comentario)
- commit: cc01d73 (aprobado 2026-11-18)

## T010 — 2026-11-18 (revisado, pendiente de commit)
- status: complete (sin commit)
- hecho: host/src/tnfs_folder.hpp y .cpp (TnfsListing, TnfsFolder::build, notice, tnfsDisplay, tnfsTitle); test/host/menu/test_tnfs_folder.cpp (10 pruebas); registro en test/host/CMakeLists.txt
- red: esqueleto que compila; las 10 pruebas fallan por aserción
- green: 10 de 10
- mutantes a mano: carpetas al final, sin filtro de punto, UTF-8 byte a byte, aviso con 20 exactas, título sin sitio para «..»: los cinco mueren
- decisión: M41-17 (marca `/` al final, título con «..», nombres vacíos fuera)
- risk: tier 1 (rsc risk --base cc01d73), un revisor
- review: 1 ronda, sin hallazgos
- controls: host 56 de 56 · retrobridge_host 1 de 1 · build OK · layering OK · notices OK · security: sin entrada externa nueva (los nombres vienen del servidor, pero solo se copian con límites) · traceability OK (AC1, AC9, AC10)
- nota: tnfs_folder.cpp aún no entra en el ejecutable; lo añade T011 o T013, que lo consumen
- commit: f5f8332 (aprobado 2026-11-18)

## T011 — 2026-11-18 (revisado, pendiente de commit)
- status: complete (sin commit)
- hecho: host/src/tnfs_loader.hpp y .cpp (TnfsLink, ImageMemory, LoaderHooks, TnfsLoader: startList, startFetch, poll, cancel); ImageBuffer en memory_image_store.hpp (M41-18); TnfsFolder::swap; test/host/menu/test_tnfs_loader.cpp (24 pruebas) con un GatedPort que retiene órdenes elegidas; prueba de T009 adaptada a ImageBuffer y una prueba nueva (búfer movido)
- red: esqueleto; 19 de 21 fallan por aserción. Una de las que pasaban se endureció (comprueba el tipo de resultado)
- green: 24 de 24; 30 repeticiones sin fallos
- mutantes a mano (10): publicar cancelado sin liberar, descartar sin liberar, sin filtro de punto, NotOpened como NoResponde, start que no cancela el anterior, entrega repetida, porcentaje que baja, STAT fallido ignorado, restart que no vacía: todos mueren (los tres últimos y «start no cancela» después de añadir pruebas)
- hallazgo de la implementación: con -fno-exceptions (ADR-004) un std::vector sin memoria aborta; de ahí M41-18
- decisión: M41-18 y M41-19
- risk: tier 1 (rsc risk --base f5f8332), un revisor
- review: 1 ronda, 0 graves; 4 detalles: L6 corregido (búfer movido con tamaño 0, prueba roja y luego verde), L7, L8 y L9 intencionados (M41-19). La corrección de L6 no pasó por una segunda ronda: es un cambio de 14 líneas cubierto por su prueba
- controls: host 57 de 57 · retrobridge_host 1 de 1 · build OK · layering OK · notices OK · security: los datos del servidor solo se copian con límites; tamaño sin tope propio (M41-19, punto 8) · traceability OK (AC5, AC16, AC17, AC18, AC20, AC21, M41-1, M41-6)
- nota: tnfs_loader.cpp aún no entra en el ejecutable; lo conecta T013
- commit: beea525 (aprobado 2026-11-18)

## T012 — 2026-11-18 (sin empezar el código; notas de diseño para la sesión siguiente)
- estado: leídos el plan (fila T012, Interfaces de T012), la spec (Behaviour, Errores, AC1 a AC21), menu_controller.hpp/.cpp y menu_program.inc. No hay código de T012 ni cambios sin commit en la rama.
- se pausa aquí a petición del responsable: el brain se puso al día y T012 sigue en una sesión nueva.
- diseño previsto (por confirmar al implementar):
  - vistas nuevas: carpeta TNFS (reusa la lista: título «FUENTE n: <ruta>» con tnfsTitle, 20 entradas con el display de TnfsFolder), elegir unidad para una ATR TNFS, y carga (la vista queda donde estaba; debajo del pie «CARGANDO nn%»; acción kMenuActionWait).
  - key(): compara $FF (redibujo) y $FE (sondeo) antes de `& 0x3F`; $FE nunca elige; durante una carga Esc cancela, el sondeo informa y cualquier otra tecla se ignora; contesta en 250 ms como máximo (solo poll/start/cancel del cargador, que no esperan a la red).
  - errores del cargador: NoResponde -> «ERROR: SERVIDOR NO RESPONDE», NoEncontrado -> «ERROR: SERVIDOR NO ENCONTRADO», NoSePudoAbrir -> «ERROR: NO SE PUDO ABRIR»; la causa va al registro (menuEvent), nunca a la pantalla. La vista queda donde estaba (principal o carpeta anterior) y nada asignado ni montado cambia.
  - Esc en la raíz de una fuente TNFS vuelve a la principal; en otra carpeta sube un nivel (nuevo listado). Al reabrir una fuente se empieza en «/».
  - shown_ pasa a «nada | entrada local | imagen TNFS (display corto y hueco)»; driveName devuelve el display para TNFS. D1 y el mensaje «D1: <NOMBRE> (A ARRANCA)» usan el display de T010.
  - imagen traída: validar abriendo `*tnfs*k` y DiskImage::open (como assignD1), cerrar; inválida -> «ERROR: ATR INVALIDA» y release del hueco. D1: asignar (el hueco queda para A y para volver a arrancar tras un reinicio en frío). D2-D8: montar `*tnfs*k` por el buzón.
  - liberar huecos: al reemplazar o desasignar D1; al colectar una respuesta, el hueco desplazado (si era TNFS) y el rechazado; no al volver el menú a D1 tras un reinicio en frío (D1 conserva su imagen asignada). release desde el hilo del menú es seguro (cerrojo) pero libera memoria fuera del hilo de carga: decidir si se pasa al cargador (p. ej. TnfsLoader::recycle) y anotarlo como M41-20.
  - 9 huecos alcanzan: D1 asignada + D2-D8 montadas + 1 en validación.
  - pruebas: test_menu_tnfs nuevo (FakeTnfsServer + GatedPort como en test_tnfs_loader), test_host_menu sin cambios en sus casos, test_sio_session_allocations en verde y key() sin reservas durante una carga.
- pendiente antes del PR B: integrar main (acb166a, PR #43) en la rama, con aprobación.

## T012 — 2026-11-18 (hecho)
- decisión: M41-20 (opción B, del responsable): el menú entrega los huecos con `TnfsLoader::recycle`; el hilo de carga los libera antes de cada trabajo y al despertar, y un hueco aún abierto o adjunto conserva su marca. M41-21: pantalla de la carpeta TNFS, formato de «CARGANDO nn%» y vistas tras un error.
- código:
  - `host/src/menu_loader.hpp` (nuevo): interfaz `MenuLoader` (startList, startFetch, poll, cancel, await, recycle); `LoaderError` y `LoaderStatus` se mudan aquí. El menú no depende del cliente TNFS.
  - `TnfsLoader` implementa `MenuLoader`; nuevos `await` (espera acotada con condition_variable, sin reservas) y `recycle` (cola fija de 9 marcas, `freeRecycled` en el hilo de carga).
  - `MenuController`: vistas Folder y FolderPick, trabajos Open/Enter/Up/Fetch, $FF y $FE antes de la máscara, acción 2 con «CARGANDO nn%», Esc cancela y las demás teclas se ignoran durante una carga, `shown_` pasa a `Shown` (nada, local, TNFS con hueco y nombre), validación de `*tnfs*k` para D1 y montaje por el buzón para D2-D8, liberación de huecos en cada camino, la causa del error va al registro (`menu_tnfs`), el SHA-256 en `menu_fetch`. Sin cargador (como en test_host_menu) las fuentes 2-8 siguen diciendo «PENDIENTE: FALTA EL CLIENTE TNFS».
  - `tnfsTitle(const char*, longitud, ...)`: sobrecarga sin `std::string`, para no reservar en `key()`.
  - `host/CMakeLists.txt`: el ejecutable suma `tnfs_folder.cpp` (el controlador dibuja carpetas); el cargador, `sha256.cpp` y `main.cpp` siguen para T013.
  - pruebas: `test_menu_tnfs` nuevo (22 casos, servidor falso, puerto con compuerta y reservas contadas por hilo en cada `key()`); `GatedPort` pasa a `test/host/fakes/gated_port.hpp`; `test_tnfs_loader` suma recycle y await (26 casos); `test_host_menu.cpp` sin cambios (su lista de fuentes en CMake suma `tnfs_folder.cpp`).
- TDD: rojo (test_menu_tnfs falla con los stubs), verde, refactor. Mutantes: quitar la re-marca de `freeRecycled` y quitar el `recycle` del menú hacen fallar las pruebas.
- risk: tier 1 (`rsc@4.1.7 risk --base beea525`), un revisor (refuter-correctness).
- review: ronda 1 de 3, 0 graves. L10 (faltaban buzón ocupado al terminar una carga, segundo arranque de D1 TNFS tras reinicio en frío y reinicio con montaje en curso) y L11 (la cuenta de reservas solo miraba las respuestas de espera): corregidos con pruebas, sin segunda ronda (solo pruebas). L12 (porcentaje sin relleno) y L13 (un error queda en la carpeta tras una letra fuera de la lista, igual que la lista local): intencionados (M41-21).
- controls: host 58 de 58 (test_menu_tnfs, test_tnfs_loader, test_host_menu y test_sio_session_allocations en verde 5 veces seguidas) · retrobridge_host 1 de 1 y compila · layering OK · notices OK · security: las rutas del servidor se copian con límite de 255 bytes; nada del PC llega a la pantalla · traceability: AC1 a AC3, AC5, AC6, AC8 a AC11, AC13, AC15 a AC21, M41-20.
- nota: `ctest` no recompila; después de un mutante hay que volver a compilar antes de correr la suite.
- verify (por tarea, corrida nueva antes del commit, árbol beea525 con cambios): tests PASS (host 58 de 58; retrobridge_host 1 de 1) · build PASS (0 avisos con -Werror en los dos builds) · security SUSTITUIDA: no hay escáner automático para el host C++; se revisó a mano que las rutas del servidor se copian con límite de 255 bytes y que la pantalla solo muestra textos fijos y nombres en forma display (no detecta fallos que esa lectura no vea) · traceability PASS (AC1 a AC3, AC5, AC6, AC8 a AC11, AC13, AC15 a AC21, M41-20, M41-21) · config.yaml sin comandos de prueba propios: NO-APLICA.
- commit: 38d1b17 (aprobado 2026-11-18)

## T013 — 2026-11-18 (hecho)
- contexto (HUMENGIA DEC-002 §6, reutilización del contexto, propuesta): se partió de 38d1b17 y de la entrada de T012; solo se abrieron la fila de T013, main.cpp, udp_tnfs_port.hpp, jsonl_log.* y host_clock.hpp.
- código:
  - `host/src/tnfs_link.hpp/.cpp` (nuevo): `describeTnfsRequest` (línea de registro de una orden: nombre, secuencia; OPEN con flags y modo; ruta de OPEN, OPENDIR, STAT y MOUNT; nunca lee fuera del datagrama ni se pasa del búfer; el usuario y la contraseña de MOUNT no se anotan), `TnfsTracePort` (anota cada orden como evento `tnfs_request` antes de enviarla), `UdpTnfsLink` (TnfsLink sobre UdpTnfsPort de T008) y `HostTnfsClock`.
  - `JsonlLog` toma un mutex en cada línea: el hilo de carga también escribe en el registro.
  - `main.cpp` (runMenu): un enlace por fuente de `--sources`, el cargador sobre la memoria del repositorio y el controlador con el cargador. El cargador se detiene en onStop, antes de cerrar el registro, también cuando el hub no contesta (revisión L14) o falla el socket.
  - `host/CMakeLists.txt`: rb_net_tnfs, Threads, tnfs_loader.cpp, tnfs_link.cpp, udp_tnfs_port.cpp y sha256.cpp.
- done-check: ctest en verde · retrobridge_host compila · nombre que no se resuelve: «ERROR: SERVIDOR NO ENCONTRADO» y `menu_tnfs ... not found: ... no se resuelve` en el registro, y otra pulsación vuelve a resolver · cada orden con los flags de OPEN (`tnfs_request`) · SHA-256 al terminar (`menu_fetch`, T012) · causa de la falta de memoria (`menu_tnfs`, T012).
- TDD: rojo (3 de 4 casos de test_tnfs_link fallan con el stub; el registro mezclaba líneas de dos hilos), verde, refactor (se quitó un destructor redundante de UdpTnfsLink).
- humo: `retrobridge_host --images --sources` con un nombre inexistente y sin hub sale con código 1, sin colgarse, y el registro termina con connect, menu_stats y summary.
- risk: tier 1 (`rsc@4.1.7 risk --base 38d1b17`), un revisor (refuter-correctness).
- review: ronda 1 de 3, 0 graves. L14 corregido (onStop también si el hub no contesta; reset del cargador si falla el socket). L15 intencionado: el candado cubre la escritura de cada línea; T015 mira la temporización SIO en una carga larga. L16 intencionado: el cableado de main.cpp se prueba de punta a punta en Altirra (T015); se añadió la prueba de una segunda pulsación tras un nombre que no se resuelve.
- controls: host 59 de 59 · retrobridge_host 1 de 1, 0 avisos con -Werror · layering OK · notices OK · security: el registro no guarda usuario ni contraseña de MOUNT y las rutas se escapan en JSON (JsonlLog) · traceability: AC4, AC7, AC12, M41-6.
- verify (por tarea, corrida nueva antes del commit, árbol 38d1b17 con cambios): tests PASS (host 59 de 59; retrobridge_host 1 de 1) · build PASS (0 avisos con -Werror) · security SUSTITUIDA: sin escáner automático para el host C++; revisión a mano de describeTnfsRequest (límites) y de lo que se anota (sin credenciales de MOUNT) · traceability PASS (AC4, AC7, AC12, M41-6).
- commit: d3246d6 (aprobado 2026-11-18)

## T014 — Documentos (2026-11-18)

- Tier 0 (solo documentación): sin revisor.
- Hecho: ADR-018 nuevo (`docs/adr/0018-cliente-tnfs-de-solo-lectura.md`); ADR-017 enmienda 2 (acción 2, sondeo
  $FE, AND #$3F, fuentes 2 a 8; 11 sectores, pantalla `$0879`-`$0C38`, medido con `make_menu.menu_program()`);
  README con enlaces a ADR-017 y ADR-018; M41-1 a M41-7 en `wiki/sdd/decisions.md`, con enlaces a los ADR.
- Controles: `python -I tools/check_notices.py` OK; 0 emoji en los 4 archivos; enlaces relativos de ADR-018
  existen. Pruebas y build: NO-APLICA (sin código).
- Nota: la cifra `$0C39` del resumen previo era errónea; el último byte de la pantalla es `$0C38`.
- Commit: d83a69f (aprobado por el responsable).

## T015 — Altirra local y carga larga (2026-11-18, hecho)

- Carga larga contra `192.168.1.116` (permiso del responsable): `ARCADE.ATR` montada en D2, sin host caído.
  Registro `D:\rb-e0\m41\t015\host.jsonl`: órdenes MOUNT 1, OPENDIR 6, READDIR 197, CLOSEDIR 6, STAT 180,
  OPEN 1, READ 32767, CLOSE 1; el único OPEN lleva `flags=0x0001 mode=0x0000`. `menu_fetch` 16 776 592 bytes,
  sha256 d2852598...35f1c1, unos 50 s. Captura final del responsable: D2 ARCADE.ATR.
- Sector 65 535: `SECTOR-PROBE.ATR` en D1, `BIG-65535.ATR` en D2; estado SIO $01; volcado `db 0600 L100` de Altirra igual a los 256 bytes de `esperado-sector-65535.txt` (bloque de 32 bytes 66 92 7E 3D ... A3 56, 8 veces). Sin commit: no cambia archivos del repositorio.
- Observación abierta: la lista muestra `5-REVIST.AS` y `7-COMPIL.ADO`; falta saber si el punto viene del servidor.
- Pedidos de experiencia del responsable (fuera de menu-tnfs; cambian M41-2, M41-4 y M41-8, necesitan spec nueva):
  1. cursor movido con flechas o joystick; RETURN o el botón rojo confirman;
  2. elegir una imagen solo guarda su referencia y la monta, sin traerla entera a memoria; los sectores se leen
     del servidor al pedirlos, y una función reinicia y arranca lo que está en D1.

## T016 — Demo contra pulgarcito (2026-11-18, cerrada por el responsable)

- Evidencia en `D:\rb-e0\m41\t016`: 7 capturas y `host.jsonl`.
- Visto en capturas: AC1 (raíz con carpetas `/`), AC2 (subcarpeta `0- Prueba`), AC3 (`D1: ATARI LOGO.ATR (A ARRANCA)`),
  AC5/AC6 (`ERROR: SERVIDOR NO RESPONDE` con D1 conservado), AC7 (`ERROR: SERVIDOR NO ENCONTRADO`), D2 `ARCADE.ATR`.
- Visto en el registro: AC4 (menu_boot D1 ATARI LOGO.ATR; sha256 9f21ef9a...c99c96, igual al de la spec); AC9
  (`/Atari_Chile/ATR` entries=128 shown=20); AC12 (solo MOUNT, OPENDIR, READDIR, CLOSEDIR, STAT, OPEN, READ,
  CLOSE; los 2 OPEN con flags=0x0001 mode=0x0000); ARCADE.ATR sha256 d2852598...35f1c1 = copia local del responsable.
- Falta: captura de «MOSTRANDO 20 DE 128», de «CARGANDO nn%» y del logo «ATARI»; sonda del sector 65 535 de
  ARCADE (su último sector es todo 00, comparación débil; T015 ya probó la ruta con datos distintos); run4 (AC11).
- Captura extra: «MOSTRANDO 20 DE 205» en `/.../OMS_ATR` (AC9, formato del mensaje).
- Cierre: el responsable aprueba el funcionamiento («del funcionamiento actual no me quejo»). Quedan sin captura
  «CARGANDO nn%», el logo «ATARI» (AC4 probado por el registro) y run4 (AC11, sin evidencia en Altirra).
- Pedido nuevo para la spec siguiente: 3. paginación para carpetas con más de 20 `.ATR`.

## PR B (2026-11-19)

- Merge de main acb166a: 2e55f3d (59/59 host, retrobridge_host 1/1, layering y notices OK). Push aprobado.
- PR #44 https://github.com/calaluis/retrobridge-chile/pull/44, cuerpo de `02-DOCS/raw/pr-b-menu-tnfs.md`.
  CI: 8 en verde, 2 omitidos, 0 fallos; MERGEABLE/CLEAN. Casillas de procedencia: las confirma el responsable.
- Siguiente: spec nueva con los pedidos de experiencia (cursor, montar por referencia, paginación).

## Revisión externa del PR #44 (2026-11-19)

- Hallazgos de una revisión externa (ChatGPT), los 4 confirmados contra el código:
  1. ~TnfsLoader no enviaba UMOUNT. Corregido: `TnfsClient::umountWithoutWaiting()` (un envío, sin espera ni
     reintentos) para cada cliente al cerrar. Pruebas: UMOUNT=1 con servidor vivo; 0 envíos sin uso; servidor
     callado: 1 UMOUNT y menos de 100 ms (antes: 3 envíos con esperas).
  2. JsonlLog escapaba UTF-8 como Latin-1. Corregido: secuencias UTF-8 bien formadas (RFC 3629) salen tal cual;
     el resto como `\u00XX`. Pruebas decodifican el JSON: Información/Niño, €, emoji; ANSI, cortada, sobrelarga,
     sustituto y bordes C1, E0 9F, F0 8F, F4 90. Mutante F4 alto 0x90: detectado.
  3. host/README.md actualizado; «Issue #41» cambiado por «spec menu-tnfs, PR #44» (ADR-017, ADR-018,
     decisions.md, cuerpo del PR). El prefijo M41 se mantiene.
  4. AC14 con ARCADE.ATR en D2: pendiente, la hace el responsable en Altirra.
- rsc risk: tier 1. Revisor refuter-correctness, ronda 1: should-fix (UMOUNT bloqueaba hasta 3 x 2-5 s por
  fuente con servidor caído) corregido con la prueba del servidor callado; nit (bordes de la tabla) corregido.
- Controles: 59/59 host, retrobridge_host 1/1, check_notices OK. Commits aprobados: 2eda5fc, 2e59184, 86f376b; push y cuerpo del PR actualizados.
- AC14 cerrado (2026-11-19, host con 86f376b): captura «CARGANDO 45%» durante la carga de ARCADE.ATR; sonda con
  ARCADE.ATR en D2: estado SIO $01 y `db 0600 L100` = 256 bytes 00, igual a `esperado-arcade-65535.txt`.
