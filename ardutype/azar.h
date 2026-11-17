#ifndef AZAR_H
#define AZAR_H

#include <Arduino.h>

// Azar para el menú «Azar» y las contraseñas generadas.
//
// Fuente: 32 bytes del generador del ATECC608B (si está provisionado) y 32 del
// generador de hardware del RP2040, mezclados con SHA-256. El resultado es la
// clave de un ChaCha20 que da todos los bytes que haga falta. azarSembrar()
// toma semilla nueva: se llama al empezar cada generación.
//
// Sin sesgo de módulo: para elegir entre N valores se descarta y se repite.

void azarSembrar();
bool azarConChip();                       // la última semilla llevaba el ATECC
void azarBytes(void *dst, size_t n);
uint32_t azarUniforme(uint32_t n);        // uniforme en [0, n), n > 0

// ---- Generadores ----
// Escriben un texto terminado en 0 en dst (tam = tamaño de dst) y devuelven
// la entropía en bits (0 si no aplica: monedas, dados, números).

// Conjuntos de caracteres de las contraseñas (se suman con |)
enum {
  AZAR_MINUSCULAS = 1,
  AZAR_MAYUSCULAS = 2,
  AZAR_CIFRAS = 4,
  AZAR_SIMBOLOS = 8,
};
// Con al menos un carácter de cada conjunto pedido. largo >= conjuntos pedidos.
float azarContrasena(char *dst, size_t tam, int largo, uint8_t conjuntos, bool sinParecidos);
float azarDiceware(char *dst, size_t tam, int palabras, char separador);  // separador 0: juntas
float azarAlias(char *dst, size_t tam);           // palabra-palabra-NN
float azarPin(char *dst, size_t tam, int cifras);
float azarMac(char *dst, size_t tam);             // administrada localmente, unicast
float azarFormato(char *dst, size_t tam, const char *alfabeto, int largo);
float azarUuid(char *dst, size_t tam);            // versión 4
uint32_t azarColor();                             // 0xRRGGBB

// Monedas: «C X X C» y en resumen «2C 2X»
void azarMonedas(char *dst, size_t tam, char *resumen, size_t tamResumen, int monedas);
// Dados: «5 4» y en resumen «suma 9»
void azarDados(char *dst, size_t tam, char *resumen, size_t tamResumen, int dados, int caras);
// Números entre desde y hasta (incluidos). Sin repetidos, false si no caben.
bool azarNumeros(char *dst, size_t tam, int32_t desde, int32_t hasta, int cuantos, bool repetidos);

// Alfabetos de los formatos fijos
extern const char *const AZAR_ALFANUMERICO;   // a-z A-Z 0-9
extern const char *const AZAR_TOKEN;          // a-z 0-9
extern const char *const AZAR_HEX;            // 0-9 a-f

#endif
