#include <bearssl/bearssl.h>
#include "cripto.h"

void criptoAleatorio(void *dst, size_t n) {
  uint8_t *p = (uint8_t *)dst;
  while (n) {
    uint32_t r = rp2040.hwrand32();
    size_t k = n < 4 ? n : 4;
    memcpy(p, &r, k);
    p += k;
    n -= k;
  }
}

void criptoBorrar(void *p, size_t n) {
  volatile uint8_t *v = (volatile uint8_t *)p;
  while (n--) *v++ = 0;
}

bool criptoIguales(const void *a, const void *b, size_t n) {
  const uint8_t *x = (const uint8_t *)a;
  const uint8_t *y = (const uint8_t *)b;
  uint8_t diferencia = 0;
  for (size_t i = 0; i < n; i++) diferencia |= x[i] ^ y[i];
  return diferencia == 0;
}

void criptoPbkdf2(const void *clave, size_t largoClave, const uint8_t *sal, size_t largoSal,
                  uint32_t iteraciones, uint8_t salida[TAM_CLAVE]) {
  br_hmac_key_context kc;
  br_hmac_context hc;
  uint8_t u[TAM_CLAVE];
  static const uint8_t BLOQUE_1[4] = {0, 0, 0, 1};

  br_hmac_key_init(&kc, &br_sha256_vtable, clave, largoClave);
  br_hmac_init(&hc, &kc, 0);
  br_hmac_update(&hc, sal, largoSal);
  br_hmac_update(&hc, BLOQUE_1, sizeof(BLOQUE_1));
  br_hmac_out(&hc, u);
  memcpy(salida, u, TAM_CLAVE);

  for (uint32_t i = 1; i < iteraciones; i++) {
    br_hmac_init(&hc, &kc, 0);
    br_hmac_update(&hc, u, TAM_CLAVE);
    br_hmac_out(&hc, u);
    for (size_t j = 0; j < TAM_CLAVE; j++) salida[j] ^= u[j];
  }

  criptoBorrar(u, sizeof(u));
  criptoBorrar(&kc, sizeof(kc));
  criptoBorrar(&hc, sizeof(hc));
}

uint32_t criptoCalibrarPbkdf2(uint32_t objetivoMs) {
  const uint32_t MUESTRA = 2000;
  const uint32_t MINIMO = 10000;
  uint8_t sal[16] = {0};
  uint8_t salida[TAM_CLAVE];
  uint32_t inicio = micros();
  criptoPbkdf2("calibrar", 8, sal, sizeof(sal), MUESTRA, salida);
  uint32_t us = micros() - inicio;
  uint64_t iteraciones = (uint64_t)MUESTRA * objetivoMs * 1000 / (us ? us : 1);
  return iteraciones < MINIMO ? MINIMO : (uint32_t)iteraciones;
}

void criptoHkdf(const void *clave, size_t largoClave, const uint8_t *sal, size_t largoSal,
                const char *info, uint8_t salida[TAM_CLAVE]) {
  br_hkdf_context hc;
  br_hkdf_init(&hc, &br_sha256_vtable, sal, largoSal);
  br_hkdf_inject(&hc, clave, largoClave);
  br_hkdf_flip(&hc);
  br_hkdf_produce(&hc, info, strlen(info), salida, TAM_CLAVE);
  criptoBorrar(&hc, sizeof(hc));
}

void criptoCifrar(const uint8_t clave[TAM_CLAVE], const uint8_t nonce[TAM_NONCE], void *datos, size_t n,
                  const void *aad, size_t largoAad, uint8_t tag[TAM_TAG]) {
  br_poly1305_ctmul_run(clave, nonce, datos, n, aad, largoAad, tag, br_chacha20_ct_run, 1);
}

bool criptoDescifrar(const uint8_t clave[TAM_CLAVE], const uint8_t nonce[TAM_NONCE], void *datos, size_t n,
                     const void *aad, size_t largoAad, const uint8_t tag[TAM_TAG]) {
  uint8_t calculado[TAM_TAG];
  br_poly1305_ctmul_run(clave, nonce, datos, n, aad, largoAad, calculado, br_chacha20_ct_run, 0);
  bool valido = criptoIguales(calculado, tag, TAM_TAG);
  if (!valido) criptoBorrar(datos, n);
  return valido;
}
