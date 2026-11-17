#include "azar.h"
#include "generador.h"

// La de siempre al dar de alta un servicio: todos los conjuntos y sin
// caracteres parecidos. Ahora con el azar del ATECC (azar.h).
void generarContrasena(char *dst, size_t largo) {
  azarSembrar();
  azarContrasena(dst, largo + 1, largo, AZAR_MINUSCULAS | AZAR_MAYUSCULAS | AZAR_CIFRAS | AZAR_SIMBOLOS,
                 true);
}
