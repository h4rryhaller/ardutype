#include "generador.h"

// Sin l, I, O, 0 ni 1: se confunden al leerlas en el OLED
static const char MINUSCULAS[] = "abcdefghijkmnopqrstuvwxyz";
static const char MAYUSCULAS[] = "ABCDEFGHJKLMNPQRSTUVWXYZ";
static const char CIFRAS[] = "23456789";
// Todos los signos ASCII imprimibles (sin el espacio)
static const char SIMBOLOS[] = "!#$%&()*+,-./:;<=>?@[\\]^_{|}";

// Número uniforme en [0, n) sin el sesgo del módulo
static uint32_t aleatorio(uint32_t n) {
  uint32_t limite = UINT32_MAX - UINT32_MAX % n;
  uint32_t r;
  do {
    r = rp2040.hwrand32();
  } while (r >= limite);
  return r % n;
}

void generarContrasena(char *dst, size_t largo) {
  static char alfabeto[sizeof(MINUSCULAS) + sizeof(MAYUSCULAS) + sizeof(CIFRAS) + sizeof(SIMBOLOS)];
  if (!alfabeto[0]) {
    strcpy(alfabeto, MINUSCULAS);
    strcat(alfabeto, MAYUSCULAS);
    strcat(alfabeto, CIFRAS);
    strcat(alfabeto, SIMBOLOS);
  }
  size_t n = strlen(alfabeto);

  bool completa;
  do {
    for (size_t i = 0; i < largo; i++) dst[i] = alfabeto[aleatorio(n)];
    dst[largo] = '\0';
    completa = strpbrk(dst, MINUSCULAS) && strpbrk(dst, MAYUSCULAS) &&
               strpbrk(dst, CIFRAS) && strpbrk(dst, SIMBOLOS);
  } while (!completa);
}
