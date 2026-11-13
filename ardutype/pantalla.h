#ifndef PANTALLA_H
#define PANTALLA_H

#include <U8g2lib.h>

// OLED SSD1306 de 128 x 32. Distribución de las pantallas de menú:
//   y 0-6    cabecera (fuente de 5 x 7) y una raya en y = 7
//   y 9-19   fila 0 (fuente de 6 x 10, 20 caracteres)
//   y 21-31  fila 1
// U8g2 con un envío por I2C propio que apunta los errores del bus
class Oled : public U8G2 {
 public:
  Oled();
};
extern Oled oled;

extern const uint8_t *const FUENTE_PEQUENA;
extern const uint8_t *const FUENTE_NORMAL;
extern const uint8_t *const FUENTE_GRANDE;

const int CARACTERES_FILA = 20;

void pantallaIniciar();
void pantallaEncender(bool encendida);
bool pantallaEncendida();
void pantallaContraste(uint8_t contraste);

void pantallaCabecera(const char *izquierda, const char *derecha = nullptr);
void pantallaFila(int fila, const char *texto, bool seleccionada);
void pantallaCentrado(int base, const char *texto);  // con la fuente que esté puesta

// Tras cada refresco: si el bus I2C ha fallado, lo cuenta por el serie, lo
// libera y reinicia el OLED. Devuelve true si ha hecho falta (hay que repintar).
bool pantallaRevisarBus();

#endif
