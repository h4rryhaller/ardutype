#ifndef BOVEDA_H
#define BOVEDA_H

#include <Arduino.h>

// Bóveda: el almacén cifrado en la flash (LittleFS, fichero /boveda.bin).
//
// Una clave maestra aleatoria de 32 bytes cifra el almacén con
// ChaCha20-Poly1305. Esa clave se guarda dos veces, envuelta:
//   - con el PIN (PBKDF2-HMAC-SHA256, iteraciones calibradas a ~1 s). Con el
//     ATECC608 (atecc.h), el resultado del PBKDF2 pasa además por el chip, que
//     lo mezcla con una clave que nunca sale de él y cuenta los intentos: sin
//     el chip no se puede probar ningún PIN, y con él solo los que queden.
//   - con la clave de recuperación: 8 palabras al azar de dice_lista.h (~84
//     bits) con el mismo PBKDF2 que el PIN. No pasa por el chip, así que
//     también sirve si el chip se agota o se pierde. Las bóvedas anteriores
//     tienen una clave base32 de 128 bits (HKDF-SHA256), que se sigue aceptando.
// Cambiar el PIN solo vuelve a envolver la clave maestra.
//
// El chip se provisiona solo (IRREVERSIBLE) la primera vez que la bóveda se
// abre o se crea con él puesto. Su clave ADMIN, la que repone los intentos tras
// un desbloqueo bueno, va en /chip.bin cifrada con la clave maestra.
//
// Cada guardado escribe /boveda.tmp y lo renombra encima del anterior: un
// corte de corriente deja el fichero viejo o el nuevo, nunca uno a medias.
//
// Sin el chip (no está puesto) el PIN queda solo en software: con la flash
// legible, quien copie la flash lo rompe enseguida.

// Ajustes: van en la cabecera en claro (se necesitan antes de desbloquear),
// autenticados junto con el almacén.
struct __attribute__((packed)) Ajustes {
  uint8_t distribucion;
  uint8_t retardo;   // índices de las tablas de ui.cpp
  uint8_t bloqueo;
  uint8_t brillo;
  uint8_t led;       // 1 = encendido
  uint8_t reservado[3];
};

enum EstadoBoveda : uint8_t {
  BOVEDA_SIN_FS,    // sin sistema de archivos: falta elegir Flash Size con FS
  BOVEDA_NUEVA,     // todavía no hay almacén: hay que crear el PIN
  BOVEDA_CERRADA,
  BOVEDA_ABIERTA,
};

enum ResultadoApertura : uint8_t {
  APERTURA_OK,
  APERTURA_CLAVE_MAL,
  APERTURA_DANADA,        // la clave valía pero el almacén no se puede leer
  APERTURA_SIN_CHIP,      // el PIN va ligado al chip y no contesta, o es otro chip
  APERTURA_SIN_INTENTOS,  // el chip ya no admite más intentos: entrar con la clave de recuperación
};

// Palabras separadas por un espacio (las más largas tienen 10 letras)
const int PALABRAS_RECUPERACION = 8;
const size_t TAM_TEXTO_RECUPERACION = PALABRAS_RECUPERACION * 11;

EstadoBoveda bovedaIniciar();  // monta la flash y lee los ajustes
EstadoBoveda bovedaEstado();
Ajustes &bovedaAjustes();

// Crea un almacén vacío y lo deja abierto. Devuelve la clave de recuperación,
// que no se guarda en ningún sitio: hay que enseñarla una vez.
bool bovedaCrear(const char *pin, char recuperacion[TAM_TEXTO_RECUPERACION]);

ResultadoApertura bovedaAbrirConPin(const char *pin);
// Palabras (en minúsculas, separadas por espacios) o, en bóvedas anteriores, base32
ResultadoApertura bovedaAbrirConRecuperacion(const char *clave);
bool bovedaRecuperacionConPalabras();  // si la clave de esta bóveda son palabras
bool bovedaComprobarPin(const char *pin);  // con la bóveda abierta (con chip, gasta un intento)
bool bovedaCambiarPin(const char *pin);    // con la bóveda abierta

// Intentos de PIN que le quedan al chip; false si el PIN no va ligado al chip
bool bovedaIntentosChip(uint32_t &quedan);

bool bovedaGuardar();     // tras cualquier cambio del almacén o de los ajustes
void bovedaCerrar();      // borra la clave y el almacén de la RAM
void bovedaBorrarTodo();  // borra el fichero: vuelve a BOVEDA_NUEVA

#endif
