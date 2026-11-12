#ifndef CRIPTO_H
#define CRIPTO_H

#include <Arduino.h>

// Primitivas sobre BearSSL (incluida en el core arduino-pico, de tiempo constante)

const size_t TAM_CLAVE = 32;
const size_t TAM_NONCE = 12;
const size_t TAM_TAG = 16;

// Bytes del generador aleatorio de hardware del RP2040
void criptoAleatorio(void *dst, size_t n);

// Borra memoria sin que el compilador lo quite por optimizar
void criptoBorrar(void *p, size_t n);

// Comparación en tiempo constante
bool criptoIguales(const void *a, const void *b, size_t n);

// PBKDF2-HMAC-SHA256 con 32 bytes de salida
void criptoPbkdf2(const void *clave, size_t largoClave, const uint8_t *sal, size_t largoSal,
                  uint32_t iteraciones, uint8_t salida[TAM_CLAVE]);

// Iteraciones de PBKDF2 que tardan unos objetivoMs en esta placa
uint32_t criptoCalibrarPbkdf2(uint32_t objetivoMs);

// HKDF-SHA256 (para claves que ya son aleatorias, como la de recuperación)
void criptoHkdf(const void *clave, size_t largoClave, const uint8_t *sal, size_t largoSal,
                const char *info, uint8_t salida[TAM_CLAVE]);

// ChaCha20-Poly1305 en el sitio. Descifrar comprueba la etiqueta; si no
// coincide, borra los datos y devuelve false.
void criptoCifrar(const uint8_t clave[TAM_CLAVE], const uint8_t nonce[TAM_NONCE], void *datos, size_t n,
                  const void *aad, size_t largoAad, uint8_t tag[TAM_TAG]);
bool criptoDescifrar(const uint8_t clave[TAM_CLAVE], const uint8_t nonce[TAM_NONCE], void *datos, size_t n,
                     const void *aad, size_t largoAad, const uint8_t tag[TAM_TAG]);

#endif
