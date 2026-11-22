# T5 Break en Altirra — 2026-11-22

Plan: `menu-unidades-persistentes`, tarea T5 (criterios C7 y C11).

## Montaje

- `retrobridge_host` compilado desde `feat/menu-unidades-persistentes` en `57f1ec7` más los cambios de T5 sin commit.
- `--images` con las imágenes de `make_atr.py --demo`; fuente 2: `tnfs.calaluis.cl`.
- Hub NetSIO de laboratorio (`netsiohub`) y Altirra 4.31 (XL NTSC, 128K). Un solo host en el hub.
- Teclas enviadas a Altirra con SendKeys (`{BREAK}` es la tecla Break). Capturas, `host.jsonl` y scripts en
  `D:\rb-e0\m51-break` (fuera del repositorio).

## C7: Break vuelve a la pantalla principal

D2 tenía `DEMO-UNO.ATR` antes de cada Break (D3, `DEMO-DOS.ATR`, desde C11).

| Desde | Captura | Resultado |
| --- | --- | --- |
| Lista de la fuente 1 | `03-break-desde-lista.png` | Pantalla principal, D2 igual |
| Tercer nivel TNFS `/Aportes_Juan_Larrain/3-Juegos/ATR` | `10-break-desde-nivel3.png` | Pantalla principal, D2 igual |
| Reabrir la fuente 2 tras Break | `11-reabrir-raiz.png` | `FUENTE 2: /` (la pila de carpetas se vació) |
| Selección de unidad de la fuente 1 | `16-break-desde-pick.png` | Pantalla principal, nada asignado |
| Selección de unidad de una imagen en el cuarto nivel `/…/2-Demos/ATR/AB` | `21-break-desde-folderpick.png` | Pantalla principal, nada traído ni montado |

`host.jsonl` registra `menu_break: back to the main screen` en cada caso y ningún `menu_mount` ni `menu_fetch`
nuevo.

## C11: Break durante un montaje

Lista de la fuente 1, `A` (`DEMO-DOS.ATR`), `3` y Break 120 ms después. Tramas `K` recibidas por el host:

```text
04:07:11.383  K aux1=$1A aux2=$80 (seq 4)   orden original
04:07:11.391  menu_select: D3 name=demo-dos.atr accepted
04:07:11.533  menu_mount: D3=mounted
04:07:11.533  K aux1=$1A aux2=$80 (seq 4)   repetida, idéntica
04:07:12.101  K aux1=$1A aux2=$80 (seq 4)   repetida, idéntica
04:07:12.701  K aux1=$F0 aux2=$A0 (seq 5)   Break como tecla nueva
```

- La orden llegó tres veces con el mismo AUX1 y AUX2. El host aplicó el montaje **una sola vez** (un
  `menu_select` y un `menu_mount` de D3 en todo el registro).
- Después llegó Break (`$F0`) con la secuencia siguiente.
- Pantalla final (`14-break-durante-montaje.png`): principal, D3 `DEMO-DOS.ATR`, sin `ERROR: EL HOST NO RESPONDE`.
- Observación: hubo dos repeticiones y no una. Las dos tienen la misma secuencia, así que el host respondió sin
  volver a aplicar nada. No se aisló si la segunda es otro aborto por Break o un reintento propio de SIOV.
