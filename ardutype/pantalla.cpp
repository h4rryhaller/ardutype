#include <Wire.h>

#include "config.h"
#include "utf8.h"
#include "pantalla.h"

static const uint32_t RELOJ_I2C = 400000;

// Errores del bus desde la última revisión (código de endTransmission)
static uint16_t erroresBus = 0;
static uint8_t ultimoErrorBus = 0;

// Como u8x8_byte_arduino_hw_i2c, pero por BUS_OLED, apunta los errores y no
// cambia el reloj en cada envío (el bus ya está iniciado en pantallaIniciar)
static uint8_t enviarI2c(u8x8_t *u8x8, uint8_t msg, uint8_t argInt, void *argPtr) {
  switch (msg) {
    case U8X8_MSG_BYTE_SEND:
      BUS_OLED.write((uint8_t *)argPtr, (int)argInt);
      break;
    case U8X8_MSG_BYTE_INIT:
    case U8X8_MSG_BYTE_SET_DC:
      break;
    case U8X8_MSG_BYTE_START_TRANSFER:
      BUS_OLED.beginTransmission(u8x8_GetI2CAddress(u8x8) >> 1);
      break;
    case U8X8_MSG_BYTE_END_TRANSFER: {
      uint8_t e = BUS_OLED.endTransmission();
      if (e) {
        erroresBus++;
        ultimoErrorBus = e;
      }
      break;
    }
    default:
      return 0;
  }
  return 1;
}

Oled::Oled() : U8G2() {
  u8g2_Setup_ssd1306_i2c_128x32_univision_f(&u8g2, U8G2_R0, enviarI2c, u8x8_gpio_and_delay_arduino);
  u8x8_SetPin_HW_I2C(getU8x8(), U8X8_PIN_NONE);
}

Oled oled;

// Fuentes _tf: llevan Latin-1 (ñ, á, ¿...); el € no está y sale en blanco
const uint8_t *const FUENTE_PEQUENA = u8g2_font_5x7_tf;
const uint8_t *const FUENTE_NORMAL = u8g2_font_6x10_tf;
const uint8_t *const FUENTE_GRANDE = u8g2_font_helvB12_tf;

static bool encendida = true;
static uint8_t contraste = 255;

void pantallaIniciar() {
  BUS_OLED.setSDA(PIN_SDA_OLED);
  BUS_OLED.setSCL(PIN_SCL_OLED);
  BUS_OLED.begin();
  BUS_OLED.setClock(RELOJ_I2C);
  // Si el bus se atasca, Wire lo libera y lo reinicia en vez de quedarse esperando
  BUS_OLED.setTimeout(25, true);
  oled.begin();
  oled.enableUTF8Print();
}

bool pantallaRevisarBus() {
  static uint32_t ultimoArregloMs = 0;
  if (!erroresBus) return false;
  uint32_t ahora = millis();
  Serial.printf("I2C: %u errores enviando al OLED (último %u)\n", erroresBus, ultimoErrorBus);
  erroresBus = 0;
  if (ahora - ultimoArregloMs < 1000) return false;  // si no se arregla, no machacar el bus
  ultimoArregloMs = ahora;

  // Liberar el bus: hasta 9 pulsos de reloj para que quien retenga SDA lo suelte, y STOP
  BUS_OLED.end();
  pinMode(PIN_SDA_OLED, INPUT_PULLUP);
  pinMode(PIN_SCL_OLED, OUTPUT);
  for (int i = 0; i < 9 && digitalRead(PIN_SDA_OLED) == LOW; i++) {
    digitalWrite(PIN_SCL_OLED, LOW);
    delayMicroseconds(5);
    digitalWrite(PIN_SCL_OLED, HIGH);
    delayMicroseconds(5);
  }
  pinMode(PIN_SDA_OLED, OUTPUT);
  digitalWrite(PIN_SDA_OLED, LOW);
  delayMicroseconds(5);
  digitalWrite(PIN_SDA_OLED, HIGH);
  BUS_OLED.begin();
  BUS_OLED.setClock(RELOJ_I2C);

  // El OLED puede haber tomado datos por órdenes: se reinicia del todo
  oled.begin();
  oled.setContrast(contraste);
  oled.setPowerSave(encendida ? 0 : 1);
  erroresBus = 0;
  Serial.println("I2C: bus liberado y OLED reiniciado");
  return true;
}

void pantallaEncender(bool si) {
  if (si == encendida) return;
  encendida = si;
  oled.setPowerSave(si ? 0 : 1);
}

bool pantallaEncendida() {
  return encendida;
}

void pantallaContraste(uint8_t c) {
  contraste = c;
  oled.setContrast(c);
}

void pantallaCabecera(const char *izquierda, const char *derecha) {
  oled.setFont(FUENTE_PEQUENA);
  int anchoDerecha = 0;
  if (derecha && *derecha) {
    anchoDerecha = oled.getUTF8Width(derecha);
    oled.drawUTF8(128 - anchoDerecha, 6, derecha);
  }
  char texto[64];
  int caben = (128 - anchoDerecha - (anchoDerecha ? 5 : 0)) / 5;
  utf8Tramo(izquierda, 0, caben, texto, sizeof(texto));
  oled.drawUTF8(0, 6, texto);
  oled.drawHLine(0, 7, 128);
}

void pantallaFila(int fila, const char *texto, bool seleccionada) {
  int y0 = fila == 0 ? 9 : 21;
  char recortado[CARACTERES_FILA * 3 + 1];
  utf8Tramo(texto, 0, CARACTERES_FILA, recortado, sizeof(recortado));
  oled.setFont(FUENTE_NORMAL);
  if (seleccionada) {
    oled.drawBox(0, y0, 128, 11);
    oled.setDrawColor(0);
  }
  oled.drawUTF8(2, y0 + 9, recortado);
  oled.setDrawColor(1);
}

void pantallaCentrado(int base, const char *texto) {
  int ancho = oled.getUTF8Width(texto);
  oled.drawUTF8((128 - ancho) / 2, base, texto);
}
