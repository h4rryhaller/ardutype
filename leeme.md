
# Ardutype
Permite almacenar tus contraseñas (de forma no segura) en un Arduno Pro Mini e inyectarlas como si fuese un teclado en un ordenador. De momento me planteo almacenar "Nombre del servicio":"Contraseña1234****". Podrás navegar por un menú (tengo microinterruptores SMD de diferentes tamaños), seleccionar un servicio e inyectar la contraseña. Ya tengo un proyecto similar "write-pass" que inyecta mi usuario y contraseña de trabajo y no podría estar más contento de no tener que introducir esa contraseña intratable y que no puedo cambiar; es una tontería lo que hace, échale un vistazo.

## Brainstorming en 5 minutos
- Ordenar las contraseñas por categorías dentro de las que cuelgan servicios. Ejemplo RR.SS. > Facebook o Banca > Banco1, Banco2.
- Se podrá añadir una nueva categoría
- Se podrá añadir un nuevo servicio 
- Se podrá generar una nueva contraseña para un servicio o introducirla manualmente
- Algunos servicios podrán constar de usuario/contraseña, por lo que será posible almacenar ambos e inyectarlos adecuadamente (usuario > TAB > contrañseña)
- Habrá una categoría scripts en la que podrás grabar rutinas que se ejecutarán bajo demanda del usuario.
- El ardutype deberá estar protegido por una contraseña
- La seguridad será la máxima posible
- Soy consciente de que perder el dispositivo puede generarte un problema de seguridad en caso de pérdida.
- Hay que cuidar el cotenido estético a través de animaciones, logos... 
- Seguro que se te ocurren excelentes ideas y mejoras.

## Fuentes - inspiración
- https://www.instructables.com/PasswordKeeper-From-Aruino-Pro-Micro-or-Why-Keep-I
- https://www.instructables.com/Simple-Serial-Converter-Makes-Pro-Mini-Programming/
- https://www.instructables.com/Mod-a-USB-to-TTL-Serial-Adapter-CP2102-to-program-/

## Materiales
- Arduino Pro Mini
- Conversor USB - TTL. Modelo PL2303HX. es el más similar que he encontrado aunque el mío tiene los pines:
  - 3V3
  - TXD
  - RXD
  - GND
  - +5V
(en lugar de 3V3, 5V, TXD, RXD, GND)
IC14.31818

- OLED Display Module 0.91 Pulgadas I2C - DollaTek. Controlador IC: SSD1306. Tipo interfaz IIC. VCC +(DC 3.3 ~ 6).
  - GND
  - VCC
  . SCL
  - SDA

- Conexiones Arduino - conversor
GND
GND     GND
VCC     +5V
RXI     TXO
TXO     RXD
DTR     -

- Microinterrupores SMD

- PCB 8i0x20mm


