# ADR-019 — Escritura de sectores desde el Atari

- Estado: Propuesta (2026-11-19), spec y plan `menu-escritura-disco` (`02-DOCS/wiki/sdd/specs/menu-escritura-disco.md`,
  `02-DOCS/wiki/sdd/plans/menu-escritura-disco.md`). Avance y evidencia en
  `02-DOCS/wiki/sdd/progress/menu-escritura-disco.md`.
- Contexto: [ADR-016](0016-transporte-netsio.md) dejó todas las unidades en solo lectura: `write_size` 0 y la `09`
  respondida vacía. La spec `menu-escritura-disco` pide que el DOS del Atari guarde archivos en una ATR local.
- Reemplaza en ADR-016: la frase «`write_size` vale 0: unidad de solo lectura» y la respuesta vacía a la `09`.

## Contexto

- FEP-003 define la `09` (dato más sincronización de escritura) pero no fija el valor de `write_size`. Se midió con
  Altirra (W001, carpeta `D:\rb-e0\m45-w001`): la `81` que acepta la orden lleva `write_size` igual al tamaño del
  sector más 1 (el checksum). Con 0, Altirra no envía datos.
- La ruta SIO no reserva memoria ([ADR-011](0011-memoria-estatica-en-ruta-sio.md)). El búfer de datos es fijo.
- Las fuentes TNFS se cargan en memoria y son de solo lectura ([ADR-018](0018-cliente-tnfs-de-solo-lectura.md)).

## Decisión

1. **Secuencia NetSIO de escritura.** Para `W` (`$57`) y `P` (`$50`) en una unidad que acepta escrituras:
   1. El ACK de la orden va en `81 <sync>` con `write_size` = tamaño del sector + 1, en little endian.
   2. El Atari envía los datos con `01`/`02` y cierra con `09 <checksum> <sync>`. El transporte junta hasta
      `write_size` bytes; el resto se cuenta y se descarta.
   3. La sincronización de la `09` se contesta con el ACK o NAK de los datos (checksum SIO).
   4. Tras la escritura, COMPLETE o ERROR con t5 de 300 µs (mínimo 250 µs).
   5. Sin datos en 1 s, o con una nueva orden (`11`) o un reinicio, la recepción se abandona.
2. **Quién escribe.** Solo `retrobridge_host` con `--images` acepta escrituras (`allowWrites`). La escritura va a la
   ATR, en su sitio. `--image` y las fuentes TNFS siguen de solo lectura.
3. **Protección.** Una ATR con el atributo de solo lectura del sistema queda protegida: STATUS devuelve `$BF` en el
   byte 1 (bit de protección del FDC) y cada `W`/`P` responde ERROR. El menú marca esa unidad con « SL».
4. **STATUS tras un fallo.** Los bits D1 a D3 del byte 0 dicen la causa del último fallo: D1 checksum de datos
   malo, D2 escritura fallida, D3 rechazo por protección. Duran hasta la siguiente `R`/`W`/`P` aceptada; un STATUS
   no los borra. Un rechazo por protección no activa `$EF`.
5. **Un solo escritor.** Una ATR solo la escribe un host. Las unidades de Altirra que sirve el host van en Off.
6. **Registro.** Cada escritura deja un evento JSONL `sio_write`: unidad, orden, sector, resultado
   (`complete`/`error`/`nak`), CRC-32 de los datos y, si falla, la causa (`no_disk`, `protected`, `io` o `no_drive`). Si la
   respuesta final no sale, el evento lleva `reply=lost`.

## Consecuencias

- El DOS del Atari puede guardar archivos en D1 a D8. La demo W006 guardó `PRUEBA.BAS`, que siguió tras relanzar
  el host; la ATR protegida no cambió su SHA-256.
- La causa del fallo es una clase, no un `errno`.
- Tras un error, el DOS reintenta por su cuenta (en la demo, 112 `W` al sector 0, todos con NAK). Es conducta del
  DOS, no del host.
- Dos hosts sobre la misma ATR, o Altirra con la unidad activa, pueden corromperla. No hay bloqueo de archivo.

## Enmienda 1 (2026-11-20, spec `menu-formatear-disco`): formateo

Contexto: el DOS del Atari formatea un disco antes de usarlo. Fuente: *Altirra Hardware Reference Manual*, §10.2
«Format ($21)» y la sección del 1050 «Format Medium Density command» ($22).

1. **Órdenes.** `$21` formatea en densidad simple y `$22` en densidad media. Solo con `allowWrites`; sin ella, NAK
   como antes. AUX no se mira.
2. **Densidad.** `$21` vale solo para una ATR de 720 sectores de 128 bytes; `$22`, solo para una de 1040 de 128.
   Otra geometría (incluida la doble densidad) responde ERROR y no toca el archivo.
3. **Ejecución.** El host escribe `$00` en cada sector antes de COMPLETE. La orden va por el camino de lectura:
   ACK, ejecución, COMPLETE o ERROR, y siempre una trama del tamaño del sector.
4. **Respuesta.** La trama lleva `$FF` en todos sus bytes: la lista de sectores malos está vacía y termina en
   `$FFFF`. El resto en `$FF` es elección propia (M46-4): el manual no dice qué va después del terminador.
5. **STATUS tras un fallo.** Como la escritura: D3 si la unidad está protegida, D2 en cualquier otro fallo
   (densidad, E/S, unidad vacía).
6. **Registro.** `sio_write` con `cmd=F` (`$21`) o `cmd=FM` (`$22`), sin sector ni CRC-32. Causa nueva: `density`.

Consecuencias: el DOS 2.5 formatea en simple (`P`) y en media (`I`); la demo F005 confirmó que `I` manda `$22` y que
después el DOS escribe el sector 1024. Con la densidad equivocada, el DOS reintenta (8 órdenes en la demo) y
muestra su error. Un fallo de E/S a mitad deja en cero los sectores ya escritos.
