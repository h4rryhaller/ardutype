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
   - **PIN de 6 cifras** (decisión del usuario, 2026-11-12): con como mucho 41 intentos, acertar es un 0,004 % (con 4 cifras, 0,4 %).
   - **Clave de recuperación en palabras** (decisión del usuario, 2026-11-12): **8 palabras** al azar de `dice_lista.h` (1459 palabras, ~84 bits), con el mismo PBKDF2 calibrado (~1 s) que el PIN, ya que no pasa por el chip (ranura `RANURA_PALABRAS`). Se enseña al crear la bóveda en 4 páginas de 2 palabras numeradas (derecha/izquierda pasan página; centro largo confirma desde la última). Se mete con la rueda de palabras de la PoC, generalizada (3 letras + centro por palabra; al completar las 8 se comprueba sola). Las bóvedas anteriores siguen aceptando su clave base32. Descartadas: 12 palabras sin derivación lenta (126 bits) y dejar el base32.

### Posibles mejoras (ideas)

- **Modo «mantener sesión» (keep-alive).** Un modo opcional, en segundo plano, que mientras está activo simule actividad para que el equipo no bloquee la sesión ni apague la pantalla (p. ej. durante una presentación o una lectura larga).
  - **Teclado:** enviar una tecla inocua cada N segundos: F15 (no hace nada visible) o alternar Bloq Despl.
  - **Ratón:** el RP2040 puede presentarse también como **ratón HID** (dispositivo compuesto teclado+ratón); un micro-movimiento del cursor (1 px y vuelta) suele ser más fiable que una tecla y no interfiere con lo que se escriba. Habría que habilitar la interfaz de ratón en el descriptor USB.
  - **No es un guion:** los guiones se ejecutan una vez, bajo demanda y con cuenta atrás; esto sería un **modo periódico propio**.
  - **Limitación (no puede «vigilar» al usuario):** un dispositivo HID solo *envía* entradas; no sabe si hay alguien delante ni recibe del sistema el estado de inactividad (lo único que un teclado recibe del host son los LEDs de bloqueo). Para que solo actúe con el usuario presente haría falta un **sensor de presencia** (PIR) en el aparato o un **ayudante en el PC** que le informe por el canal serie.
  - **Aviso:** en un equipo de trabajo, el bloqueo por inactividad es un control de seguridad; saltárselo suele incumplir la política de IT y algunos endpoints detectan HID «jiggler». Uso razonable: presentaciones/lecturas largas; para el cierre de sesión del trabajo, consultarlo antes con IT.

- **Aleatoriedad por hardware (TRNG del ATECC608).** Cuando esté el chip, el generador de contraseñas tiraría de su **TRNG** (un DRBG conforme a NIST, 32 bytes por llamada) en vez de un PRNG software. **Corrección (2026-11-12):** el RANDOM no toca la configuración, pero **mientras la zona de configuración no está bloqueada devuelve un patrón fijo** (`FF FF 00 00` repetido), no aleatorios: solo sirve después del bloqueo.
  - **Modo passphrase (Diceware) para secretos que se memorizan** (p. ej. una contraseña maestra): la lista de palabras es **pública** a propósito (principio de Kerckhoffs); la seguridad está en la **elección aleatoria**, no en ocultar la lista. Con la lista estándar de 7776 palabras son ~12,9 bits por palabra: 6 palabras ≈ 77 bits (7776⁶ ≈ 2⁷⁷ combinaciones). Requisito: que la elección venga de un **buen RNG** — justo lo que da el TRNG.
  - Para contraseñas que **se guardan** (no se memorizan), siguen siendo mejores los **caracteres aleatorios** (más entropía por longitud). Diceware es para lo que hay que recordar.
  - **Entrada por palabras (mecánica validada en PoC).** Introducir la maestra palabra a palabra: con la rueda (solo a-z) tecleas letras y se filtra la lista por prefijo. Enfoque «deletreo-primero»:
    - **Lista:** castellano, derivada de DW-Espanol-1 (D. Reinhold), filtrada a solo **a-z minúsculas y ≥4 letras**, y reducida a **UNA palabra por prefijo de 3 letras** (la más corta) → **1459 palabras, ~10,5 bits/palabra** (7 ≈ 74 b, 8 ≈ 84 b). Así **3 letras determinan la palabra**. Sin tildes ni ñ (descartadas para poder teclear con la rueda a-z). En `ardutype/dice_lista.h`.
    - **Teclas:** arriba/abajo cambian la última letra **saltando las que no dan palabra** (nunca aparece «ninguna»); derecha añade otra letra; izquierda borra letra o, vacío, la última palabra; centro acepta la palabra mostrada; centro largo entrega la frase.
    - **PoC:** clase `RuedaPalabras` en `ui.cpp`, accesible en Ajustes → «PoC frase»; teclea la frase por USB para probarla. Cabe de sobra en flash (RP2040, 8 MB). **No reduce la entropía** (sigues eligiendo tus palabras); solo baja los toques frente a teclear ~20 caracteres.
    - **Alternativa:** 2 palabras por prefijo (2388, 11,2 bits) si se quieren menos palabras, a cambio de un toque para elegir entre 2. Descartada la lista EFF inglesa.
    - **Pendiente (pantalla real):** que la frase alimente la derivación (PBKDF2 → bóveda) en vez de teclearse, con mínimo de palabras / aviso de bits, enganchada al desbloqueo; se verá junto al ATECC608.

## 11. Preguntas abiertas

1. Resuelta: unas 50 entradas.
2. Resuelta: español el 95 % del tiempo, distribución seleccionable (ES Linux, ES Windows, US). Sin Mac.
3. Resuelta: las dos cosas; se podrá gestionar tanto desde el PC como desde el dispositivo.
4. Resuelta: sí, habrá **clave de recuperación** en papel (segundo desbloqueo).
5. Sin concretar: los scripts son una opción interesante, no un requisito. Prioridad baja.
6. Resuelta: PCB 80 × 20 mm, OLED 38 × 12 mm, montaje en dos capas y joystick genérico "5 Way Switch DIP 10×10×10 mm" (lote de 5). Al recibirlo: contar patillas, medir el paso y mapear con el polímetro qué patilla es el común y cuáles son las 5 direcciones.
7. Resuelta: no se migra write-pass. Sea cual sea la respuesta, `config.h` con la contraseña de trabajo en claro no debería ir a git ni a Syncthing.
8. Resuelto: RESET y BOOT están junto al USB-C; el NeoPixel en el extremo opuesto, junto a CLK, queda tapado por el OLED y se sacrifica como indicador de estado (el OLED ya lo muestra). Pendiente solo medir con el calibre la anchura exacta de la zona de RESET/BOOT al recibir la placa.
