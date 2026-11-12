#include "utf8.h"

uint32_t utf8Siguiente(const char *&p) {
  uint8_t b = *p++;
  if (b < 0x80) return b;
  int resto;
  uint32_t cp;
  if ((b & 0xE0) == 0xC0) { cp = b & 0x1F; resto = 1; }
  else if ((b & 0xF0) == 0xE0) { cp = b & 0x0F; resto = 2; }
  else if ((b & 0xF8) == 0xF0) { cp = b & 0x07; resto = 3; }
  else return 0xFFFD;
  while (resto--) {
    if ((*p & 0xC0) != 0x80) return 0xFFFD;
    cp = (cp << 6) | (*p++ & 0x3F);
  }
  return cp;
}

size_t utf8Bytes(uint32_t cp) {
  return cp < 0x80 ? 1 : cp < 0x800 ? 2 : 3;
}

size_t utf8Codificar(uint32_t cp, char *dst) {
  if (cp < 0x80) {
    dst[0] = cp;
    return 1;
  }
  if (cp < 0x800) {
    dst[0] = 0xC0 | (cp >> 6);
    dst[1] = 0x80 | (cp & 0x3F);
    return 2;
  }
  dst[0] = 0xE0 | (cp >> 12);
  dst[1] = 0x80 | ((cp >> 6) & 0x3F);
  dst[2] = 0x80 | (cp & 0x3F);
  return 3;
}

int utf8Contar(const char *s) {
  int n = 0;
  while (*s) {
    utf8Siguiente(s);
    n++;
  }
  return n;
}

void utf8Tramo(const char *src, int desde, int cuantos, char *dst, size_t tamDst) {
  size_t escrito = 0;
  int i = 0;
  while (*src && i < desde + cuantos) {
    const char *antes = src;
    utf8Siguiente(src);
    size_t largo = src - antes;
    if (i >= desde) {
      if (escrito + largo >= tamDst) break;
      memcpy(dst + escrito, antes, largo);
      escrito += largo;
    }
    i++;
  }
  dst[escrito] = '\0';
}
