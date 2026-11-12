#include <Wire.h>

#include "config.h"
#include "utf8.h"
#include "pantalla.h"

U8G2_SSD1306_128X32_UNIVISION_F_HW_I2C oled(U8G2_R0, U8X8_PIN_NONE);

// Fuentes _tf: llevan Latin-1 (ñ, á, ¿...); el € no está y sale en blanco
const uint8_t *const FUENTE_PEQUENA = u8g2_font_5x7_tf;
const uint8_t *const FUENTE_NORMAL = u8g2_font_6x10_tf;
const uint8_t *const FUENTE_GRANDE = u8g2_font_helvB12_tf;

static bool encendida = true;

void pantallaIniciar() {
  Wire.setSDA(PIN_SDA);
  Wire.setSCL(PIN_SCL);
  Wire.begin();
  oled.begin();
  oled.setBusClock(400000);
  oled.enableUTF8Print();
}

void pantallaEncender(bool si) {
  if (si == encendida) return;
  encendida = si;
  oled.setPowerSave(si ? 0 : 1);
}

bool pantallaEncendida() {
  return encendida;
}

void pantallaContraste(uint8_t contraste) {
  oled.setContrast(contraste);
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
