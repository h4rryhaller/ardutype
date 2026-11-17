#include <bearssl/bearssl.h>
#include <math.h>
#include "atecc.h"
#include "cripto.h"
#include "dice_lista.h"
#include "azar.h"

// ---- Fuente ----

static uint8_t clave[32];
static uint32_t bloque = 0;        // contador de ChaCha20
static uint8_t reserva[64];        // bytes del último bloque aún sin usar
static size_t usados = sizeof(reserva);
static bool conChip = false;
static uint32_t semillas = 0;

void azarSembrar() {
  uint8_t delChip[TAM_ATECC] = {0};
  conChip = ateccEstado() == ATECC_LISTO && ateccAleatorio(delChip);
  uint8_t dePlaca[32];
  criptoAleatorio(dePlaca, sizeof(dePlaca));

  br_sha256_context ctx;
  br_sha256_init(&ctx);
  br_sha256_update(&ctx, clave, sizeof(clave));  // lo anterior no resta
  br_sha256_update(&ctx, delChip, sizeof(delChip));
  br_sha256_update(&ctx, dePlaca, sizeof(dePlaca));
  semillas++;
  br_sha256_update(&ctx, &semillas, sizeof(semillas));
  br_sha256_out(&ctx, clave);

  criptoBorrar(delChip, sizeof(delChip));
  criptoBorrar(dePlaca, sizeof(dePlaca));
  criptoBorrar(reserva, sizeof(reserva));
  bloque = 0;
  usados = sizeof(reserva);
}

bool azarConChip() {
  return conChip;
}

void azarBytes(void *dst, size_t n) {
  static const uint8_t iv[12] = {0};
  if (!semillas) azarSembrar();
  uint8_t *p = (uint8_t *)dst;
  while (n) {
    if (usados == sizeof(reserva)) {
      memset(reserva, 0, sizeof(reserva));
      bloque = br_chacha20_ct_run(clave, iv, bloque, reserva, sizeof(reserva));
      usados = 0;
    }
    size_t k = min(n, sizeof(reserva) - usados);
    memcpy(p, reserva + usados, k);
    criptoBorrar(reserva + usados, k);  // cada byte sale una sola vez
    usados += k;
    p += k;
    n -= k;
  }
}

uint32_t azarUniforme(uint32_t n) {
  if (n <= 1) return 0;
  uint32_t limite = UINT32_MAX - UINT32_MAX % n;  // múltiplo de n: sin sesgo
  uint32_t r;
  do {
    azarBytes(&r, sizeof(r));
  } while (r >= limite);
  return r % n;
}

// ---- Contraseñas ----

// Sin parecidos: fuera l, I, O, 0 y 1, que se confunden (sobre todo en el OLED)
static const char MINUSCULAS[] = "abcdefghijklmnopqrstuvwxyz";
static const char MINUSCULAS_CLARAS[] = "abcdefghijkmnopqrstuvwxyz";
static const char MAYUSCULAS[] = "ABCDEFGHIJKLMNOPQRSTUVWXYZ";
static const char MAYUSCULAS_CLARAS[] = "ABCDEFGHJKLMNPQRSTUVWXYZ";
static const char CIFRAS[] = "0123456789";
static const char CIFRAS_CLARAS[] = "23456789";
// Signos ASCII sin comillas, ` ni ~ (teclas muertas en algunas distribuciones)
static const char SIMBOLOS[] = "!#$%&()*+,-./:;<=>?@[\\]^_{|}";

const char *const AZAR_ALFANUMERICO = "abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789";
const char *const AZAR_TOKEN = "abcdefghijklmnopqrstuvwxyz0123456789";
const char *const AZAR_HEX = "0123456789abcdef";

float azarContrasena(char *dst, size_t tam, int largo, uint8_t conjuntos, bool sinParecidos) {
  const char *partes[4] = {
    sinParecidos ? MINUSCULAS_CLARAS : MINUSCULAS,
    sinParecidos ? MAYUSCULAS_CLARAS : MAYUSCULAS,
    sinParecidos ? CIFRAS_CLARAS : CIFRAS,
    SIMBOLOS,
  };
  char alfabeto[sizeof(MINUSCULAS) + sizeof(MAYUSCULAS) + sizeof(CIFRAS) + sizeof(SIMBOLOS)] = "";
  int pedidos = 0;
  for (int i = 0; i < 4; i++) {
    if (conjuntos & (1 << i)) {
      strcat(alfabeto, partes[i]);
      pedidos++;
    }
  }
  if (!pedidos || largo < pedidos || (size_t)largo >= tam) {
    dst[0] = '\0';
    return 0;
  }
  size_t n = strlen(alfabeto);
  bool completa;
  do {
    for (int i = 0; i < largo; i++) dst[i] = alfabeto[azarUniforme(n)];
    dst[largo] = '\0';
    completa = true;
    for (int i = 0; i < 4; i++)
      if ((conjuntos & (1 << i)) && !strpbrk(dst, partes[i])) completa = false;
  } while (!completa);
  return largo * log2f(n);
}

float azarFormato(char *dst, size_t tam, const char *alfabeto, int largo) {
  size_t n = strlen(alfabeto);
  if ((size_t)largo >= tam) largo = tam - 1;
  for (int i = 0; i < largo; i++) dst[i] = alfabeto[azarUniforme(n)];
  dst[largo] = '\0';
  return largo * log2f(n);
}

// ---- Palabras ----

static const uint32_t NUM_PALABRAS = sizeof(DICE_LISTA) / sizeof(DICE_LISTA[0]);

float azarDiceware(char *dst, size_t tam, int palabras, char separador) {
  size_t n = 0;
  dst[0] = '\0';
  for (int i = 0; i < palabras; i++) {
    const char *p = DICE_LISTA[azarUniforme(NUM_PALABRAS)];
    size_t largo = strlen(p);
    if (n + largo + 2 > tam) break;
    if (i && separador) dst[n++] = separador;
    memcpy(dst + n, p, largo + 1);
    n += largo;
  }
  return palabras * log2f(NUM_PALABRAS);
}

float azarAlias(char *dst, size_t tam) {
  const char *a = DICE_LISTA[azarUniforme(NUM_PALABRAS)];
  const char *b = DICE_LISTA[azarUniforme(NUM_PALABRAS)];
  snprintf(dst, tam, "%s-%s-%02lu", a, b, (unsigned long)azarUniforme(100));
  return 2 * log2f(NUM_PALABRAS) + log2f(100);
}

// ---- Formatos fijos ----

float azarPin(char *dst, size_t tam, int cifras) {
  return azarFormato(dst, tam, CIFRAS, cifras);
}

float azarMac(char *dst, size_t tam) {
  uint8_t b[6];
  azarBytes(b, sizeof(b));
  b[0] = (b[0] & 0xFC) | 0x02;  // bit 1: administrada localmente; bit 0 a 0: unicast
  snprintf(dst, tam, "%02x:%02x:%02x:%02x:%02x:%02x", b[0], b[1], b[2], b[3], b[4], b[5]);
  return 46;
}

float azarUuid(char *dst, size_t tam) {
  uint8_t b[16];
  azarBytes(b, sizeof(b));
  b[6] = (b[6] & 0x0F) | 0x40;  // versión 4
  b[8] = (b[8] & 0x3F) | 0x80;  // variante RFC 4122
  snprintf(dst, tam, "%02x%02x%02x%02x-%02x%02x-%02x%02x-%02x%02x-%02x%02x%02x%02x%02x%02x", b[0], b[1],
           b[2], b[3], b[4], b[5], b[6], b[7], b[8], b[9], b[10], b[11], b[12], b[13], b[14], b[15]);
  return 122;
}

uint32_t azarColor() {
  uint8_t b[3];
  azarBytes(b, sizeof(b));
  return (uint32_t)b[0] << 16 | (uint32_t)b[1] << 8 | b[2];
}

// ---- Monedas, dados y números ----

void azarMonedas(char *dst, size_t tam, char *resumen, size_t tamResumen, int monedas) {
  int caras = 0;
  size_t n = 0;
  for (int i = 0; i < monedas && n + 3 <= tam; i++) {
    bool cara = azarUniforme(2);
    caras += cara;
    if (i) dst[n++] = ' ';
    dst[n++] = cara ? 'C' : 'X';
  }
  dst[n] = '\0';
  snprintf(resumen, tamResumen, "%dC %dX", caras, monedas - caras);
}

void azarDados(char *dst, size_t tam, char *resumen, size_t tamResumen, int dados, int caras) {
  long suma = 0;
  size_t n = 0;
  dst[0] = '\0';
  for (int i = 0; i < dados; i++) {
    long v = azarUniforme(caras) + 1;
    suma += v;
    int k = snprintf(dst + n, tam - n, i ? " %ld" : "%ld", v);
    if (k < 0 || n + k >= tam) break;
    n += k;
  }
  if (dados > 1) snprintf(resumen, tamResumen, "suma %ld", suma);
  else snprintf(resumen, tamResumen, "d%d", caras);
}

bool azarNumeros(char *dst, size_t tam, int32_t desde, int32_t hasta, int cuantos, bool repetidos) {
  if (desde > hasta) {
    int32_t t = desde;
    desde = hasta;
    hasta = t;
  }
  uint32_t rango = (uint32_t)(hasta - desde) + 1;
  if (!repetidos && (uint32_t)cuantos > rango) return false;
  static const int MAX = 50;
  int32_t salidos[MAX];
  if (cuantos > MAX) cuantos = MAX;
  size_t n = 0;
  dst[0] = '\0';
  for (int i = 0; i < cuantos; i++) {
    int32_t v;
    bool repetido;
    do {  // pocos números: descartar los repetidos basta
      v = desde + (int32_t)azarUniforme(rango);
      repetido = false;
      for (int j = 0; j < i && !repetidos; j++) repetido |= salidos[j] == v;
    } while (repetido);
    salidos[i] = v;
    int k = snprintf(dst + n, tam - n, i ? " %ld" : "%ld", (long)v);
    if (k < 0 || n + k >= tam) break;
    n += k;
  }
  return true;
}
