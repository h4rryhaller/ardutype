#ifndef UTF8_H
#define UTF8_H

#include <Arduino.h>

// Utilidades mínimas de UTF-8 (hasta 3 bytes: todo lo que usa Ardutype)

// Siguiente código Unicode; avanza p. 0xFFFD si la secuencia es inválida.
uint32_t utf8Siguiente(const char *&p);

// Escribe cp en dst (hasta 3 bytes, sin terminar) y devuelve cuántos ocupa.
size_t utf8Codificar(uint32_t cp, char *dst);

// Bytes que ocupa cp.
size_t utf8Bytes(uint32_t cp);

// Número de caracteres de s.
int utf8Contar(const char *s);

// Copia en dst los caracteres [desde, desde + cuantos) de src, terminado en 0.
void utf8Tramo(const char *src, int desde, int cuantos, char *dst, size_t tamDst);

#endif
