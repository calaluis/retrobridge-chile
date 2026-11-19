# Borrador de licencia de uso personal para RetroBridge Chile

Propuesta para la primera versión pública. La licencia que rige este repositorio privado figura en [LICENSE](../LICENSE) y en [ADR-013](adr/0013-licencias-separadas.md).

## Texto propuesto para la versión pública

**Copyright © 2026 Luis Rodrigo Carrasco Lagos.**

### Alcance

Estas condiciones se aplican al código, al firmware y a la documentación propios de RetroBridge Chile identificados bajo esta licencia. Cada componente de terceros se rige por su propia licencia, indicada en `THIRD_PARTY_NOTICES.md`.

### Permiso de uso personal

El titular autoriza a las personas naturales a descargar, estudiar, compilar, modificar, instalar y ejecutar el software en equipos y emuladores para su propio uso personal y sin fines comerciales. Este permiso incluye guardar copias de respaldo para ese uso.

### Contribuciones al proyecto

Se permite ver y bifurcar el repositorio en GitHub conforme a los términos del servicio, y modificar y compartir esas copias en GitHub para proponer aportes mediante pull requests al repositorio oficial. Conserva los avisos de copyright y de licencias aplicables en esas copias.

La incorporación de cambios a la rama oficial requiere la aprobación de `@calaluis`.

### Usos sujetos a autorización

El titular se reserva la redistribución del código o del firmware fuera del permiso anterior y su explotación comercial, salvo los actos permitidos por la ley. Tales usos requieren autorización previa y por escrito.

La explotación comercial comprende la venta del código o firmware, su distribución como parte de un equipo que se venda o arriende y su uso para prestar servicios remunerados de instalación, integración, mantenimiento o soporte. Este requisito también se aplica cuando se entrega el firmware sin un cargo separado como parte de una venta o de un servicio pagado.

Para solicitar una autorización, contacta a [@calaluis](https://github.com/calaluis). El acuerdo comercial indicará los usos autorizados y sus condiciones.

### Cumplimiento

El titular se reserva las acciones que le reconozca la legislación aplicable frente a usos fuera de los permisos concedidos.

## Preparación de la publicación

- Preparar un repositorio nuevo desde una copia revisada del código autorizado. El aviso público identifica al titular por su nombre y por `@calaluis`. El historial y la identificación privada quedan en el repositorio interno.
- Incorporar el texto aprobado como `LICENSE` de la copia pública. Alinear su README y los documentos que describan la licencia. Conservar los avisos de terceros.
- Definir cómo se obtendrá el permiso para licenciar comercialmente aportes de terceros cuando sea necesario. La aprobación de un PR y la autorización comercial de un aporte son decisiones distintas.
- Configurar `.github/CODEOWNERS` con `* @calaluis`. En el repositorio de publicación, exigir pull request, revisión del Code Owner y comprobaciones de CI para `main`.

## Referencias

- [Términos de GitHub sobre repositorios públicos y contribuciones](https://docs.github.com/en/site-policy/github-terms/github-terms-of-service)
- [Ley chilena 17.336, artículos 19, 20 y 85 B](https://www.bcn.cl/leychile/navegar?idNorma=28933)
