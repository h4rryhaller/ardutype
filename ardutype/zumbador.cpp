#include "zumbador.h"

void zumbadorIniciar() {
  pinMode(PIN_ZUMBADOR, OUTPUT);
  digitalWrite(PIN_ZUMBADOR, LOW);
}

void zumbadorPitido(uint16_t hz, uint16_t ms) {
  tone(PIN_ZUMBADOR, hz, ms);
}
