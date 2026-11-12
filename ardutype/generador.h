#ifndef GENERADOR_H
#define GENERADOR_H

#include <Arduino.h>

// Contraseña aleatoria con el generador de hardware del RP2040: letras sin
// eñe ni acentos, cifras (sin l, I, O, 0 ni 1, que se confunden en el OLED)
// y los signos ASCII !"#$%&'()*+,-./:;<=>?@[\]^_`{|}~.
// Lleva al menos una minúscula, una mayúscula, una cifra y un signo.
void generarContrasena(char *dst, size_t largo);  // dst con sitio para largo + 1

#endif
