#ifndef ZUMBADOR_H
#define ZUMBADOR_H

#include "config.h"

// Zumbador pasivo en PIN_ZUMBADOR. Los pitidos no bloquean el bucle.
void zumbadorIniciar();
void zumbadorPitido(uint16_t hz = PITIDO_HZ, uint16_t ms = PITIDO_MS);

#endif
