# Multijugador por LAN

El modo LAN conecta dos equipos de la misma red local mediante TCP y usa el cable link de mGBA para mantener sincronizada la partida.

## Preparación

- Los dos equipos deben ejecutar una versión compatible del launcher y tener **el mismo título y exactamente la misma ROM**.
- El modo LAN admite una sala con un anfitrión y un jugador que se une (dos jugadores en total).
- El puerto TCP predeterminado es **5738**. Se puede cambiar en **Ajustes → Multijugador LAN → Puerto TCP común**. El anfitrión y el invitado deben usar el mismo número.
- La contraseña debe tener entre **8 y 63 caracteres ASCII**. No se guarda en el archivo de configuración.

## Crear y entrar a una sala

1. En ambos equipos, abrí **Ajustes → Multijugador LAN** y configurá el mismo puerto y contraseña.
2. En el equipo anfitrión, elegí **Crear sala**. Compartí con el otro jugador la IPv4 local que muestra la pantalla y el puerto.
3. En el equipo invitado, escribí la IPv4 del anfitrión en **IP del anfitrión** y elegí **Conectarse a sala**.
4. Esperá a que ambos equipos muestren **Conexión LAN establecida**.
5. Los dos jugadores deben iniciar el mismo título. El anfitrión controla J1 y el equipo invitado controla J2.

Cada equipo ejecuta localmente los dos núcleos mGBA que forman el cable link. Antes de avanzar cada fotograma, los núcleos locales esperan a que se intercambien las entradas de ambos jugadores. Esto mantiene la misma asignación J1/J2 en las dos pantallas.

## Firewall y red Wi-Fi

La conexión es directa dentro de la intranet; no utiliza servidores externos, descubrimiento automático, retransmisión por Internet ni reenvío de puertos del router.

- **Windows:** cuando el Firewall de Windows pregunte, permití el launcher en redes privadas. Si no aparece el aviso, agregá una regla de entrada TCP para el puerto configurado (5738 por defecto). No es necesario habilitar redes públicas.
- **macOS:** permití conexiones entrantes para el launcher en los ajustes del firewall.
- **Linux:** abrí el puerto TCP configurado en el firewall. Con UFW y el puerto predeterminado, por ejemplo: `sudo ufw allow 5738/tcp`.
- **Android:** concedé el permiso de red incluido en la aplicación y conectá ambos equipos a la misma red Wi-Fi.

Los dos dispositivos deben estar en la misma red local y poder comunicarse entre sí. Las redes de invitados y las opciones del router llamadas *AP isolation*, *client isolation* o *aislamiento inalámbrico* suelen impedir que los equipos se vean: desactivalas para esa red. Una VPN o un firewall local también puede bloquear la conexión. Si falla, verificá la IPv4 del anfitrión, el puerto y que el firewall permita el tráfico TCP entrante.

## Contraseña y seguridad

La contraseña **no se transmite en texto claro**. La sala usa un desafío con números aleatorios, una derivación PBKDF2-HMAC-SHA1 (60.000 iteraciones) y pruebas HMAC para verificar al jugador y autenticar los paquetes de entrada. También se comprueba el título y la firma de la ROM antes de aceptar la sala.

El tráfico de juego **no está cifrado**. Usá esta función solo en una red local de confianza y elegí una contraseña larga que no reutilices en otros servicios. La autenticación de los paquetes evita modificaciones casuales o de terceros que no conozcan la contraseña, pero no sustituye el cifrado de una VPN o de una red segura.

## Límites actuales y compatibilidad

- Durante una partida LAN, la pausa local y los guardados/cargas rápidos se deshabilitan para evitar que un equipo avance o restaure un estado distinto al del otro.
- Si se pierde la conexión, la partida se cierra y el launcher vuelve al menú.
- El modo individual y el multijugador local existente (incluidas las opciones locales de Sonic Battle) siguen disponibles.
- LAN está disponible en las compilaciones nativas de escritorio y Android. La versión web y la compilación PS4 no exponen sockets LAN y conservan sus modos actuales.
