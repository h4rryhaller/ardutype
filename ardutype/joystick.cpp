#include "config.h"
#include "joystick.h"

static const uint8_t PINES[] = {PIN_ARRIBA, PIN_ABAJO, PIN_IZQUIERDA, PIN_DERECHA, PIN_CENTRO};
static const Tecla TECLAS[] = {ARRIBA, ABAJO, IZQUIERDA, DERECHA, CENTRO};
static const int NUM = sizeof(PINES) / sizeof(PINES[0]);

static bool estable[NUM];
static bool lectura[NUM];
static uint32_t cambioMs[NUM];
static uint32_t pulsadoMs[NUM];
static uint32_t repetirMs[NUM];
static bool largoEmitido = true;  // un centro pulsado al arrancar no cuenta

static const uint8_t TAM_COLA = 8;
static Tecla cola[TAM_COLA];
static uint8_t inicioCola = 0, finCola = 0;

static void encolar(Tecla t) {
  uint8_t siguiente = (finCola + 1) % TAM_COLA;
  if (siguiente == inicioCola) return;  // llena: se pierde
  cola[finCola] = t;
  finCola = siguiente;
}

static bool seRepite(Tecla t) {
  return t == ARRIBA || t == ABAJO || t == IZQUIERDA;
}

static void sondear() {
  uint32_t ahora = millis();
  for (int i = 0; i < NUM; i++) {
    bool l = digitalRead(PINES[i]) == LOW;
    if (l != lectura[i]) {
      lectura[i] = l;
      cambioMs[i] = ahora;
    }
    if (l != estable[i] && ahora - cambioMs[i] >= ANTIRREBOTE_MS) {
      estable[i] = l;
      if (l) {
        pulsadoMs[i] = ahora;
        repetirMs[i] = ahora + REPETICION_INICIO_MS;
        if (TECLAS[i] == CENTRO) largoEmitido = false;
        else encolar(TECLAS[i]);
      } else if (TECLAS[i] == CENTRO && !largoEmitido) {
        encolar(CENTRO);
      }
    }
    if (!estable[i]) continue;

    if (TECLAS[i] == CENTRO) {
      if (!largoEmitido && ahora - pulsadoMs[i] >= PULSACION_LARGA_MS) {
        largoEmitido = true;
        encolar(CENTRO_LARGO);
      }
    } else if (seRepite(TECLAS[i]) && (int32_t)(ahora - repetirMs[i]) >= 0) {
      encolar(TECLAS[i]);
      bool rapido = ahora - pulsadoMs[i] >= REPETICION_RAPIDA_TRAS_MS;
      repetirMs[i] = ahora + (rapido ? REPETICION_RAPIDA_MS : REPETICION_MS);
    }
  }
}

void joystickIniciar() {
  for (int i = 0; i < NUM; i++) pinMode(PINES[i], INPUT_PULLUP);
  delay(5);
  for (int i = 0; i < NUM; i++) estable[i] = lectura[i] = digitalRead(PINES[i]) == LOW;
}

Tecla joystickLeer() {
  sondear();
  if (inicioCola == finCola) return NINGUNA;
  Tecla t = cola[inicioCola];
  inicioCola = (inicioCola + 1) % TAM_COLA;
  return t;
}
