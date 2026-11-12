// Ardutype: prueba del teclado USB (fase 2)
//
// Placa: "Adafruit KB2040" (core arduino-pico). Librería: U8g2.
//
// Abre un editor de texto en el ordenador, pon el cursor en una línea vacía y
// pulsa el CENTRO del joystick: tras una cuenta atrás de 3 s teclea las líneas
// de prueba. Compáralas con las de referencia del monitor serie (115200).
// Cualquier dirección durante la cuenta atrás la cancela. Nunca teclea sola.
//
//   IZQUIERDA / DERECHA  distribución del ordenador (ES Linux, ES Windows, US)
//   ARRIBA               compensar Bloq Mayús (sí / no)
//   ABAJO                retardo por tecla (5, 10, 20, 40 ms)
//   CENTRO               teclear las líneas de prueba

#include <Wire.h>
#include <U8g2lib.h>
#include "teclado.h"

const uint8_t PIN_SDA = 0;  // TX
const uint8_t PIN_SCL = 1;  // RX

enum { ARRIBA, DERECHA, IZQUIERDA, ABAJO, CENTRO, NUM_DIRECCIONES };
const uint8_t PIN_DIRECCION[NUM_DIRECCIONES] = {2, 3, 4, 5, 6};

const uint32_t ANTIRREBOTE_MS = 30;
const uint32_t CUENTA_ATRAS_MS = 3000;

const uint16_t RETARDOS[] = {5, 10, 20, 40};
const int NUM_RETARDOS = sizeof(RETARDOS) / sizeof(RETARDOS[0]);

// Líneas de prueba. Las tres últimas solo tienen sentido en español.
const char *const LINEAS[] = {
  "abcdefghijklmnopqrstuvwxyz",
  "ABCDEFGHIJKLMNOPQRSTUVWXYZ",
  "0123456789",
  "!\"#$%&'()*+,-./:;<=>?@[\\]^_`{|}~",
  "ñÑçÇ€¿¡ºª·¬´¨",
  "áéíóú ÁÉÍÓÚ àèìòù ÀÈÌÒÙ",
  "âêîôû ÂÊÎÔÛ äëïöü ÄËÏÖÜ",
};
const int NUM_LINEAS = sizeof(LINEAS) / sizeof(LINEAS[0]);

U8G2_SSD1306_128X32_UNIVISION_F_HW_I2C oled(U8G2_R0, U8X8_PIN_NONE);

bool pulsado[NUM_DIRECCIONES];
bool lecturaAnterior[NUM_DIRECCIONES];
uint32_t cambioMs[NUM_DIRECCIONES];

int indiceRetardo = 1;
bool compensar = true;
bool cuentaAtras = false;
uint32_t cuentaInicioMs = 0;
char estado[22] = "Centro: teclear";
bool redibujar = true;

void dibujar() {
  char linea[22];
  oled.clearBuffer();
  oled.setFont(u8g2_font_6x10_tf);

  snprintf(linea, sizeof(linea), "%s %ums", tecladoNombre(tecladoDistribucionActual()),
           RETARDOS[indiceRetardo]);
  oled.drawStr(0, 9, linea);

  const char *mayus = !tecladoLedConocido() ? "?" : tecladoMayusculas() ? "SI" : "no";
  snprintf(linea, sizeof(linea), "BloqMay:%s comp:%s", mayus, compensar ? "si" : "no");
  oled.drawStr(0, 20, linea);

  oled.drawStr(0, 31, estado);
  oled.sendBuffer();
}

void ponerEstado(const char *texto) {
  strncpy(estado, texto, sizeof(estado) - 1);
  estado[sizeof(estado) - 1] = '\0';
  redibujar = true;
}

void imprimirAjustes() {
  Serial.printf("Distribución %s, retardo %u ms, compensar Bloq Mayús: %s\n",
                tecladoNombre(tecladoDistribucionActual()), RETARDOS[indiceRetardo],
                compensar ? "sí" : "no");
}

void teclearPrueba() {
  ponerEstado("Tecleando...");
  dibujar();
  Serial.println("Tecleando. Referencia:");
  size_t saltados = 0;
  for (int i = 0; i < NUM_LINEAS; i++) {
    Serial.printf("  %s\n", LINEAS[i]);
    size_t n = tecladoEscribir(LINEAS[i]);
    if (n) Serial.printf("    (%u sin soportar, el último U+%04lX)\n", (unsigned)n,
                         (unsigned long)tecladoUltimoNoSoportado());
    saltados += n;
    tecladoEscribir("\n");
  }
  char texto[22];
  if (saltados) snprintf(texto, sizeof(texto), "Hecho, %u saltados", (unsigned)saltados);
  else snprintf(texto, sizeof(texto), "Hecho");
  Serial.println(texto);
  ponerEstado(texto);
}

void alPulsar(int direccion) {
  if (cuentaAtras) {
    cuentaAtras = false;
    Serial.println("Cancelado");
    ponerEstado("Cancelado");
    return;
  }
  switch (direccion) {
    case IZQUIERDA:
    case DERECHA: {
      int d = tecladoDistribucionActual() + (direccion == DERECHA ? 1 : NUM_DISTRIBUCIONES - 1);
      tecladoDistribucion((Distribucion)(d % NUM_DISTRIBUCIONES));
      break;
    }
    case ARRIBA:
      compensar = !compensar;
      tecladoCompensarMayusculas(compensar);
      break;
    case ABAJO:
      indiceRetardo = (indiceRetardo + 1) % NUM_RETARDOS;
      tecladoRetardo(RETARDOS[indiceRetardo]);
      break;
    case CENTRO:
      cuentaAtras = true;
      cuentaInicioMs = millis();
      return;
  }
  imprimirAjustes();
  ponerEstado("Centro: teclear");
}

void leerJoystick() {
  uint32_t ahora = millis();
  for (int i = 0; i < NUM_DIRECCIONES; i++) {
    bool lectura = digitalRead(PIN_DIRECCION[i]) == LOW;
    if (lectura != lecturaAnterior[i]) {
      lecturaAnterior[i] = lectura;
      cambioMs[i] = ahora;
    }
    if (lectura != pulsado[i] && ahora - cambioMs[i] >= ANTIRREBOTE_MS) {
      pulsado[i] = lectura;
      if (lectura) alPulsar(i);
    }
  }
}

void setup() {
  Serial.begin(115200);
  for (int i = 0; i < NUM_DIRECCIONES; i++) pinMode(PIN_DIRECCION[i], INPUT_PULLUP);

  Wire.setSDA(PIN_SDA);
  Wire.setSCL(PIN_SCL);
  Wire.begin();
  oled.begin();
  oled.setBusClock(400000);

  tecladoIniciar();
  tecladoRetardo(RETARDOS[indiceRetardo]);
  tecladoCompensarMayusculas(compensar);

  uint32_t inicio = millis();
  while (!Serial && millis() - inicio < 3000) delay(10);
  Serial.println("\nArdutype: prueba del teclado");
  imprimirAjustes();
}

void loop() {
  leerJoystick();

  if (cuentaAtras) {
    uint32_t pasado = millis() - cuentaInicioMs;
    if (pasado >= CUENTA_ATRAS_MS) {
      cuentaAtras = false;
      teclearPrueba();
    } else {
      char texto[22];
      snprintf(texto, sizeof(texto), "Tecleo en %lu s...",
               (unsigned long)((CUENTA_ATRAS_MS - pasado + 999) / 1000));
      if (strcmp(texto, estado)) ponerEstado(texto);
    }
  }

  // El ordenador puede cambiar Bloq Mayús en cualquier momento
  static bool mayusPintado = false, ledPintado = false;
  if (tecladoMayusculas() != mayusPintado || tecladoLedConocido() != ledPintado) {
    mayusPintado = tecladoMayusculas();
    ledPintado = tecladoLedConocido();
    Serial.printf("Bloq Mayús del ordenador: %s\n", mayusPintado ? "encendido" : "apagado");
    redibujar = true;
  }

  if (redibujar) {
    redibujar = false;
    dibujar();
  }
  delay(2);
}
