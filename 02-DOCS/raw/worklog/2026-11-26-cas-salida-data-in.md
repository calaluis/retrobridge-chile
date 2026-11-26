# 2026-11-26 — cas-salida-data-in (PR #123)

- Qué: la cinta sale por DATA IN solo en el paquete menu-sio-cinta, con la opción CONFIG_RB_MENU_SIO_TAPE_OUTPUT.
- Por qué: el responsable aceptó R2, R6 y R7 sin medición para su FN32ROV-1.3-Q24. La ronda 1 de revisión pidió
  alcance opt-in (H1) y puertas fail-closed con MOTOR y DATA IN listos (H2).
- Archivos: rb_board (validate_for_menu_cassette_output), main/menu_sio.cpp, main/tape_gates.hpp, Kconfig,
  sdkconfig.menu-sio-cinta, firmware_package.py, ci.yml, registro de aceptación.
- Resultado: ctest 108/108, tools 340 OK, 4 mutaciones caen. Commit a3249e0. CI en curso.
- Decisión: la aceptación de DATA IN es de una placa; va en una opción de compilación, no en el perfil genérico.
- Siguiente: ronda 2 de revisión. Flashear menu-sio-cinta y probar un .cas en el 65XE solo con aprobación.
