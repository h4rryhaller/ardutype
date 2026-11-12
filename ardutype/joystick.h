#ifndef JOYSTICK_H
#define JOYSTICK_H

#include <Arduino.h>

// Eventos ya filtrados del joystick.
//   - Las direcciones salen al pulsar; arriba, abajo e izquierda se repiten
//     si se mantienen (más rápido pasado un rato).
//   - El centro sale al soltar (CENTRO) o al mantenerlo (CENTRO_LARGO, una vez).
enum Tecla : uint8_t {
  NINGUNA,
  ARRIBA,
  ABAJO,
  IZQUIERDA,
  DERECHA,
  CENTRO,
  CENTRO_LARGO
};

void joystickIniciar();
Tecla joystickLeer();  // una por llamada; NINGUNA si no hay

#endif
