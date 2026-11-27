---
author: Calaluis
branch: fix/cinta-data-in-65xe
status: in-progress
---
# La cinta no carga en el 65XE: la toma de DATA IN vence (T9b, depuración)

## Intent

Que `international_karate.cas` cargue en el 65XE con `menu-sio-cinta`. Primero, medir por qué no carga.

## Evidencia de la prueba física (2026-11-26, `menu-sio-cinta` `47e5f56`)

- Menú en el 65XE: sí. `C: INTERNATIONAL_KARATE.CAS` montada. Arranque con START+OPTION: BOOT ERROR.
- Consola: `cambio 1` (TakeRequested) → `cambio 4` (TakeTimedOut) → `cambio 6` (ToDisk), 369 veces en 47 s.
  La toma vence a los 20 ms (`kTakeTimeoutUs`). Después, una toma se confirmó y la cinta llegó al
  segmento 36 de 213; con MOTOR apagado no soltó DATA IN, y el menú dijo «EL HOST NO RESPONDE».
- `rmt: GPIO 21 is not usable, maybe conflict with others`: aviso de ESP-IDF por el pin ya reservado por la
  UART; `rmt_new_tx_channel` no falló. No se trata como causa.
- Logs fuera del repo: `D:\GitHub\Respaldos\FujiNet-RB-menu-sio-cinta-47e5f56\consola-*.log`.

## Scope

- Paso 1 (este commit): diagnóstico que solo registra. `EspDataIn::hardwareFree()` guarda qué condición
  estaba ocupada (bits) y cuántas veces dijo que no; `CassetteService` guarda el último tick; `menu_sio`
  registra cada toma (confirmada o vencida), y cada 2 s mientras la línea no es del disco.
- Paso 2: con la medición, prueba RED en el host y corrección.

Fuera: H4, las puertas y los paquetes. Flashear y probar en el 65XE necesitan aprobación aparte.

## Checklist

- [x] Rama `fix/cinta-data-in-65xe` desde `main` `47e5f56` — prueba: `git log`.
- [x] Diagnóstico sin cambios de decisión — prueba: mismo orden de condiciones en `hardwareFree()`;
  atómicos de 32 bits (sin libatomic).
- [ ] CI del diagnóstico y paquete `menu-sio-cinta`.
- [ ] Medición en el 65XE (con aprobación).
- [ ] RED → GREEN de la causa y corrección.

## Next

CI del diagnóstico. Después, con aprobación, instalarlo y repetir el arranque de la cinta.
