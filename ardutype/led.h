#ifndef LED_H
#define LED_H

#include <Arduino.h>

// NeoPixel de la placa como indicador:
//   - destello azul con cada pulsación
//   - dos destellos rojos en un error, uno verde en un acierto
//   - ámbar latiendo durante la cuenta atrás y el tecleo
//   - arcoíris lento en reposo (con el OLED apagado)
// Los destellos se superponen al modo de fondo y luego vuelve a él.
void ledIniciar();
void ledTick();

void ledActivo(bool activo);    // ajuste: false lo apaga del todo
void ledBrillo(uint8_t brillo); // 0-255

void ledPulsacion();
void ledError();
void ledOk();
void ledTecleo(bool tecleando);
void ledReposo(bool reposo);

#endif
