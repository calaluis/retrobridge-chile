# Solicitud de revisión independiente — PR #124, Issue #125, medición física con `3863461`

Por favor revisa la medición publicada en https://github.com/calaluis/retrobridge-chile/pull/124#issuecomment-7230570625 y el commit `f6985b2` (sección «Medición física con `3863461`» de 02-DOCS/wiki/ftd/cinta-data-in-65xe.md).

Preguntas concretas:
1. ¿Los contadores (`ordenes 1`, `tramas 22 cortas 16 command_pegado 4803`, cero fallos de envío y UART) bastan para afirmar que en esta ocurrencia la orden de montar no llegó completa y que la placa no falló al enviar? Para revisarlo, mira `sio_line_transport.cpp:30-45` (tramas cortas y COMMAND pegado) y `menu_sio_transport.cpp` (`serveMenu`).
2. ¿Está bien descartar H1 (TX sin terminar o UART no lista) solo para esta ocurrencia, manteniendo abierta la de `60ac6e4` (`err_tx 3`)?
3. ¿La hipótesis nueva (el préstamo de DATA IN al montar la cinta afecta la recepción de la orden siguiente) es coherente con el código del árbitro de DATA IN en `main/menu_sio.cpp`? ¿Qué medición mínima la confirmaría o la descartaría sin cambiar el protocolo de pantalla?
4. El pin +5V del SIO marcaba 4780 mV y no salió el resumen de apagado, aunque el 65XE se indicó como apagado. ¿Cómo debería tratarse este dato antes de seguir?

Límites: no hay cambio de código en `f6985b2`. La placa tiene `3863461`. H3, H4 y GPIO36 quedan fuera de alcance. Esta revisión no autoriza flasheo, COM3, cambios eléctricos ni merge.
