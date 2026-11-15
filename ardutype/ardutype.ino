// Ardutype: gestor de contraseñas que las teclea como un teclado USB.
//
// Placa: "Adafruit KB2040" (core arduino-pico). Librerías: U8g2 y Adafruit NeoPixel.
//
// FASE 5: sincronización con KeePassXC desde el PC (admin.h, herramienta
// ardutype_sync.py). Almacén cifrado en la flash (boveda.h): al subirlo hay que elegir en
// el IDE Herramientas > Flash Size: "8MB (Sketch: 7MB, FS: 1MB)".
// Pomodoro (pomodoro.h): trabajo, descanso y descanso largo con aviso por el
// zumbador de D8 y el color del LED; sigue contando con el Ardutype bloqueado.
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

void setup() {
  Serial.begin(115200);
  ledIniciar();
  zumbadorIniciar();
  joystickIniciar();
  pantallaIniciar();
  tecladoIniciar();
  bovedaIniciar();
  uiIniciar();
  Serial.println("Ardutype: fase 8 (PIN limitado por el ATECC608)");
}

void loop() {
  uiTick();
  adminTick();
  pomodoroTick();
  ledTick();
  delay(1);
}
