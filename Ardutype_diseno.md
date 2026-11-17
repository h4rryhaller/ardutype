# Ardutype: documento de diseño

Versión 2, 20 de septiembre de 2026. La versión 1 sustituyó las suposiciones del `leeme.md` sobre la placa (el Pro Mini no puede ser un teclado USB). Esta versión 2 cambia de nuevo la placa: se descarta el USB-A integrado (poco cómodo para manejar el dispositivo mientras se usa) a favor de un **cable USB-C**, y con ese cambio de requisito la placa pasa de la Waveshare RP2040-One a la **Adafruit KB2040**.

## 1. Decisión y resumen

Un dispositivo con pantalla, conectado por cable USB-C, que guarda contraseñas cifradas, se desbloquea con una contraseña maestra y las teclea en el ordenador como si fuera un teclado.

- **Placa:** Adafruit KB2040, con conector USB-C (para cable, no integrado como enchufe).
- **Pantalla:** OLED SSD1306 de 0,91" (128×32), I2C.
- **Entrada:** un **joystick táctil de 5 direcciones** (arriba, abajo, izquierda, derecha y pulsación central), en versión con patillas para no soldar SMD. Pulsación larga para funciones extra.
- **Programación:** arrastrando un fichero UF2 por USB. Ya no hacen falta ni el conversor PL2303 ni el Pro Mini.

## 2. Confirmación de la placa (según la ficha y el pinout de Adafruit)

Comprobado contra la página del producto y la guía de pinout de Adafruit:

- RP2040, doble núcleo Cortex-M0+ a 133 MHz, **8 MB de flash** (el doble que la RP2040-One), USB con soporte de dispositivo.
- Conector **USB-C**, pensado para cable, no para enchufar la placa directamente.
- Forma "Pro Micro" con pines en los dos bordes largos, pensada explícitamente por Adafruit para teclados y macropads.
- **18 GPIO en los bordes + 2 más en el conector STEMMA QT** (20 en total).
- Dimensiones: **35,0 × 17,8 mm**, prácticamente el mismo tamaño que la RP2040-One.
- Lleva un **LED RGB NeoPixel** de estado de fábrica, cerca del pin CLK. No he podido confirmar su número de GPIO exacto ni cómo se referencia desde Arduino (en CircuitPython es `board.NEOPIXEL`) — verificar en el pinout antes de programarlo.
- Botones **RESET y BOOT** accesibles físicamente en la placa.
- Conector **STEMMA QT** integrado para I2C sin soldar, aunque nuestro OLED (DollaTek, de pines) se cableará directo a los pines, no por ese conector.

**Confirmado (guía oficial de pinout de Adafruit, learn.adafruit.com/adafruit-kb2040/pinouts):** RESET y BOOT están junto al conector USB-C (hay que dejar libre ese extremo); el NeoPixel RGB está en el extremo opuesto, junto a la etiqueta CLK; además hay un LED verde de alimentación justo encima del USB-C. Falta solo medir con el calibre la anchura exacta de la zona de RESET/BOOT al recibir la placa.

### Pines libres que usaremos
Del pinout confirmado:
- Borde con D0-D9 (D0/TX y D1/RX libres de sobra).
- Borde con A0-A3, CLK, MISO, MOSI, D10, y **D12 (SDA)** / **D13 (SCL)**, que son también el STEMMA QT.
- 3V3, GND, RAW (5V) y VBUS en los extremos.

Del bus I2C: **D12 es SDA y D13 es SCL**, confirmado en la guía de pinout de Adafruit.

## 3. Conexiones

| Señal | Pin de la KB2040 | Notas |
|---|---|---|
| OLED VCC | 3V3 | El módulo acepta 3,3-6 V |
| OLED GND | GND | |
| OLED SDA | TX (GPIO0, I2C0 SDA) | Dirección I2C típica: 0x3C |
| OLED SCL | RX (GPIO1, I2C0 SCL) | |
| Joystick ARRIBA | D2 | A GND (común), con `INPUT_PULLUP` |
| Joystick DERECHA | D3 | idem |
| Joystick IZQUIERDA | D4 | idem |
| Joystick ABAJO | D5 | idem |
| Joystick PULSACIÓN CENTRAL | D6 | idem |
| Joystick COMÚN | GND | |
| Zumbador pasivo | D8 (GPIO8) → resistencia → zumbador → GND | Pomodoro (2026-11-15). Ver la nota de corriente en la fase 9 |
| LED de estado | NeoPixel de la placa (GPIO17 según el core; sin confirmar en la placa) | Ya soldado en la placa |

**Mapeo del joystick cableado y comprobado con el polímetro (2026-10-25):** D2 arriba, D3 derecha, D4 izquierda, D5 abajo, D6 centro. La KB2040 va soldada con sus pines a la PCB (sin zócalo). Sketch de prueba: `prueba_hardware/`.

**Corrección (2026-10-25):** D12/D13 (GPIO12/13) no salen por el borde de la placa, solo por el conector STEMMA QT. El OLED va a **TX (GPIO0) = SDA** y **RX (GPIO1) = SCL**, que en el RP2040 son también I2C0; en el firmware, `Wire.setSDA(0); Wire.setSCL(1);`. La herramienta de administración irá por el USB (CDC), así que TX/RX no hacen falta para nada más.

Con esto cuadra: usamos 7 GPIO de los 20 disponibles. Quedan 13 libres, entre ellos D0/D1 (TX/RX) para la futura herramienta de administración por USB serie, y A0-A3 y SPI por si hace falta el ATECC608 de la fase 2 (ese chip es I2C, así que compartiría D12/D13).

### Distribución en la PCB (80 × 20 mm, recortable)
Medidas (KB2040 y OLED medidos con calibre, 2026-10-02): PCB 80 × 20 mm (recortable), OLED 38 × 12 mm, joystick de 10 × 10 mm (elegido), **KB2040 de 33 × 17 mm** (cuerpo sin el saliente del USB-C; la ficha da 35,0 × 17,8). En anchura todo cabe en los 20 mm de la PCB (KB2040 17, OLED 12).

> **REDISEÑO 2026-10-02 — montaje plano coplanar (elegido; sustituye al sándwich de dos capas de abajo).**
> El montaje en dos capas (OLED sobre la KB2040 con Kapton) se descarta por ser complicado de montar. Nuevo plan: **todo plano sobre la misma cara de una única PCB de prototipo de 60 × 40 mm**, coplanar:
> - KB2040 soldada plana, USB-C saliendo por un borde.
> - OLED al lado (no encima), en el mismo plano.
> - Joystick de 5 direcciones al lado (a la espera de que llegue; en una foto de prueba se puso un interruptor suelto solo para ver el formato).
> Ventajas: no hay que desoldar la cabecera del OLED ni aislar con Kapton; **RESET, BOOT y el NeoPixel quedan accesibles/visibles** (se puede recuperar el NeoPixel como indicador de estado, y desaparece el problema de medir la zona libre de 8-10 mm); todo el cableado a la vista. Precio: más superficie (60 × 40 en vez de un "palo" de ~56 mm), pero al usuario le gusta el formato. Pendiente: distribución exacta de las piezas en los 60 × 40 cuando llegue el joystick. Pines sin cambios (sección 4). El resto de esta sección (sándwich) queda como referencia histórica.

**Diferencia clave frente al diseño con la RP2040-One:** el USB-C de la KB2040 es un conector hembra montado en la propia placa, no un borde que hace de enchufe. El cable sale por ese extremo sin añadir longitud al dispositivo, así que ya no hay que sumar los 10 mm que antes ocupaba el conector.

**Opción A, en línea:** KB2040 33 + OLED 38 + joystick 10, unos 81 mm en total — justo en el límite de la PCB de 80 mm (habría que recortar o apurar).

**Opción B, en dos capas (elegida).** El OLED va sobre la KB2040, sin su cabecera de pines, con aislante entre capas. Medido desde el extremo del USB-C:

- Una zona inicial de **~8-10 mm (estimado; confirmar con el calibre al recibir la placa)** debe quedar libre junto al USB-C para RESET y BOOT, ahora confirmados en ese extremo por la guía de pinout de Adafruit.
- OLED, desde ese punto y a lo largo de 38 mm — **como la KB2040 mide solo 33 mm (medido), el OLED sobresaldrá unos 13-15 mm más allá del extremo de la placa** (8-10 mm de zona libre + 38 de OLED − 33 de placa), apoyado sobre su propia PCB de soporte, no sobre la KB2040.
- **NeoPixel sacrificado (decisión del usuario):** el NeoPixel está en el extremo opuesto al USB-C, junto a CLK, y queda tapado por el OLED en este montaje. Se renuncia al indicador de estado por NeoPixel — el OLED ya muestra esa información (ver sección 8). Puede que se cuele algo de luz por el lateral, ya que el OLED no queda perfectamente pegado a la KB2040 (holgura del aislante y de los pines cortados a ras), pero no es algo de lo que depender.
- Joystick, justo después del OLED, otros 10 mm.
- **Total estimado: unos 55-58 mm**, bastante más corto que los ~70 mm de la opción con la RP2040-One, porque ya no se suma el conector.
- El ancho de 20 mm sigue sobrando (OLED 12 mm).

Cuidados en la opción B:

- **BOOT accesible.** Si el OLED tapa la zona de RESET/BOOT no podrás recuperar el dispositivo si el firmware se cuelga. Igual que antes, el core arduino-pico permite entrar en modo de carga por software casi siempre, pero conviene un acceso físico de reserva.
- **Aislamiento.** Cinta Kapton o una lámina fina de plástico entre el OLED y la KB2040, cuyos bordes dejan pines al aire. Termorretráctil en los cables.
- **Desoldar la cabecera del OLED** con malla desoldadora, sin sobrecalentar (el cristal es frágil). Alternativa: cortar los pines a ras y soldar cables finos.
- **Apoyo** bajo la parte del OLED y del joystick que sobresalen del cuerpo de la KB2040 (separadores, o una segunda PCB de soporte pegada).
- **Grosor:** lo marcarán el joystick y la separación entre capas. Medir cuando tengas las piezas.
- **Cable USB-C:** al ser un dispositivo para tener entre las manos, un cable corto y flexible (20-30 cm) es más cómodo que uno largo y rígido.

**Sin resistencias ni componentes discretos.** Los botones usan las resistencias internas (`INPUT_PULLUP`) y el módulo OLED ya lleva las de I2C. Todo es cableado.

**Joystick de 10 × 10 mm (modelo genérico "5 Way Switch DIP 10×10×10", lote de 5).** El título de la ficha da 10 × 10 × 10 mm y montaje DIP; el número de patillas y el paso no constan y, en este tipo de componente, suelen ser 6 o 7 (5 direcciones más 1 o 2 comunes). Mapéalo con el polímetro en continuidad al recibirlo. Verifica en la ficha que sea de patillas (no SMD), cuántas tiene (5 direcciones más 1 o 2 comunes), el paso entre ellas, y que cada dirección cierre contra el común.

## 4. Materiales

| Tienes | Hay que comprar |
|---|---|
| OLED SSD1306 0,91" (DollaTek) | La Adafruit KB2040 |
| Microinterruptores SMD (ya no hacen falta) | **Joystick táctil de 5 direcciones, 10 × 10 mm, con patillas** |
| PCB 80×20 mm (se puede cortar) | **Cable USB-C** corto y flexible (20-30 cm) |
| Soldador y experiencia | Opcional: carcasa impresa o termorretráctil |
| Pro Mini y PL2303 | (sobran para este proyecto) |

## 5. Seguridad: lo que se puede y lo que no

Tu leeme pide "la máxima seguridad posible" y a la vez dice "de forma no segura". Aquí lo fijo así: **las contraseñas se guardan cifradas.**

### Modelo de amenaza
- **Lo que protegemos:** que alguien que encuentre o robe el dispositivo no pueda leer las contraseñas.
- **Lo que no protegemos:** que alguien con el dispositivo desbloqueado, o con acceso a tu ordenador mientras teclea, las vea.

### Riesgo principal: teclear donde no toca (2026-11-05)
Un dispositivo HID es **ciego**: teclea en lo que tenga el foco en ese instante y no puede ver la pantalla para comprobar que es el campo correcto. Si el foco está en un chat, un terminal, un documento o una pantalla compartida, el secreto se escribe en claro donde no debe (y puede quedar logueado). Es el fallo operativo más probable de un tecleador de contraseñas. Como el aparato no puede verificar el foco, la defensa es reducir la superficie y exigir intención:

- **Lo más sensible no se teclea nunca.** La contraseña maestra se introduce en el propio aparato y alimenta la derivación (PBKDF2 → bóveda) *on-device*; no sale por USB. El peor caso (la maestra expuesta) queda descartado por diseño. Solo se emiten los secretos por servicio.
- **Nunca teclear solo (auto-type).** Ni al conectar, ni al encender, ni por inactividad. Siempre tras un gesto deliberado en el aparato (centro largo).
- **Previsualizar y dar margen de aborto.** La OLED muestra lo que va a escribir; una cuenta atrás corta (1-2 s) antes de emitir da tiempo a cancelar si el foco está mal.
- **INTRO final desactivado por defecto.** El salto de línea es lo que convierte «escribí donde no tocaba» en «envié el mensaje / ejecuté el comando». Opt-in por servicio (los scripts pueden pedir `INTRO` explícito).
- **Procedimiento del usuario:** hacer clic en el campo correcto y comprobar el cursor antes de disparar. Es la última línea de defensa.

Primo de este riesgo: un **mapa de teclado** equivocado (host en US y aparato asumiendo ES, o Bloq Mayús activado) también «escribe mal». Mismo síntoma, causa distinta; ver §7.

### Una limitación que hay que asumir
El RP2040 **no tiene protección contra lectura de la flash**. Con acceso físico, alguien puede volcar todo el contenido. Por eso:

- **La seguridad depende solo de la criptografía y de la contraseña maestra.** Un borrado tras N intentos fallidos no protege contra quien vuelca la flash sin pasar por el firmware. Sería solo cosmético.
- (Esto corrige lo que sugerí antes, con los lock bits del 32U4: esa opción no existe en esta placa.)

### Diseño criptográfico propuesto
- Contraseña maestra, con una **sal aleatoria** guardada en claro junto al almacén.
- Clave derivada con un **KDF lento**: PBKDF2-HMAC-SHA256 con las iteraciones calibradas midiendo en la propia placa, buscando 1-2 segundos de desbloqueo. Conviene evaluar si hay margen para uno más exigente en memoria.
- Cifrado autenticado (**ChaCha20-Poly1305 o AES-GCM**), para detectar manipulación además de ocultar el contenido.
- El almacén se guarda como un único bloque cifrado en los últimos sectores de la flash, con **escritura en doble copia** para que un corte de corriente no lo corrompa.
- Después de usar cualquier secreto, se borra de la RAM. Nada sensible sale por el puerto serie.
- Bloqueo automático por inactividad.

### El problema real: la contraseña maestra
Con solo un joystick, lo cómodo es un PIN. Un PIN de 4-8 dígitos se rompe sin dificultad si alguien vuelca la flash. Opciones:

1. **Frase alfanumérica de 10 o más caracteres**, introducida con la rueda de caracteres (arriba/abajo cambia el carácter, aceptar avanza). Es lento, pero se hace una vez por sesión.
2. **Fase 2: chip ATECC608** (I2C, comparte bus con el OLED). Liga la clave al hardware y limita los intentos, y así el volcado de la flash deja de bastar para romperlo. Es la mejora real de seguridad.

Mi recomendación: empezar con la opción 1 y dejar el ATECC608 como fase 2.

### Cambiar la contraseña maestra
**Sí se podrá cambiar, desde los botones.** El diseño lo facilita con dos capas de clave:

1. Se genera una **clave aleatoria de 32 bytes** que cifra el almacén. Nunca se deriva de la contraseña.
2. Tu contraseña maestra deriva otra clave (con la sal y el KDF) que solo **envuelve la clave aleatoria**.

Cambiar la contraseña consiste en que el dispositivo te pida la actual, y la introduces tú con la rueda de caracteres. Después te pide la nueva dos veces y vuelve a envolver la clave aleatoria con una sal nueva. La primera vez, al no haber contraseña, te pide crearla. **No hay que reescribir todo el almacén**, así que es rápido y seguro contra cortes de corriente (escritura en doble copia).

Consecuencias:
- **Si olvidas la contraseña, no hay forma de recuperar nada.** Es lo esperable en un diseño así.
- Como opción, la misma clave aleatoria puede envolverse con una **clave de recuperación** larga, impresa una sola vez y guardada en papel. Así tendrías un segundo desbloqueo.
- Cambiar la contraseña no invalida copias antiguas de la flash. Si sospechas que alguien la copió, cambia también las contraseñas de los servicios importantes.
- Las dos capas facilitan añadir más adelante otros métodos de desbloqueo, como el chip ATECC608.

### Otros puntos
- **Reflashear el firmware es posible** (basta arrastrar un UF2), pero sin la contraseña maestra no se descifra el almacén.
- **Generador de contraseñas:** combinar el generador aleatorio de hardware del RP2040 (`rp2040.hwrand32()`) con ruido del ADC y tiempos de pulsación, y mezclarlo con SHA-256. Su calidad es discutible, y el ATECC608 la resolvería mejor.

### Uso previsto (2026-11-15)

El 90 % de las contraseñas se meten en Firefox desde KeePassXC; el Ardutype no compite con eso. Su sitio es donde KeePassXC no llega: antes de arrancar el sistema (cifrado del disco, BIOS, GRUB), el inicio de sesión y las consolas TTY, máquinas ajenas o del trabajo donde no se puede instalar nada (el origen del proyecto, write-pass), máquinas virtuales o consolas remotas sin portapapeles, y el móvil por USB OTG. Uso limitado, pero útil. Por eso conviene **guardar en él solo las contraseñas de esos casos** (revisión de seguridad, abajo). Para darle más uso: el pomodoro (fase 9), los guiones (fase 5, ya hechos) y el modo «mantener sesión» (idea pendiente, en «Posibles mejoras»). Decidir la fase 7 según lo que se use en las próximas semanas.

### Revisión de seguridad (2026-11-15)

Vista de un usuario exigente con el dispositivo ya en producción. **Conclusión:** fiable ante pérdida o robo; débil ante quien pueda manipularlo a solas y devolverlo.

**Lo que aguanta:** sin el chip no se puede probar ningún PIN, y con él como mucho 10-41 intentos (0,004 % con 6 cifras). La clave de recuperación de 8 palabras (~84 bits) no se rompe por fuerza bruta. Primitivas estándar (BearSSL). Superficie pequeña: sin radio; las órdenes del PC exigen desbloqueo y confirmación en el aparato; al desenchufarlo queda bloqueado. La copia maestra está en KeePassXC.

**Preocupaciones, de más a menos grave:**
1. **Firmware sin verificar ("criada malvada").** El RP2040 no tiene arranque seguro: con el botón BOOT cualquiera carga otro `.uf2`. Un firmware manipulado puede capturar el PIN la siguiente vez (y con el chip, abrir la bóveda) o comportarse como *rubber ducky* contra el PC, que ya confía en él como teclado. No tiene solución con esta placa.
2. **Escucha del bus I2C del ATECC** (ya fuera del modelo, sección 10, fase 8): quien conecte un analizador mientras se desbloquea obtiene `h`, que junto a un volcado de la flash abre la bóveda. Ahora las patas del chip quedan accesibles bajo la placa de protección.
3. **Desbloqueado y desatendido lo da todo** (navegar, «Mostrar» en el OLED). Bloqueo automático por defecto: 15 min.
4. **Sin auditoría:** las primitivas son buenas, pero su composición (ranuras, envoltura de claves, protocolo serie) solo la hemos revisado nosotros.
5. **Las contraseñas también están en el móvil** (KeePassDX + Syncthing): su seguridad cuenta tanto como la del Ardutype.
6. **Teclear tiene sus riesgos:** foco en la ventana equivocada (sección «Riesgo principal») y keyloggers en el PC, igual que tecleando a mano.

**Medidas:**
- Ya, sin coste: bloqueo automático a 5 min; no dejarlo enchufado sin vigilancia; papel de las 8 palabras lejos del aparato; cuidar la seguridad del móvil.
- Baratas: precinto o pegatina de seguridad sobre la carcasa y el botón BOOT para que se note si lo han abierto; resina sobre el ATECC y sus pistas.
- Con la fase 7: valorar una placa con **RP2350** (arranque seguro con firmware firmado, claves en OTP), que cierra el punto 1. Estudiar si la *IO protection key* del ATECC608B permite cifrar lo que viaja por el bus (punto 2).
- **Guardar solo lo que haga falta fuera de KeePassXC** (etiqueta `ardutype`): menos contraseñas en el aparato, menos se pierde si falla cualquiera de lo anterior.

## 6. Modelo de datos

- **Categorías** (RR.SS., Banca, Trabajo...), con **servicios** dentro.
- Cada servicio: `nombre`, `usuario` (opcional) y `contraseña`. **Usuario y contraseña se teclean por separado** (decisión del 2026-10-26): las acciones son «teclear usuario» y «teclear contraseña»; se descartan los modos combinados usuario TAB contraseña (+ INTRO). Un servicio sin usuario solo ofrece la contraseña. Tras teclear el usuario, la selección pasa sola a «teclear contraseña».
- Volumen previsto: **unas 50 entradas**.
- **Scripts:** secuencias con `TEXTO "..."`, `TAB`, `INTRO`, `ESPERA 500` y combinaciones como `CTRL+ALT+SUPR`. Lo que hace hoy write-pass sería un script más.
- Con 8 MB de flash hay espacio de sobra, el doble que con la placa anterior.

## 7. Teclado: layout español completo

write-pass solo cubre `@`, `#` y `$`. Ardutype necesita una tabla completa de carácter a código HID más modificadores:
- **AltGr:** `@ # ~ € \ | { } [ ]`.
- **Shift:** `! " · $ % & / ( ) = ? ¿ * _ : ;`.
- **Teclas muertas** (´ ` ^ ¨): necesitan una pulsación doble o un espacio.
- **`ñ`, `Ñ`, `ç`.**

Detalles prácticos:
- El **layout debe ser seleccionable** (ES o US), por si el equipo destino usa otro.
- Teclear con **retardos por tecla** y con una cuenta atrás de 2-3 segundos en el OLED antes de empezar.
- **Bloq Mayús activado** cambiaría las letras. Hay que investigar si se puede leer el estado de los LED del teclado por HID.

**Implementación (2026-10-26, `prueba_teclado/teclado.cpp`, probada con éxito en Linux y Windows):** el usuario usa teclado español el 95 % del tiempo, pero quiere poder elegir la distribución. Distribuciones: **ES Linux, ES Windows y US** (las dos ES solo difieren en la ~, tecla muerta en Windows y directa en Linux; macOS queda fuera, sus modificadores son otros). La parte ASCII reutiliza las tablas `KeyboardLayout_es_ES`/`en_US` del core y se añaden ñ Ñ ç Ç € ¿ ¡ º ª · ¬, las vocales con ´ ` ^ ¨ por tecla muerta y esos signos sueltos (muerta + espacio). Las teclas se mandan como códigos HID crudos. **Bloq Mayús:** con la DigiSpark de write-pass salía todo en mayúsculas. El ordenador manda el estado de su LED (`Keyboard.onLED`); si está encendido, se apaga antes de teclear y se restaura al terminar.

## 8. Interfaz

- **Arranque:** logotipo animado.
- **Desbloqueo:** rueda de caracteres para la contraseña maestra.
- **Navegación con el joystick:** arriba y abajo mueven la selección, derecha entra o avanza, izquierda vuelve, y la pulsación central acepta. Una pulsación larga da acciones extra (por ejemplo, teclear frente a mostrar).
- **Rueda de caracteres:** arriba y abajo cambian la letra, derecha avanza, izquierda borra, central confirma. Es mucho más natural que con 4 botones.
- **Menú:** categorías, servicios, acción (teclear, editar, borrar). Añadir categoría o servicio desde el propio dispositivo.
- **Estado por el OLED, no por LED:** el NeoPixel queda tapado por el OLED (sección 3) y se sacrifica como indicador; el propio OLED muestra el estado (bloqueado, desbloqueado, tecleando) con un icono o color de fondo.
- **Animaciones** de transición y un salvapantallas al bloquearse.
- Escribir servicios enteros con un joystick sigue siendo incómodo. Propongo una **herramienta de administración en el PC** (por USB serie, solo en modo admin) para dar de alta entradas. Aviso: pasa las contraseñas por tu ordenador.

## 9. Entorno y librerías (a verificar)

- Arduino IDE con el core **arduino-pico** (Earle Philhower), que incluye de fábrica la placa "Adafruit KB2040".
- OLED: **U8g2** o **Adafruit SSD1306**.
- LED: **Adafruit NeoPixel** (usa PIO).
- Criptografía: **Arduino Cryptography Library** (rweather) o mbedTLS, si el core la incluye.
- USB HID: el teclado del core, usando informes HID en bruto para poder enviar cualquier código y modificador.

### Subir el firmware sin arriesgar la bóveda (2026-11-15)

La bóveda (`/boveda.bin`) y la clave ADMIN del chip (`/chip.bin`) viven en el sistema de ficheros, cuya posición en la flash depende del ajuste **Flash Size**. Con otro ajuste, el firmware no los encontraba y `LittleFS.begin()`, que **formatea solo** si no puede montar, los borraba: las contraseñas se recuperan de KeePassXC, pero el ATECC608 se queda sin su clave ADMIN y hay que soldar otro. Tres protecciones:
1. **`config.h` no compila** si el Flash Size no es «8MB (Sketch: 7MB, FS: 1MB)» (comprueba `FS_START`/`FS_END`, que el core pasa al compilar). Vale también desde el IDE. `formatear_memoria` lleva la misma comprobación.
2. **Sin formateo automático** (`LittleFSConfig(false)`): si no se puede montar, pantalla «Memoria no legible» y no se toca nada. Una placa nueva se formatea a propósito con `formatear_memoria`.
3. **`subir.sh`**: compila con el ajuste fijado en el FQBN y sube. Busca la placa sola (VID de Adafruit) o acepta `-p /dev/ttyACM0`; si está en modo BOOTSEL, copia el `.uf2` a la unidad RPI-RP2; `-c` solo compila. Usa el `arduino-cli` del sistema o descarga uno propio (1.6.0) en `herramienta/bin/`, con la configuración del IDE (`~/.arduinoIDE/arduino-cli.yaml`). `build/` y `herramienta/bin/` no van a git.

4. **Copia de la memoria de ficheros** (`respaldo.sh` / `restaurar.sh`): ni lo anterior ni nada en el RP2040 impide subir **otro** sketch que formatee la memoria (el cargador de la ROM acepta cualquier `.uf2`, y el reinicio a BOOTSEL del core no se puede desactivar). La copia lo arregla todo: guarda con `picotool` (del core) la zona del sistema de ficheros, `0x106FF000`–`0x107FF000` (1 MB), con la bóveda y `/chip.bin`; sin `/chip.bin` el ATECC608 no se puede volver a usar. Por defecto en `~/Sys/ardutype-respaldos` (fuera de Syncthing), con un `.info` que guarda el número de serie de la placa (el identificador único de su flash), la fecha y el SHA-256. Es lo mismo que se puede leer de la placa en mano: va cifrado (la ranura del PIN no se puede probar sin el chip, y la de la clave de recuperación son 84 bits con PBKDF2). `restaurar.sh` comprueba tamaño, marca de LittleFS e integridad, enseña los ficheros, exige la misma placa (salvo `-o`, p. ej. al cambiar de KB2040 conservando el ATECC) y pedir escribir RESTAURAR. Los dos reinician el Ardutype (queda bloqueado): lo pasan a BOOTSEL abriendo su puerto serie a 1200 baudios, como el IDE (`picotool -f` no sirve: su interfaz de reinicio solo existe compilando con el método de subida «Picotool»), y lo devuelven a marcha al terminar, también si algo falla. Si el puerto está en uso (monitor serie del IDE, ardusync) avisan y no hacen nada. En BOOTSEL el número de serie USB es otro (el de la ROM, no el de la flash) y el filtro `--ser` de picotool no encuentra la placa: se elige por su sitio en el bus, y el `.info` guarda los dos números. **Probado (2026-11-15):** copia con `/boveda.bin` (2976 bytes) y `/chip.bin` (74 bytes), verificada por picotool, idéntica a una segunda lectura y con la cabecera `ARDT` de la bóveda; `restaurar.sh` probado hasta la confirmación (sin escribir). `picotool` necesita la regla de udev `herramienta/60-ardutype.rules` (acceso al grupo plugdev). Hacer una copia tras cambios importantes (p. ej. después de cada sincronización).

Con esto, sincronizar con KeePassXC antes de subir ya no es imprescindible: solo protege los cambios hechos en el aparato que aún no se hayan enviado.

## 10. Fases

1. **Prototipo en protoboard:** KB2040, OLED y joystick. Localizar en la placa real RESET, BOOT y el NeoPixel para cerrar las medidas del montaje en dos capas.
2. **Teclado:** tabla ES completa. Probar escritura de todos los caracteres.
3. **Menú y OLED:** navegación, rueda de caracteres, logotipo.
   **Estado (2026-10-26):** firmware en `ardutype/` (compila, pendiente de probar en la placa). Mientras llega el ATECC608 se desbloquea con un **PIN de pruebas de 4 cifras** (`PIN_PRUEBA` en `config.h`) y el almacén vive en RAM sin cifrar, con datos de ejemplo inventados: lo que se añade se pierde al desenchufar. Módulos: `joystick` (antirrebote, repetición, pulsación larga), `pantalla`, `ui` (pila de pantallas), `almacen` (lo sustituirá la fase 4 con la misma interfaz), `generador`, `teclado` (copia del probado en la fase 2) y `utf8`.
   **Rueda:** se edita siempre el último carácter. Arriba/abajo lo cambian dentro de su grupo (abc, ABC, 123 y símbolos ASCII sin comillas, ~ ni `; sin ñ, ç ni acentos), derecha añade uno igual (así los caracteres cercanos salen rápido), izquierda borra (con el texto vacío, cancela), centro cambia de grupo y centro mantenido acepta. Debajo se ven los vecinos del carácter en su grupo.
   **Menús:** centro mantenido bloquea desde cualquier menú. Ajustes en RAM: distribución, retardo por tecla, bloqueo por inactividad (5/15/30/60 min) y brillo. El OLED se apaga tras 1 min sin tocar nada; la primera tecla solo lo enciende.
   **Usuario visible (decisión del usuario):** el usuario no es secreto; en las acciones del servicio aparece como «U: usuario» y elegirlo lo teclea. «Mostrar contraseña» enseña la contraseña 15 s.
   **Generador (decisión del usuario):** 16 caracteres de a-z, A-Z, 0-9 y los signos ASCII `!#$%&()*+,-./:;<=>?@[\]^_{|}` (sin comillas, ~ ni `; todos están también en la rueda), sin eñe, ç ni acentos, y sin l, I, O, 0 ni 1 (se confunden en el OLED); al menos uno de cada clase.
   **NeoPixel (adelantado de la fase 6, módulo `led`, librería Adafruit NeoPixel):** destello azul con cada pulsación, dos rojos en un error (PIN incorrecto, sin USB, no caben más...), uno verde en un acierto (desbloqueo, tecleo terminado, contraseña generada), ámbar latiendo en la cuenta atrás y el tecleo, y arcoíris lento en reposo (OLED apagado). Ajustes: «LED: sí/no»; el brillo de Ajustes vale para el OLED y el LED.
4. **Almacén cifrado:** KDF calibrado, cifrado autenticado, doble copia.
   **Estado (2026-10-26):** firmware en `ardutype/` (compila, pendiente de probar en la placa). Módulos `cripto` (sobre BearSSL, incluida en el core) y `boveda`; `almacen` queda como modelo en RAM más su serialización.
   - **Fichero** `/boveda.bin` en LittleFS. Hay que subir con **Herramientas > Flash Size: «8MB (Sketch: 7MB, FS: 1MB)»**; sin esa partición el OLED avisa («Falta la memoria»).
   - **Claves:** clave maestra aleatoria de 32 bytes; cuerpo cifrado con **ChaCha20-Poly1305** y la cabecera entera como datos autenticados. La clave maestra va envuelta en dos ranuras: **PIN** (PBKDF2-HMAC-SHA256, iteraciones calibradas en la placa a ~1 s, mínimo 10 000) y **recuperación** (128 bits aleatorios, HKDF-SHA256).
   - **Clave de recuperación:** 26 caracteres en base32, `XXXX-XXXX-XXXX-XXXX-XXXX-XXXX-XX`. Se enseña una sola vez al crear la bóveda y no se guarda; se admite en minúsculas y confundiendo 0/O, 1/I y 8/B. Desde la pantalla de bloqueo, centro mantenido la pide con la rueda, que en este caso solo ofrece los grupos `A-Z` y `2-7` (sin guiones: no hacen falta). Al entrar obliga a poner un PIN nuevo. No hace falta fallar el PIN antes: es una segunda llave de 128 bits, no un modo de rescate, y exigir fallos previos no añadiría seguridad.
   - **Sustituye a la doble copia:** cada guardado escribe `/boveda.tmp` y lo renombra encima (LittleFS lo hace atómico), así que un corte de corriente deja la versión vieja o la nueva.
   - **Ajustes** en la cabecera, en claro pero autenticados, porque se necesitan antes de desbloquear.
   - **Primer arranque:** crear PIN (dos veces), clave de recuperación, confirmar que está apuntada. Ajustes añade «Cambiar PIN» (pide el actual) y «Borrar todo».
   - **Al bloquear** se borran de la RAM la clave maestra y el almacén.
   - **Limitación conocida:** cambiar el PIN no rota la clave maestra (la ranura de recuperación la necesita); en bloques libres de la flash pueden quedar ranuras antiguas envueltas con PINs viejos. Con el ATECC608 (fase 8) las ranuras con chip no se pueden probar sin él; las de antes del chip sí, de ahí lo de crear la bóveda de cero.
   - Probado en el PC: ida y vuelta de la clave de recuperación (20 000 claves) y de la serialización (almacén lleno: 23 015 bytes).
5. **Herramienta de administración** por USB serie.
   **Diseño (2026-10-26): integración con KeePassXC.** KeePassXC es el original (todas las contraseñas y sus copias de seguridad); Ardutype lleva una copia de las ~50 de diario.
   - **Uso inmediato, sin fase 5:** un servicio «KeePassXC» en el Ardutype teclea la contraseña maestra de la base de datos, que así puede ser larga y aleatoria. Esa maestra también se apunta en papel junto a la clave de recuperación (si se pierde el Ardutype, no puede quedarse solo ahí).
   - **Herramienta `ardutype-sync`** en Python con `pykeepass`. Lee el `.kdbx` directamente y pide la maestra en el PC, sin exportar a CSV (un CSV deja todo en claro en el disco). Linux y Windows.
   - **Qué entradas:** las que lleven la **etiqueta `ardutype`** (las etiquetas de KeePassXC 2.7+ se guardan en la base de datos, formato KDBX 4.1). Como alternativa, todo lo que esté bajo un grupo «Ardutype».
   - **Correspondencia:** grupo de KeePassXC → categoría; Título → nombre; Usuario → usuario; Contraseña → contraseña. Cada servicio guarda el **UUID de la entrada** (16 bytes) para reconocerla aunque cambie de título, y una marca de «cambiado en el dispositivo». Esto pide la versión 2 del formato de serialización (lectura de la v1 conservada).
   - **Límites:** 16 categorías, 64 servicios, nombre de hasta 31 bytes, usuario y contraseña de hasta 95. La herramienta avisa de lo que no quepa, en lugar de recortarlo sin decir nada. También avisa de los caracteres que la distribución elegida no puede teclear.
   - **Sincronización KeePassXC → Ardutype:** el Ardutype tiene que estar desbloqueado. La herramienta calcula las diferencias y las enseña («+3 nuevas, 2 cambiadas, 1 borrada»). El dispositivo pide confirmar en su pantalla antes de aplicar nada.
   - **Ardutype → KeePassXC (después):** el dispositivo solo devuelve las entradas creadas o cambiadas en él, por ejemplo una contraseña generada fuera de casa. Nunca vuelca el almacén entero. Si una entrada cambió en los dos lados, la herramienta pregunta cuál vale. Al terminar se borra la marca de «cambiado».
   - **Protocolo:** CDC serie (el mismo puerto USB que el teclado). Comandos en líneas de texto con los datos en base64. Toda orden que lea o escriba contraseñas exige que el dispositivo esté desbloqueado y que se confirme en él. Las contraseñas viajan en claro por el cable: se acepta, porque un PC comprometido con KeePassXC abierto ya lo tiene todo.
   - **Fuera:** emular una YubiKey para KeePassXC : KeePassXC solo reconoce dispositivos por el VID de Yubico u OnlyKey; con el PIN de 4 cifras y la flash legible, la clave HMAC se sacaría en minutos; y si el mismo aparato teclea la maestra y responde al desafío, los dos factores se convierten en uno.
   **Estado (2026-10-26):** implementada, compila, probada en el PC contra el `admin.cpp` y el `almacen.cpp` reales (pendiente de probar en la placa).
   - **Firmware:** módulo `admin` (protocolo en `admin.h`) y pantalla «PC conectado». Abrirla deja solo esa pantalla (corta una edición a medias), salvo durante la clave de recuperación, un PIN nuevo o un tecleo. Izquierda la cierra; sin órdenes se cierra sola al minuto (`SESION_PC_MS`). Cada servicio lleva `enlazado`, `uuid` y `cambiado`; editar, crear o renombrar su categoría lo marca; borrar uno enlazado lo apunta en `borrados`. Formato serializado 3 (`0xA3` delante); lee el 2 (`0xA2`, con guion en línea) y el 1 de la fase 4 (sus servicios cuentan como nuevos).
   - **Herramienta:** `herramienta/ardutype_sync.py` (`pip install -r herramienta/requirements.txt`). Busca la placa por el VID de Adafruit (o `--puerto`), pide la maestra de KeePassXC, admite `--clave-fichero`, `--etiqueta` y `--simular`. Antes de escribir deja `<base>.kdbx.antes-ardutype`. Guarda el historial de las entradas que cambia.
   - **Conflictos:** en cada entrada enviada se guarda (atributo protegido `ardutype-sincronizado`) la huella SHA-256 de lo enviado. Si una entrada cambió en el dispositivo y su huella ya no cuadra con KeePassXC, ha cambiado en los dos lados y la herramienta pregunta.
   - **Recortes:** nombre y categoría de más de 31 bytes se recortan (y al volver no pisan el título completo); usuario o contraseña de más de 95 bytes no se envían. Un servicio borrado en el dispositivo solo pierde la etiqueta en KeePassXC.
   - **Robustez:** si se corta tras guardar el .kdbx y antes de enlazar, la siguiente vez el servicio nuevo se reconoce por sus campos y no se duplica. `ENVIAR` se rechaza con cambios del dispositivo sin recoger.
   - **Sin YubiKey:** `pykeepass` no hace el desafío-respuesta. Si se añade una YubiKey a la base, habría que leerla con `keepassxc-cli`.
   - **Guiones (2026-10-27):** cada servicio puede tener un **guion** opcional, una secuencia que el Ardutype teclea y ejecuta (automatizar un acceso, abrir una terminal y lanzar un comando, etc.). Se escriben y editan **solo en KeePassXC**, en el atributo `ardutype-guion`; en el dispositivo únicamente se ejecutan.
     - **Módulo `guion`** (`guion.h`/`.cpp`): intérprete y comprobador. Órdenes `ESCRIBIR <texto>` (admite `$usuario` y `$contrasena`), `TECLA <t>[+<t>…]` y `PAUSA <ms>` (tope 10 s); `#` comenta. Teclas: `intro tab esc espacio supr retro insert arriba abajo izq der inicio fin repag avpag menu f1..f12` y una letra/cifra suelta; modificadores `ctrl alt altgr mays super`. Sintaxis completa y ejemplo en `herramienta/guiones.md`.
     - **Teclado:** `tecladoCombinacion(mods, tecla)` para las pulsaciones con modificadores.
     - **Almacén:** los guiones viven en un almacén aparte (`MAX_GUIONES` 8 ranuras de `LARGO_GUION` 1024 bytes); cada servicio guarda el índice de su ranura (`SIN_GUION` si no tiene). Así subir el tamaño del guion no multiplica la RAM por los 64 servicios. En el formato serializado 3, el servicio con guion lleva el bit 2 de marcas y un índice; los textos van en una lista aparte (largo u16). Se bajó `MAX_SERVICIOS` de 100 a 64 (de sobra para las ~50 entradas previstas).
     - **Interfaz:** en las acciones del servicio, si hay guion, aparece «Ejecutar guion», con la misma cuenta atrás de 3 s que el tecleo. `Tecleo` se generalizó para teclear un texto o ejecutar un guion. Antes de ejecutar se comprueba la sintaxis; si falla, avisa con la línea.
     - **Herramienta:** lee/escribe el atributo `ardutype-guion`, valida el guion (misma gramática) y su longitud (hasta 1023 bytes) antes de enviarlo (si no, lo envía sin guion y avisa); avisa también si hay más de 8 entradas con guion. La huella de conflictos y el campo `SERV` del protocolo incluyen el guion.
     - **Seguridad:** es el mismo mecanismo que un «rubber ducky»; la diferencia es que **siempre lo lanza el usuario** (elige el servicio, cuenta atrás), nunca al conectar ni al desbloquear. Teclea a ciegas (no comprueba la ventana activa) y no puede ocultar rastro (lo que escriba queda en el historial del shell salvo que el propio guion lo borre).
     - Probado en el PC: intérprete (teclas, modificadores, `$usuario`/`$contrasena`, casos con error), envío y ejecución de extremo a extremo, y que un guion sobrevive a editar el servicio en el dispositivo.
6. **Estética:** animaciones y LED de estado.
7. **PCB final y carcasa.**
   **Parada (2026-11-14)** hasta validar bien el funcionamiento en el uso diario. Mientras tanto, una placa por debajo con separadores protege las soldaduras y el ATECC.
8. **Fase 2 de seguridad:** ATECC608.
   **2026-11-12:** llegan los ATECC608B (10 × **ATECC608B-SSHDA-B**: SOIC-8, I2C, genérico sin configurar, dirección 0x60; con 10 se pueden sacrificar unos cuantos en pruebas de bloqueo). Montaje: adaptador SOIC-8 a DIP-8 (paso 1,27 mm); patilla 1 junto al punto; 4 GND, 5 SDA, 6 SCL, 8 VCC (3V3), 1-3 y 7 sin conectar; 100 nF entre 8 y 4. Prueba de concepto en `prueba_atecc/` (solo lectura, sin librerías: despertar, CRC-16, sondeo): busca el chip, INFO (revisión), vuelca la zona de configuración (serie, dirección, bloqueos, SlotConfig/KeyConfig), SHA-256 de "abc" contra el valor conocido y RANDOM. **Resultado:** todo correcto (0x60, revisión 0x6003, configuración de fábrica sin bloquear, SHA bien, RANDOM = patrón fijo).
   **Diseño (2026-11-12):** el límite de intentos lo aplica el chip, porque el firmware se puede reflashear. La hoja de datos completa del 608B es bajo NDA; el mecanismo sale de la nota AN3539 de Microchip y del firmware abierto de Coldcard (608A), y se verifica en un chip de sacrificio.
   - **Ranura 4 INTENTO:** clave aleatoria que nunca sale del chip, `LimitedUse`: cada MAC con ella suma 1 al contador 0, y al llegar al límite (`CountMatch`) el chip deja de usarla.
   - **Ranura 6 LIMITE:** el límite del contador, legible en claro, solo se escribe cifrado con la clave 3. Va de 32 en 32 (el chip no compara los 5 bits bajos); tras un desbloqueo bueno se sube para que queden al menos 10 → entre 10 y 41 intentos.
   - **Ranura 3 ADMIN:** clave que solo conoce la bóveda abierta (cifrada dentro); autoriza subir el límite.
   - **Desbloqueo:** `h = MAC_chip(INTENTO, PBKDF2(PIN, sal))`; la KEK de la ranura PIN sale de `h`. Sin el chip no se puede probar ningún PIN; con el chip, como mucho los intentos que queden.
   - **Agotados los intentos:** se entra con la clave de recuperación (no depende del chip), que da acceso a la clave ADMIN y permite subir el límite.
   - **Fuera del modelo:** alguien que escuche el bus I2C mientras desbloqueas ve `h`.
   - Módulo `ardutype/atecc.h/.cpp` (sin librerías). Prueba destructiva `prueba_atecc_bloqueo/` (clave ADMIN fija: el chip queda solo para pruebas; compila para KB2040 o ESP8266, que hace de banco de pruebas aparte para no tocar el chip bueno, que queda virgen para el Ardutype): provisiona y bloquea, comprueba que cada intento suma 1, que se para en el límite, que se sube con la clave buena y no con una falsa, y que el chip se niega a leer las claves o a escribir el límite en claro. **Chip de pruebas 1 (2026-11-12, en ESP8266 con pinzas):** provisiona y bloquea bien; cada intento suma 1 y da la misma respuesta; los 5 ataques, rechazados (0x0F); se para justo en el límite (32). **Falla subir el límite con la clave buena (0x0F).** Sospecha: la ranura ADMIN tenía `NoMac` (0x809F); el GenDig con una clave así marca TempKey y la escritura cifrada se rechaza (Coldcard no lo pone). Corregido a 0x808F. Ese chip queda inservible (agotado y sin poder subir el límite). La prueba de la clave falsa no llegó a escribir (el límite ya cubría lo pedido): ahora `s`/`f` fuerzan siempre límite + 32 e indican en qué paso falla. **Chip de pruebas 2:** todo correcto. Clave falsa rechazada (en el Write, 0x0F, límite sin cambiar); clave buena: 32 → 64; se para en 64; **con el contador ya en el límite también deja subirlo** (64 → 96), así que la clave de recuperación puede devolver los intentos sin cambiar el chip; la respuesta del MAC no cambia; RANDOM ya aleatorio. Confirmado que el `NoMac` de ADMIN era el fallo del chip 1.
   **Integración (2026-11-12, compila; pendiente de probar en la placa):**
   - `boveda`: nueva ranura `RANURA_PIN_CHIP` (formato de la bóveda sin cambios): KEK = HKDF(MAC del chip sobre el PBKDF2 del PIN, sal). La clave ADMIN va en `/chip.bin`, cifrada con la clave maestra y con el número de serie del chip (si el chip es otro, no se prueba el PIN para no gastarle intentos).
   - **Provisión automática** (irreversible) la primera vez que la bóveda se abre o se crea con un chip virgen. Una bóveda antigua (PIN sin chip) se pasa sola al chip al abrirla con el PIN; pueden quedar restos de la ranura vieja en la flash, así que **para contraseñas reales: «Borrar todo» y crear la bóveda de cero con el chip puesto** (así la clave maestra nunca toca la flash envuelta solo con el PIN).
   - Tras cada desbloqueo bueno (PIN, recuperación o «PIN actual») se suben los intentos para que queden al menos `INTENTOS_CHIP` (10; quedan de 10 a 41). PIN incorrecto: «Incorrecto. Quedan N». Agotados: «PIN agotado: clave» → la clave de recuperación los repone y obliga a poner PIN nuevo. Sin el chip: «Falta el chip». La espera de 30 s tras 3 fallos se mantiene.
   - «Borrar todo» conserva en RAM la clave ADMIN para la bóveda nueva; si se reinicia antes de crearla, el chip queda sin su clave (huérfano): el PIN irá sin chip y habría que poner otro chip.
   - Si el PIN ya va con el chip, «Cambiar PIN» sin el chip falla en vez de dejarlo solo en software.
   - **Fallo en la placa (2026-11-12): la pantalla se queda a medias y deja de refrescarse** (tras «¿La has apuntado?» y tras meter el PIN; la mitad de abajo conserva la pantalla anterior, el LED sigue animándose). El bus I2C del OLED se corta a mitad de un envío. Sospecha: el ATECC compartía el bus y el tráfico de la pantalla (rachas de bytes a 0 mantienen SDA bajo > 60 µs) lo despierta a mitad de una transacción. El usuario prefiere no recablear salvo que no quede más remedio (la alternativa sería el ATECC en su propio bus, I2C1 en A0/A1 con pull-ups). Primero, diagnóstico sin tocar el hardware: U8g2 usa un envío I2C propio que apunta los errores; tras cada refresco, si los hubo, se escribe por el serie (código, niveles de SDA/SCL y ms desde la última orden al ATECC), se libera el bus (9 pulsos de reloj y STOP) y se reinicia el OLED. Además `Wire.setTimeout(25, true)`. **Resultado:** con el ATECC en el bus, error 5 (tiempo agotado) en casi cada refresco, aunque el chip lleve decenas de segundos sin órdenes (lo despierta el propio tráfico); antes del diagnóstico la espera era de 1 s por trozo y eso congelaba la pantalla. Con el SDA del ATECC desconectado, los errores desaparecen (salvo uno suelto con la rueda de palabras en «(ninguna)», estado que ya no se puede alcanzar: derecha solo añade letra si alguna palabra sigue). **Decisión: separar los buses moviendo el OLED** (menos trabajo que mover el chip): OLED en I2C1 (A0 = SDA, A1 = SCL, con sus pull-ups); ATECC solo en I2C0 (TX/RX) a 100 kHz. Las pull-ups internas del RP2040 no bastaron (el chip no contestaba ni a 20 kHz): **hacen falta 4,7 kΩ de TX y RX a 3V3**. Después, un fallo intermitente («falla el MAC», RX a 2,7 V en reposo) resultó ser **SDA rozando SCL** en las patas 5 y 6 del chip, montado al aire: conviene aislarlas. Con todo eso, **desbloqueo con el chip funcionando (2026-11-13)**. Aún hubo fallos sueltos (`falla el MAC`, `no se pueden reponer los intentos`) por contactos del montaje al aire; para localizarlos, `prueba_atecc` tiene un modo continuo (línea «c»: INFO + SHA diez veces por segundo, cuenta los fallos mientras se mueven los cables). Arreglado el contacto: estable, y **cambio de PIN tras la recuperación con palabras comprobado**. Se mantienen el informe de errores I2C por serie y la recuperación del bus del OLED; además, al meter el PIN se informa por serie de por qué falta el chip.
   - **PIN de 6 cifras** (decisión del usuario, 2026-11-12): con como mucho 41 intentos, acertar es un 0,004 % (con 4 cifras, 0,4 %).
   - **Clave de recuperación en palabras** (decisión del usuario, 2026-11-12): **8 palabras** al azar de `dice_lista.h` (1459 palabras, ~84 bits), con el mismo PBKDF2 calibrado (~1 s) que el PIN, ya que no pasa por el chip (ranura `RANURA_PALABRAS`). Se enseña al crear la bóveda en 4 páginas de 2 palabras numeradas (derecha/izquierda pasan página; centro largo confirma desde la última). Se mete con la rueda de palabras de la PoC, generalizada (3 letras + centro por palabra; al completar las 8 se comprueba sola). Las bóvedas anteriores siguen aceptando su clave base32. Descartadas: 12 palabras sin derivación lenta (126 bits) y dejar el base32.

   **Producción (2026-11-14):** patas 5 y 6 del ATECC aisladas; bóveda borrada y creada de cero con el chip puesto. La estética (fase 6) se da por buena de momento. Empieza el uso con contraseñas reales.

9. **Pomodoro (2026-11-15, probado en la placa: todo correcto).** Zumbador **piezo** con 100 Ω en D8.
   - **Fases:** trabajo 25 min (LED naranja), descanso 5 min (verde), descanso largo 15 min (azul); se eligen a mano. **Pausa** indefinida: el LED parpadea despacio (1 s) con el color de la fase pausada y «Reanudar» sigue donde se quedó.
   - **Al terminar:** un pitido y el LED parpadea deprisa (2 veces por segundo) con el color de la fase; el pitido se repite cada minuto hasta que se pausa (silencia) o se empieza otra fase. La pantalla muestra «¡fin!» y el tiempo pasado desde el final (`+02:13`).
   - **Interfaz:** menú principal → «Pomodoro»: cabecera con la fase y cuenta atrás en grande. Centro o derecha abren el menú (Pausar / Reanudar, Trabajo, Descanso, D. largo, Detener), con el cursor en lo más probable: tras un trabajo, el descanso, y el largo cada 4 trabajos. Izquierda vuelve, centro largo bloquea.
   - **Sin la bóveda:** el pomodoro no toca contraseñas, así que funciona bloqueado. En la pantalla de bloqueo, **arriba** lo abre, y la línea de abajo alterna cada 3 s con su estado. Si salta el bloqueo automático mirando el pomodoro, se cierra la bóveda pero la cuenta atrás sigue en pantalla.
   - **OLED:** en la pantalla del pomodoro con una fase en marcha no se apaga a los 60 s: baja al brillo mínimo; la primera tecla solo lo devuelve.
   - **LED:** el color del pomodoro tiene prioridad sobre el arcoíris de reposo, así que se ve en cualquier pantalla; los destellos (pulsación, error) y el tecleo siguen por encima. Con el LED desactivado en Ajustes no se ve.
   - **Código:** `pomodoro.h/.cpp` (motor, independiente de la interfaz, con `pomodoroTick()` en el `loop`), `zumbador.h/.cpp` (`tone()` sin bloquear), `ledPomodoro()` en `led`, y `MenuPomodoro`/`PantallaPomodoro` en `ui.cpp`. Duraciones, tono (2 kHz, 250 ms) y pin en `config.h`.
   - **Zumbador y corriente:** el de pranayama-helper lleva 100 Ω, pero esa placa va a 5 V. Un GPIO del RP2040 da como mucho 12 mA. Con un zumbador **piezo** (con el polímetro, sin continuidad o MΩ) los 100 Ω valen. Con uno **magnético** (bobina de 16-42 Ω) saldrían ~28 mA: o una resistencia de 330 Ω (~9 mA, suena menos) o un transistor NPN (BC337/2N2222, 1 kΩ en la base, zumbador entre 3V3 o VBUS y el colector, con diodo en paralelo).

10. **Mantener sesión (2026-11-15, probado en la placa: funciona).** Probado en Linux (bspwm, `xss-lock` + `i3lock` con el protector de pantalla de X11): activo, el PC no se bloquea; desactivado, sí.
   - **Qué hace:** mientras está activo, mueve el ratón 1 px y lo devuelve cada 30 s (`MANTENER_INTERVALO_MS`), así que el PC no bloquea la sesión ni apaga la pantalla. El cursor no se mueve de sitio. Se eligió el ratón y no una tecla inocua (F15, Bloq Despl): es más fiable entre sistemas y no se mezcla con lo que se escribe.
   - **USB:** el Ardutype pasa a ser **teclado + ratón** (dispositivo compuesto, librería `Mouse` del core). El PC lo vuelve a reconocer la primera vez.
   - **Duración:** 30 min, 1 h, 2 h, 4 h o sin límite; al acabar se apaga solo. Así, si se olvida, el PC acaba bloqueándose.
   - **Nunca con el PC suspendido** (`tud_suspended()`): el movimiento podría despertarlo.
   - **Interfaz:** menú principal → «Mantener sesión» (si está activo, la entrada muestra lo que queda: «Sesión: 1 h 23 min»). Con él activo, el menú empieza por «Desactivar». Como el pomodoro, funciona con el Ardutype bloqueado: en la pantalla de bloqueo, **abajo** abre el menú, y la línea de abajo alterna también con su estado.
   - **Código:** `mantener.h/.cpp`, con `mantenerTick()` en el `loop`; `MenuMantener` en `ui.cpp`. De paso, `Pantalla::trasBloqueo()` sustituye a `sinBoveda()`: dice qué pantalla sigue a la vista si salta el bloqueo automático.
   - **Aviso:** en un equipo de trabajo, el bloqueo por inactividad es un control de seguridad; saltárselo suele ir contra la política de IT, y algunos antivirus detectan los «jigglers» USB. Uso razonable: presentaciones, lecturas largas, compilaciones o descargas en el propio equipo. Un HID solo envía entradas: no sabe si hay alguien delante.

11. **Notificaciones del PC (2026-11-16, probado en la placa: funciona).** Probado: aceptar bloqueado (pide el PIN), denegar, caducar y matar el script a medias (la pregunta se retira sola). Prototipo de la idea 9.2 de Vault 1984: el Ardutype avisa con pitido y LED y deja **aceptar o denegar** preguntas que llegan del PC (scripts de bash y, más adelante, las peticiones de permiso de Claude Code).
   - **Protocolo** (puerto serie, `admin.h`), también con el Ardutype bloqueado: `AVISO [MUDO] <origen> <texto>` (`MUDO`: solo LED y pantalla, sin pitido); `PREGUNTA <id> <segundos> <origen> <texto>`, que contesta `@RESPUESTA <id> SI|NO|TIEMPO`; `RETIRAR <id>`; `QUITAR <origen>` (quita el aviso si es de ese origen; si ya no está, también contesta `@OK`). Textos en hexadecimal, como el resto. Una notificación a la vez: una pregunta sustituye a un aviso; otra pregunta encima recibe `@ERR ocupado`. Rechazadas mientras se sincroniza.
   - **En el aparato:** pitido de 2800 Hz (más agudo que el del pomodoro), LED **violeta** parpadeando (lento un aviso, rápido una pregunta) por encima del pomodoro, y la pantalla se enciende. Las preguntas repiten el pitido cada 30 s. Se pone encima de lo que haya, salvo mientras se teclea (la cuenta atrás seguiría a ciegas), en la sincronización o con la clave de recuperación en pantalla: entonces espera.
   - **Teclas:** arriba/abajo desplazan el texto (20 caracteres por fila, sin cortar nada). Aviso: cualquier otra tecla lo quita. Pregunta: izquierda deniega; centro o derecha llevan a «No / Sí» con **No primero**; aceptar son tres gestos a propósito. Las teclas de los primeros 0,6 s se ignoran (un toque por inercia no contesta).
   - **Seguridad:** es comodidad, no seguridad (un programa malicioso del PC no necesita preguntar). Reglas: sin respuesta a tiempo = `TIEMPO`, nunca sí; **bloqueado, aceptar pide el PIN** (denegar no), y ese PIN deja el Ardutype desbloqueado; una notificación **enciende la pantalla pero no retrasa el bloqueo automático** (si no, el PC podría mantenerlo abierto a base de avisos); si quien pregunta cierra el puerto 2 s seguidos, la pregunta se retira.
   - **PC:** `herramienta/notificar.py`, solo con la biblioteca estándar (sin pyserial: vale desde cualquier script y desde los *hooks*). `notificar.py aviso [-o ORIGEN] [--mudo] TEXTO`, `notificar.py quitar [-o ORIGEN]`, `notificar.py pregunta [-o ORIGEN] [-t SEGUNDOS] [-e SEGUNDOS] [--sin-terminal] TEXTO`; enlazado como `avisar` o `preguntar`, el modo sale del nombre. Sale con 0 (entregado / aceptado), 1 (denegado), 2 (sin respuesta) o 3 (error). Bloquea el puerto con `flock` mientras lo usa: dos preguntas esperan turno, y `ardutype_sync.py` abre ahora el puerto con `exclusive=True` (el mismo `flock`). La pantalla solo dibuja Latin-1: el resto de caracteres sale como «?».
   - **`preguntar` primero en la terminal (2026-11-16, probado: funciona).** Si hay terminal y el programa está en primer plano, pregunta ahí (`[s/n]`); si en 10 s (`-e`, `ARDUTYPE_ESPERA`; 0 = a la vez) no hay respuesta, pregunta también en el Ardutype y vale la primera que llegue (la otra se retira, o se borra lo tecleado a medias). `-t` es el plazo total. Sin terminal (cron, hooks, segundo plano) pregunta directamente en el Ardutype. Con terminal, un fallo del Ardutype no corta: se sigue esperando en la terminal; si está ocupado, se reintenta cada segundo.
   - **Código:** `notificacion.h/.cpp` (estado, plazo, pitido, puerto cerrado) con `notiTick()` en el `loop`; órdenes en `admin.cpp`; `PantallaNoti` y `sincronizarNoti()` en `ui.cpp`; `ledNotificacion()` en `led.cpp`.
   - **Atajos:** `~/.local/bin/avisar` y `~/.local/bin/preguntar` enlazan a `notificar.py`.
   - **Claude Code, primera versión** (`herramienta/claude_permiso.py`, hooks en `~/.claude/settings.json`; probada y retirada el 2026-11-16): `PermissionRequest` → pregunta en el Ardutype con el resumen de lo que se pide (`Bash: <comando> (<descripción>)`, `Edit: <fichero>`...), 90 s de plazo (`ARDUTYPE_PLAZO_PERMISO`). Aceptar o denegar ahí contesta a Claude Code; sin respuesta o sin Ardutype no dice nada y la petición sale en la terminal como siempre (nunca falla hacia «aceptar»). `Notification` (asíncrono) → aviso con el mensaje de Claude Code (espera respuesta, necesita permiso).
   - **Claude Code, versión actual (2026-11-16, probado: funciona).** Mientras el hook `PermissionRequest` espera, Claude Code no deja contestar en la terminal, y no hay forma de preguntar primero en la terminal. **Decisión del usuario: contestar siempre en la terminal**; se quita ese hook (el modo `permiso` de `claude_permiso.py` sigue ahí, sin usar) y el Ardutype solo avisa:
     - `Notification` (asíncrono): `permission_prompt` (salta ~6 s después de la petición si no se ha tecleado nada) y `elicitation_dialog` → aviso **con pitido**; `idle_prompt` (~60 s después de terminar Claude, si no se ha escrito nada) → aviso **mudo, solo LED**; el resto de tipos se ignoran.
     - `PostToolUse`, `PostToolUseFailure`, `Stop` y `UserPromptSubmit` (asíncronos) → `claude_permiso.py quitar`: el aviso ya se contestó o ya no hace falta, y se apaga el LED. Claude Code no tiene un evento de «permiso contestado»; estos lo cubren (aceptado → la herramienta se ejecuta; denegado → Claude sigue o termina). Un archivo de marca (`$XDG_RUNTIME_DIR/ardutype-aviso-claude-<uid>`) hace que solo se abra el puerto si queda un aviso puesto.
     - Los hooks añadidos a mitad de sesión no se cargan hasta reiniciar Claude Code (`claude --continue`).
     - Copia de los hooks en `herramienta/claude_hooks.json` (el bloque `hooks` de `~/.claude/settings.json`, que no está en ningún repo): para rehacerlos, copiar ese bloque en `settings.json`.

12. **Azar (2026-11-17, probado en la placa: funciona).** Prototipo de la idea 9.1 de Vault 1984: menú principal → **«Azar»**. No toca la bóveda, así que también funciona **bloqueado**: en la pantalla de bloqueo, **izquierda** lo abre.
   - **Fuente (`azar.h/.cpp`):** 32 bytes del RANDOM del ATECC608B (solo si está provisionado; antes da un patrón fijo) y 32 del generador de hardware del RP2040, mezclados con SHA-256 junto a la semilla anterior y un contador. Eso es la clave de un **ChaCha20** que da los bytes (BearSSL del core). Semilla nueva en cada generación. **Sin sesgo de módulo** (descartar y repetir). La contraseña de «Generar (16)» al dar de alta un servicio también usa ya esta fuente (antes, solo el RP2040).
   - **Funciones:** Contraseña (longitud 4-64; casillas `[x] a-z`, `[x] A-Z`, `[x] 0-9`, `[x] Símbolos`, `[x] Excl. parecidos`, al menos un conjunto y al menos un carácter de cada uno), Diceware (3-10 palabras; separador `-`, espacio, `.` o ninguno), PIN (4-12 cifras), Moneda (1-20; «C X X C», resumen «2C 2X»), Dados (1-10; d4, d6, d8, d10, d12, d20, d100; suma), Números (desde, hasta 0-999999, cuántos 1-20, repetidos sí/no), Color (`#RRGGBB` y el **LED de ese color**), Alias (`palabra-palabra-NN`), MAC (administrada localmente, unicast), Alfanumérico 32, Token 64 (a-z0-9), Hexadecimal 32, UUID v4, **Prueba del azar** y «Sonido ruleta».
   - **Diceware con la lista de siempre** (`dice_lista.h`, 1459 palabras, ~10,5 bits por palabra): decisión del usuario, para no cambiar las palabras de la clave de recuperación ya generada. 6 palabras ≈ 63 bits; 8 ≈ 84 bits.
   - **Efecto ruleta:** antes del resultado pasan valores al azar cada vez más despacio (de 35 ms a 300 ms por paso, ~1,5 s), con un clic del zumbador en cada uno (se silencia con «Sonido ruleta»); en Color, el LED cambia a la vez. Una tecla lo salta.
   - **Pantalla del resultado:** en grande si cabe; si no, en filas de 20 caracteres (arriba/abajo desplazan). La cabecera alterna cada 3 s entre el nombre y «C teclea, D otra», con los bits (o la suma, el resumen, el rango) a la derecha. **Centro teclea** (con la cuenta atrás de siempre), **derecha genera otro**, **izquierda sale** y borra el resultado de la RAM y el color del LED (también si desaparece la pantalla por el bloqueo automático).
   - **Sin saltos de fuente (corregido tras la primera prueba):** el tamaño se decidía con cada valor de la ruleta, así que con números de 0 a 99 alternaba entre grande y pequeña. Ahora se decide una vez por generación con el caso más largo posible, y en Números, Dados, Moneda y PIN cada valor tiene un **hueco fijo** alineado a la derecha (el 7 ocupa el sitio del 43); en filas, con la fuente de ancho fijo y relleno de espacios. Lo tecleado no lleva ese relleno.
   - **Editar números:** hasta 999, arriba/abajo de uno en uno (mantenidos, se repiten y aceleran); desde 1000 (desde/hasta), por cifras: izquierda/derecha eligen la cifra y arriba/abajo la cambian. Centro acepta; izquierda en la primera cifra cancela.
   - **Prueba del azar:** tira 6000 dados y dibuja las seis barras con su recuento; al acabar, **chi²** (5 grados de libertad): por debajo de 11,07 «bien», hasta 15,09 «dudoso», más «raro» (pasa menos del 1 % de las veces con un azar bueno). Indica si la semilla llevaba el ATECC.
   - **Parámetros recordados en `/azar.bin`**, aparte de la bóveda (no son secretos; así se guardan también bloqueado y sin cambiar el formato de la bóveda).
   - **LED:** el color del azar va por encima de todo, con un brillo mínimo de 60/255 para que se distinga.
   - **Código:** `azar.h/.cpp` (fuente y generadores, sin interfaz); en `ui.cpp`, `EditarNumero`, `PantallaResultado`, `PantallaPrueba`, `MenuParametros` (genérico, por tabla de parámetros) y `MenuAzar`; `ledColorAzar()` en `led`; `generador.cpp` se queda como envoltorio.

13. **Interruptor del hombre muerto (2026-11-17, probado en la placa).** Prototipo de la idea 9.5 de Vault 1984: si nadie toca el joystick durante el plazo de una acción, el Ardutype la lanza en el PC. A diferencia del bloqueo por inactividad del PC (que reinicia cualquiera que toque el PC, y también «mantener sesión»), este solo lo reinicia quien tiene el aparato. Funciona también con el Ardutype bloqueado.
   - **Acciones** (menú principal → «Hombre muerto»; interruptor general «Activo» y, por acción, `[x] Activa`, «Plazo» de 1 a 480 min y «Probar ahora» con la cuenta atrás del tecleo):
     - **Bloquear PC** (por defecto sí, 15 min): pulsa **Super+L**. Windows lo trae de serie; en el parrot se ha añadido a sxhkd (`super + l` → `xset s activate`, que dispara xss-lock + i3lock). Desactiva «mantener sesión».
     - **Suspender PC** (no, 60 min): la tecla **Sleep** de control de consumo (`KEY_SLEEP`, 0x32, por el informe de consumo que ya registra la librería `Keyboard`): logind la atiende (`HandleSuspendKey=suspend`) aunque i3lock tenga el teclado, cosa que un atajo de sxhkd no conseguiría. El parrot no tiene swap: hibernar no es posible; xss-lock bloquea al suspender.
     - **Guion** (no, 30 min): un guion propio con la sintaxis de los de los servicios (TECLA, PAUSA, ESCRIBIR), sin `$usuario` ni `$contrasena`. Va **sin cifrar** en `/muerto.txt` (fuera de la bóveda, para poder ejecutarse bloqueado): nada de secretos. **Se omite si el bloqueo ya ha saltado en esa ausencia** (se teclearía en la pantalla de bloqueo); el menú avisa si su plazo es mayor que el del bloqueo («Guion tras bloqueo: no irá»). Con el mismo plazo, va antes el guion, luego el bloqueo y luego suspender. Al cambiar el plazo de una acción desactivada, el menú avisa («Ojo: está desactivada»; o «Ojo: hombre muerto apagado» si lo está el interruptor general).
   - **Aviso:** en el último minuto antes de cada acción (en los últimos 30 s si el plazo es de 1 min), pitido de 1400 Hz (más grave que los demás) cada 15 s, LED rojo anaranjado parpadeando deprisa (por encima de las notificaciones) y pantalla «Bloquear PC en 42 s / Pulsa una tecla», que enciende el OLED sin retrasar el bloqueo del Ardutype. **Cualquier pulsación** (aunque solo despierte la pantalla) reinicia todos los plazos. Al lanzarse cada acción sale un aviso («Bloquear PC: hecho», «Guion omitido», «…: sin PC»).
   - **Una vez por ausencia:** cada acción se lanza una sola vez; aunque siga sin tocarse el joystick, no se repite (ni el guion ni el bloqueo) hasta la siguiente pulsación, que vuelve a armar todos los plazos desde cero. Las notificaciones del PC no cuentan como señal de vida (las podría mandar cualquiera que use el PC).
   - **Sin USB o con el PC suspendido** no hace nada (la acción se da por hecha hasta la siguiente pulsación).
   - **«Probar ahora»:** la misma cuenta atrás que el tecleo, pero con «Va a lanzarse.» en vez de «Pon el cursor.».
   - **Cargar el guion:** se escribe en `~/.config/ardutype/muerto.guion` (plantilla con ejemplos para Windows y para el parrot) y `ardutype-muerto cargar` (enlace a `herramienta/muerto.py`, solo biblioteca estándar; reutiliza la conexión y el `flock` de `notificar.py`) lo envía sin comentarios ni líneas en blanco (máx. 511 bytes). Pide el Ardutype desbloqueado y confirmarlo en su pantalla («¿Guion de N líneas?»). `ardutype-muerto ver` lo muestra; `ardutype-muerto borrar` lo quita. Protocolo: `MUERTO` → `@MUERTO <guion>`; `MUERTO <guion|->` → confirmación → `@OK`; errores `@ERR variables`, `@ERR sintaxis <línea> <motivo>`, `@ERR cancelado`.
   - **Código:** `muerto.h/.cpp` (motor, configuración en `/muerto.bin`, `muertoTick()` en el `loop`), `MenuMuerto`, `MenuAccionMuerto`, `PantallaMuerto` y `sincronizarMuerto()` en `ui.cpp` (`muertoSenal()` con cada tecla), orden `MUERTO` en `admin.cpp`, `ledMuerto()` en `led`; constantes en `config.h`.

14. **Agente SSH con la clave en el ATECC (2026-11-17, probado en la placa).** Clave **ECDSA P-256** creada dentro del ATECC608B (ranura 0) que nunca sale de él; sirve para entrar en GitHub (`git push`) y para **firmar commits** (firmas SSH de git). Primer paso de la criptografía; el modelo (agente en el PC, confirmación en pantalla, GenKey y Sign en el chip) es el mismo que usará Vault 1984.
   - **Ranuras libres** (`herramienta/atecc_ranuras.py`, solo lectura; órdenes `ATECC` y `ATECCPUB n`): la configuración de fábrica deja las ranuras **0, 1 y 2** como claves privadas P-256 con firma de mensajes externos y **GenKey permitido aun con el chip bloqueado**, y ninguna ranura bloqueada aparte. Reparto: 0 SSH; 1 y 2 (además ECDH) reservadas para cifrar archivos y FIDO2.
   - **Limitación conocida:** esas ranuras no exigen autorización (`ReqAuth`), y la configuración ya no se puede cambiar: el **PIN lo exige solo el firmware**. Quien tenga el aparato y grabe otro firmware podría firmar (no copiar la clave) mientras lo tenga, o sustituirla con GenKey. Aceptado para SSH/git: si se pierde el Ardutype, se revoca la clave en GitHub (la clave de siempre, `id_ed25519_github`, queda de reserva). No vale para firma con valor legal. En Vault 1984: `ReqAuth` con la clave de autorización dentro de la bóveda, GenKey una sola vez y ranura bloqueada, bus I2C cifrado y arranque seguro.
   - **Firmware:** `SSHPUB` → `@SSHPUB <X||Y>` (también bloqueado: la pública no es secreta; `@ERR sinclave` si no hay); `SSHGENERAR` → «¿Crear clave SSH?» / «¿Sustituir clave SSH?» → GenKey modo 4; `SSHFIRMAR <resumen> <motivo>` → «¿<motivo>?» con pitido → Nonce *pass-through* con el SHA-256 en TempKey + Sign externo → `@FIRMA <r||s>`. Firma **a ciegas**: el resumen lo calcula el PC y el motivo («Firmar commit», «Login SSH git», «Firmar archivo») lo pone el agente, sin que el Ardutype lo pueda comprobar; lo que garantiza es que nada se firma sin confirmarlo. Código en `atecc.cpp` (`ateccClavePublica`, `ateccGenerarClave`, `ateccFirmar`) y `admin.cpp`.
   - **PC:** `herramienta/agente_ssh.py` (`ardutype-ssh`; solo biblioteca estándar): `crear`, `publica` (guarda `~/.config/ardutype/ssh.pub`) y `agente`, que habla el protocolo de ssh-agent en `$XDG_RUNTIME_DIR/ardutype-ssh.sock` (identidades y firmas; lo demás, `FAILURE`). Con el Ardutype bloqueado le pone el aviso «Desbloquea para firmar» y espera 60 s. Servicio de usuario `herramienta/ardutype-ssh.service` (enlazado en `~/.config/systemd/user`, habilitado).
   - **Uso:** en `~/.ssh/config`, `Host github-ardutype` con `IdentityAgent ${XDG_RUNTIME_DIR}/ardutype-ssh.sock` e `IdentityFile ~/.config/ardutype/ssh.pub`; `github.com` sigue con la clave de siempre. Firma de commits por repositorio: `gpg.format=ssh`, `user.signingkey=~/.config/ardutype/ssh.pub`, `gpg.ssh.program=ssh-keygen-ardutype` (envoltorio que apunta `SSH_AUTH_SOCK` al agente), `gpg.ssh.allowedSignersFile=~/.config/ardutype/allowed_signers`, `commit.gpgsign=true`. En GitHub, la misma pública como *Authentication Key* y como *Signing Key*.
   - **Probado:** firma de archivo verificada con `ssh-keygen -Y verify`, `ssh -T github-ardutype` y commits firmados en este repositorio.

15. **Cifrar y firmar archivos (2026-11-17, probado en la placa).**
   - **Firmar:** no necesita nada nuevo en el firmware: es la firma SSH de la fase 14 con el espacio de nombres `file` (`ssh-keygen -Y sign`), un formato estándar que verifica cualquiera con OpenSSH. `ardutype-cifrado firmar ARCHIVO` → `ARCHIVO.sig` (pregunta «¿Firmar archivo?»); `ardutype-cifrado verificar ARCHIVO` lo comprueba contra `~/.config/ardutype/allowed_signers`. Probado: firma buena; tras cambiar el archivo, «FIRMA INCORRECTA».
   - **Cifrar:** con **age** (formato estándar, `age` 1.2.1 del sistema) y un plugin propio, `age-plugin-ardutype` (`herramienta/age_plugin_ardutype.py`, con la librería `cryptography` del sistema). Clave P-256 en la **ranura 1** del ATECC (la de fábrica permite ECDH con el secreto en claro). **La ranura 2 no se usa nunca para ECDH:** su configuración escribiría el secreto en la ranura 3 (ADMIN).
     - **Esquema** (como el X25519 de age): par efímero `e, E`; `S = ECDH(e, P)`; `k = HKDF-SHA256(S, sal = E‖P, info = "age-plugin-ardutype/v1")`; la clave del archivo va cifrada con ChaCha20-Poly1305(`k`). Estrofa `-> ardutype <SHA-256(P)[:4]> <E comprimida>`. Destinatario `age1ardutype1…` y identidad `AGE-PLUGIN-ARDUTYPE-1…`; las dos llevan solo la pública: la identidad no es secreta.
     - **Cifrar no necesita el Ardutype** (solo la pública); **descifrar** sí: el chip calcula `S` tras confirmar «¿Descifrar archivo?» (con pitido; si está bloqueado, aviso «Desbloquea para descifrar»). Antes de pedir la ECDH, el plugin comprueba que la pública del Ardutype es la de la identidad.
     - **Reserva:** la privada del chip no tiene copia; todo se cifra también para una **clave age normal de reserva** (`age1twsnhdk…`), cuya privada está **solo en KeePassXC** (entrada «age reserva Ardutype»; generada en tmpfs, probada y borrada con `shred`). Para usarla: copiarla a un archivo temporal, `age -d -i` y borrarlo.
     - **Firmware:** `CIFPUB` (también bloqueado), `CIFGENERAR` («¿Crear cifrado?» / «¿Sustituir cifrado?»: sustituirla deja sin poder abrir lo cifrado solo para la anterior) y `ECDH <pública> <motivo>` → `@ECDH <secreto>`; `ateccEcdh()` en `atecc.cpp`. El secreto viaja en claro al PC (USB, no I2C ajeno): protege que nada se descifre sin confirmarlo y que la privada no salga.
   - **Herramienta:** `ardutype-cifrado` (`herramienta/cifrado.py`): `crear`, `destinatario`, `cifrar` (→ `.age`, para todos los de `~/.config/ardutype/destinatarios.txt`; avisa si no hay reserva), `descifrar`, `firmar`, `verificar`. Probado de punta a punta (cifrar, descifrar con el chip, archivo idéntico) y, antes, con `age` real y una clave simulada, también descifrando con la reserva.

### Posibles mejoras (ideas)

- **Modo «mantener sesión» (keep-alive):** hecho, ver fase 10.
- **Disco cifrado en una SD (idea, 2026-11-15; proyecto para aprender).** El Ardutype se presentaría también como **disco USB** (*mass storage*, TinyUSB): bloqueado, el PC ve un lector **sin tarjeta**; con el mismo desbloqueo que la bóveda aparece el disco. Cada sector de 512 bytes se cifra/descifra al vuelo con **AES-XTS** (el modo de BitLocker, LUKS y VeraCrypt); la SD solo guarda datos cifrados. La clave de los datos, aleatoria y envuelta con el PIN y el ATECC como la de la bóveda. El PC formatea el disco virtual (FAT/exFAT): funciona en cualquier sistema **sin instalar nada**.
  - **Hardware:** módulo SD para Arduino (SPI) del cajón, en los pines SPI0 libres de la KB2040 (SCK, MO, MI) y un CS libre (D7, D9 o D10). **Ojo:** muchos de esos módulos llevan regulador a 3,3 V y adaptador de niveles pensados para 5 V: alimentarlo desde VBUS (5 V), no desde 3V3 (el regulador no daría su tensión); comprobar el modelo.
  - **Velocidad:** USB *Full Speed* y AES por software en el RP2040: **~0,5-1 MB/s**. Bien para documentos y fotos sueltas; no para vídeo. Para más: placa con USB *High Speed* y AES por hardware (p. ej. Teensy 4.1, 10-20 MB/s).
  - **Seguridad:** las mismas limitaciones que la revisión de seguridad (firmware sin verificar, escucha del bus del ATECC); para datos sensibles, más razón para el RP2350 con arranque seguro. XTS no autentica (como todo cifrado de disco).
  - **En la práctica, mientras tanto:** pendrive normal con **LUKS** (o VeraCrypt) y el Ardutype tecleando su contraseña larga: más rápido y con cifrado auditado; necesita LUKS/VeraCrypt en el PC.
- **Aleatoriedad por hardware (TRNG del ATECC608).** Cuando esté el chip, el generador de contraseñas tiraría de su **TRNG** (un DRBG conforme a NIST, 32 bytes por llamada) en vez de un PRNG software. **Corrección (2026-11-12):** el RANDOM no toca la configuración, pero **mientras la zona de configuración no está bloqueada devuelve un patrón fijo** (`FF FF 00 00` repetido), no aleatorios: solo sirve después del bloqueo.
  - **Modo passphrase (Diceware) para secretos que se memorizan** (p. ej. una contraseña maestra): la lista de palabras es **pública** a propósito (principio de Kerckhoffs); la seguridad está en la **elección aleatoria**, no en ocultar la lista. Con la lista estándar de 7776 palabras son ~12,9 bits por palabra: 6 palabras ≈ 77 bits (7776⁶ ≈ 2⁷⁷ combinaciones). Requisito: que la elección venga de un **buen RNG** — justo lo que da el TRNG.
  - Para contraseñas que **se guardan** (no se memorizan), siguen siendo mejores los **caracteres aleatorios** (más entropía por longitud). Diceware es para lo que hay que recordar.
  - **Entrada por palabras (mecánica validada en PoC).** Introducir la maestra palabra a palabra: con la rueda (solo a-z) tecleas letras y se filtra la lista por prefijo. Enfoque «deletreo-primero»:
    - **Lista:** castellano, derivada de DW-Espanol-1 (D. Reinhold), filtrada a solo **a-z minúsculas y ≥4 letras**, y reducida a **UNA palabra por prefijo de 3 letras** (la más corta) → **1459 palabras, ~10,5 bits/palabra** (7 ≈ 74 b, 8 ≈ 84 b). Así **3 letras determinan la palabra**. Sin tildes ni ñ (descartadas para poder teclear con la rueda a-z). En `ardutype/dice_lista.h`.
    - **Teclas:** arriba/abajo cambian la última letra **saltando las que no dan palabra** (nunca aparece «ninguna»); derecha añade otra letra; izquierda borra letra o, vacío, la última palabra; centro acepta la palabra mostrada; centro largo entrega la frase.
    - **PoC:** clase `RuedaPalabras` en `ui.cpp`, antes accesible en Ajustes → «PoC frase», que tecleaba la frase por USB; **quitada al pasar a producción (2026-11-14)**, la clase queda solo para la clave de recuperación. Cabe de sobra en flash (RP2040, 8 MB). **No reduce la entropía** (sigues eligiendo tus palabras); solo baja los toques frente a teclear ~20 caracteres.
    - **Alternativa:** 2 palabras por prefijo (2388, 11,2 bits) si se quieren menos palabras, a cambio de un toque para elegir entre 2. Descartada la lista EFF inglesa.
    - **Pendiente (pantalla real):** que la frase alimente la derivación (PBKDF2 → bóveda) en vez de teclearse, con mínimo de palabras / aviso de bits, enganchada al desbloqueo; se verá junto al ATECC608.
- **Más usos de las notificaciones (ideas, 2026-11-16; ver fase 11).** Todo con `avisar` / `preguntar`, sin tocar el firmware salvo que se diga:
  - **Tareas largas:** `make; avisar "Compilado"`, o un alias `hecho` que avise al terminar cualquier comando: con pitido si falla, mudo (solo LED) si va bien.
  - **Confirmaciones en scripts:** `preguntar "¿Restaurar la bóveda?" && ./restaurar.sh`; antes de un `git push --force` o un `rm -r` grande.
  - **Avisos del sistema:** conflictos de Syncthing (p. ej. en la base de KeePassXC), un respaldo de cron que falla, disco casi lleno, un `ssh` entrante al PC.
  - **Calendario y tiempo:** recordatorios de reuniones; fin del pomodoro del PC (el aparato ya tiene el suyo, fase 9).
  - **Claude Code:** avisar cuando termina un agente en segundo plano (`agent_completed`, ahora se ignora); que el aviso de «he terminado» diga en pantalla qué ha terminado.
  - **`sudo` con confirmación en el aparato, con cuidado:** es comodidad, no seguridad (fase 11), así que solo como un paso más, nunca en lugar de la contraseña.

## 11. Preguntas abiertas

1. Resuelta: unas 50 entradas.
2. Resuelta: español el 95 % del tiempo, distribución seleccionable (ES Linux, ES Windows, US). Sin Mac.
3. Resuelta: las dos cosas; se podrá gestionar tanto desde el PC como desde el dispositivo.
4. Resuelta: sí, habrá **clave de recuperación** en papel (segundo desbloqueo).
5. Sin concretar: los scripts son una opción interesante, no un requisito. Prioridad baja.
6. Resuelta: PCB 80 × 20 mm, OLED 38 × 12 mm, montaje en dos capas y joystick genérico "5 Way Switch DIP 10×10×10 mm" (lote de 5). Al recibirlo: contar patillas, medir el paso y mapear con el polímetro qué patilla es el común y cuáles son las 5 direcciones.
7. Resuelta: no se migra write-pass. Sea cual sea la respuesta, `config.h` con la contraseña de trabajo en claro no debería ir a git ni a Syncthing.
8. Resuelto: RESET y BOOT están junto al USB-C; el NeoPixel en el extremo opuesto, junto a CLK, queda tapado por el OLED y se sacrifica como indicador de estado (el OLED ya lo muestra). Pendiente solo medir con el calibre la anchura exacta de la zona de RESET/BOOT al recibir la placa.
