# ADR-014 — Contextos de almacenamiento y de unidad con traspaso por mensajes

- Estado: Aprobada (2026-10-23), decisiones H2-12 y H2-14 corregidas
- Contexto: arquitectura v0.2, §3.4 y §5.1

## Contexto

H1 dejó dos obligaciones para H2:

- no cerrar ni reutilizar fuentes vinculadas;
- serializar el montaje y la lectura, porque la marca `attached` no es atómica.

Ejecutar el montaje dentro de la sesión SIO lo serializaría, pero abrir una imagen puede tardar segundos (TNFS/WSS
en el futuro). Durante ese tiempo, un comando del Atari incumpliría t2 (≤ 16 ms) o se perdería.

## Decisión

Hay dos contextos. Cada uno es el **único dueño** de sus recursos, y se comunican solo por mensajes.

| Contexto | Posee | Puede bloquear | Casos de uso |
|---|---|---|---|
| **Almacenamiento** | `ImageRepository` y las fuentes no vinculadas | Sí (segundos) | `MountDiskImage`, `UnmountDiskImage`, `CollectDriveReply` |
| **Unidad** (sesión SIO) | `DriveBay`, las fuentes vinculadas y el bus | No, salvo leer un sector entre ACK y COMPLETE (ADR-005) | `ApplyDriveRequest`, `GetDriveStatus`, `ReadSector` |

- **Montar:**
  1. `MountDiskImage` abre la fuente y valida la cabecera (`DiskImage::open`) en el contexto de almacenamiento.
  2. Envía un `DriveRequest` que transporta el `DiskImage` por movimiento.
  3. `ApplyDriveRequest` lo aplica en O(1), entre transacciones SIO, y nunca usa el repositorio.
- **Devolución de fuentes:** toda fuente que deba cerrarse vuelve **una sola vez**, ya desvinculada, en el
  `DriveReply`:
  - `displaced`: la fuente del disco reemplazado o desmontado;
  - `rejected`: la de un montaje que no pudo aplicarse.

  `CollectDriveReply` las cierra en el contexto de almacenamiento.
- **Puerto `DriveMailbox`:** admite un solo intercambio en curso (`Idle → Requested → Processing → Replied →
  Idle`), así que una respuesta nunca se descarta.
  - `submit()` solo consume la solicitud si tiene éxito.
  - Con `MailboxBusy`, quien llama conserva el `DiskImage` y cierra su fuente.
- **`ImageRepository`:** su interfaz pública no virtual aplica `RB_ASSERT` sobre las fuentes vinculadas (H2-11).
- **Bucle de la sesión (H2-14, H2c):** `waitCommandFrame` usa un timeout de 20 ms, de modo que el buzón se atiende
  entre transacciones.

Dimensionamiento que se desprende: para reemplazar un disco sin desmontarlo antes, el repositorio necesita al menos
**unidades + 1** slots.

## Alternativas descartadas

- **Montaje dentro de la sesión SIO:** bloquea el bus durante la apertura.
- **Mutex sobre `DriveBay`:** la sesión esperaría el lock mientras dura la apertura, con el mismo problema de t2, y
  además agrega sincronización al código puro.

## Limitación en H2

Los dos contextos se **simulan en un solo hilo** con `FakeDriveMailbox`. La concurrencia real, el orden y la
visibilidad de memoria del traspaso dependen de la implementación del buzón en infraestructura (cola RTOS, H3/H5).

## Criterios de aceptación para H5

1. Aplicar un montaje o desmontaje en el contexto de la unidad tarda ≤ 100 µs (medido).
2. Con un montaje que tarda ≥ 3 s en el contexto de almacenamiento, ningún comando SIO se pierde y el ACK sigue en
   ≤ 1 ms (analizador lógico).
3. El traspaso entre contextos usa una cola con garantías de orden y de visibilidad de memoria. No se comparte
   ningún objeto sin traspaso.
4. Un COMMAND↓ que llega mientras se aplica una solicitud se atiende al terminar: la ISR registra la marca de tiempo
   y el UART guarda los bytes en su búfer.

## Verificación en H2a

`test_drive_lifecycle`, `test_drive_mailbox` y `test_port_contracts` comprueban:

- que el contexto de la unidad no usa el repositorio;
- que no hay fuentes perdidas ni cerradas dos veces en los montajes cancelados (cabecera inválida, buzón ocupado,
  unidad no configurada);
- que un slot vinculado no se reutiliza;
- con pruebas que esperan un aborto: `close` sobre una fuente vinculada y `putReply` sin una solicitud tomada.
