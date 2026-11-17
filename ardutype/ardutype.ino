// Ardutype: gestor de contraseñas que las teclea como un teclado USB.
//
// Placa: "Adafruit KB2040" (core arduino-pico). Librerías: U8g2 y Adafruit NeoPixel.
//
// FASE 5: sincronización con KeePassXC desde el PC (admin.h, herramienta
// ardutype_sync.py). Almacén cifrado en la flash (boveda.h): al subirlo hay que elegir en
// el IDE Herramientas > Flash Size: "8MB (Sketch: 7MB, FS: 1MB)" (config.h no compila
// con otro), o mejor con ../subir.sh, que lo fija.
// Pomodoro (pomodoro.h): trabajo, descanso y descanso largo con aviso por el
// zumbador de D8 y el color del LED; sigue contando con el Ardutype bloqueado.
// Mantener sesión (mantener.h): el Ardutype es también un ratón USB y lo mueve
// 1 px cada 30 s para que el PC no bloquee la sesión.
// Notificaciones (notificacion.h): avisos y preguntas de aceptar o denegar
// desde el PC (herramienta/notificar.py), con pitido y LED violeta.
// Azar (azar.h): moneda, dados, contraseñas, Diceware, PIN, números, color,
// alias, MAC, formatos fijos y una prueba del azar; con el TRNG del ATECC.
// Hombre muerto (muerto.h): sin tocar el joystick, bloquea o suspende el PC
// o ejecuta un guion propio, con aviso un minuto antes.
// FASE 8: ATECC608B en el bus del OLED (atecc.h): el chip limita los intentos
// del PIN. Se provisiona solo, de forma IRREVERSIBLE, la primera vez que se
// abre o se crea la bóveda con él puesto.

#include "config.h"
#include "joystick.h"
#include "pantalla.h"
#include "teclado.h"
#include "boveda.h"
#include "led.h"
#include "admin.h"
#include "ui.h"
#include "zumbador.h"
#include "pomodoro.h"
#include "mantener.h"
#include "notificacion.h"
#include "muerto.h"

void setup() {
  Serial.begin(115200);
  ledIniciar();
  zumbadorIniciar();
  joystickIniciar();
  pantallaIniciar();
  tecladoIniciar();
  mantenerIniciar();
  bovedaIniciar();
  muertoIniciar();
  uiIniciar();
  Serial.println("Ardutype: fase 8 (PIN limitado por el ATECC608)");
}

void loop() {
  uiTick();
  adminTick();
  pomodoroTick();
  mantenerTick();
  notiTick();
  muertoTick();
  ledTick();
  delay(1);
}
