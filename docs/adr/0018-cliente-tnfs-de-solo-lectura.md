# ADR-018 — Cliente TNFS de solo lectura y carga en memoria

- Estado: Propuesta (2026-11-18), spec y plan `menu-tnfs` (`02-DOCS/wiki/sdd/specs/menu-tnfs.md`,
  `02-DOCS/wiki/sdd/plans/menu-tnfs.md`). Decisiones M41-1 a M41-21 en `02-DOCS/wiki/sdd/decisions.md`.
- Contexto: [ADR-017](0017-menu-de-arranque.md), enmienda 1, dejó las fuentes 2 a 8 en `PENDIENTE`: abrirlas
  necesitaba un cliente TNFS propio. La spec `menu-tnfs` pide navegar esas fuentes desde el Atari y montar sus imágenes.

## Contexto

- El menú es un terminal delgado: cada tecla es una transacción SIO que el host contesta con 962 bytes. Un
  `SIOV` espera la respuesta unos segundos; una red lenta o caída no puede bloquear esa respuesta.
- La ruta SIO no reserva memoria ([ADR-011](0011-memoria-estatica-en-ruta-sio.md)) y cada origen se cierra una
  sola vez ([ADR-014](0014-contextos-almacenamiento-y-unidad.md)).
- El código es propio, escrito desde documentación pública registrada en [docs/fuentes.md](../fuentes.md)
  ([ADR-012](0012-desarrollo-independiente.md)). No se leen otras implementaciones de TNFS.
- RetroBridge no debe escribir en un servidor TNFS ajeno.

## Decisión

1. **Cliente propio de solo lectura (M41-4).** La biblioteca `rb_net_tnfs` solo puede codificar las 9 órdenes de
   lectura (MOUNT, UMOUNT, OPENDIR, READDIR, CLOSEDIR, STAT, OPEN, READ, CLOSE). La única apertura es
   `openReadOnly`, con indicadores fijos 0x0001 y modo 0; no existe un parámetro de indicadores. Esta es la
   garantía estructural de que el host no escribe en el servidor.
2. **Red fuera del hilo SIO (M41-1).** `TnfsLoader` corre en un hilo propio con un trabajo a la vez, número de
   trabajo y cancelación. `key()` espera hasta 200 ms (`kLoadWaitMicros`) y, si el trabajo no terminó, contesta
   con la acción 2 «esperar» ([ADR-017](0017-menu-de-arranque.md), enmienda 2). Un resultado tardío o reemplazado
   se descarta.
3. **Imagen entera en memoria (M41-2, M41-18).** Una imagen TNFS se trae completa a uno de 9 huecos de
   `MemoryImageStore` y se abre como `*tnfs*0` a `*tnfs*8`. Después se valida y se monta como cualquier imagen.
   `ImageLocation`, `rb_application` y `rb_domain` no cambian.
4. **Devolución de huecos (M41-20).** El menú no libera huecos: los entrega con `MenuLoader::recycle`. El hilo de
   carga los libera antes de cada trabajo; un hueco todavía abierto o adjunto conserva su marca.
5. **Errores visibles, disco intacto (M41-6, M41-21).** Servidor sin respuesta, nombre no resuelto o falta de
   memoria se muestran como `ERROR: SERVIDOR NO RESPONDE`, `ERROR: SERVIDOR NO ENCONTRADO` o
   `ERROR: NO SE PUDO ABRIR`. La causa queda en el registro del host y el disco anterior se conserva.
6. **Registro de cada orden (AC12).** `TnfsTracePort` escribe en el registro JSONL cada orden enviada, con los
   indicadores y el modo de cada OPEN; al terminar una carga, el SHA-256 de la imagen. El usuario y la clave de
   MOUNT no se registran.

## Consecuencias

- Una imagen grande ocupa memoria del PC: con 9 huecos de 16 MB, hasta unos 144 MB.
- El servidor falso de las pruebas (`FakeTnfsServer`) no usa `TnfsCodec` (M41-5), así que un error del códec no
  se esconde detrás de sí mismo.
- La demo lee por SIO el sector 65 535 de `ARCADE.ATR` en D2 y compara sus 256 bytes con la imagen descargada
  (M41-7).
- Escribir en un servidor TNFS exige un ADR nuevo.
