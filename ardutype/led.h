#ifndef LED_H
#define LED_H

#include "config.h"

// NeoPixel de la placa como indicador:
//   - destello azul con cada pulsación
//   - dos destellos rojos en un error, uno verde en un acierto
//   - ámbar latiendo durante la cuenta atrás y el tecleo
//   - violeta parpadeando con una notificación del PC
//   - color del pomodoro (fijo, o parpadeando en pausa o al terminar)
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

// Pomodoro: por encima del reposo (y del tecleo no, que avisa de algo en curso)
enum ModoLedPomodoro : uint8_t { LED_POMO_NADA, LED_POMO_FIJO, LED_POMO_LENTO, LED_POMO_RAPIDO };
void ledPomodoro(uint32_t rgb, ModoLedPomodoro modo);

// Notificación del PC: violeta parpadeando (lento un aviso, rápido una
// pregunta), por encima del pomodoro y del reposo
enum ModoLedNoti : uint8_t { LED_NOTI_NADA, LED_NOTI_LENTO, LED_NOTI_RAPIDO };
void ledNotificacion(ModoLedNoti modo);

#endif
