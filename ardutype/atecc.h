#ifndef ATECC_H   
#define ATECC_H

#include <Arduino.h>

// ATECC608B solo en su bus I2C (Wire en PIN_SDA/PIN_SCL): el propio chip limita los intentos del PIN,
// así que reflashear la placa o copiar la flash no da intentos ilimitados.
//
// Ranuras que usamos (las demás se quedan como vienen de fábrica, sin uso):
//   4 INTENTO: clave aleatoria que nunca sale del chip. Cada MAC con ella suma
//     uno al contador 0, y el chip se niega a usarla cuando el contador llega
//     al límite (CountMatch).
//   3 ADMIN: clave que el firmware solo conoce con la bóveda abierta (va
//     guardada dentro, cifrada). Autoriza subir el límite.
//   6 LIMITE: el límite del contador 0. Se lee en claro; solo se escribe
//     cifrado y autorizado con ADMIN.
// El límite va de 32 en 32: el chip no compara los 5 bits bajos.
//
// Provisionar bloquea la configuración y los datos del chip: es IRREVERSIBLE.

enum EstadoAtecc : uint8_t {
  ATECC_AUSENTE,
  ATECC_VIRGEN,    // configuración sin bloquear: se puede provisionar
  ATECC_A_MEDIAS,  // configuración nuestra ya bloqueada, datos no (provisión cortada)
  ATECC_LISTO,
  ATECC_AJENO,     // bloqueado con otra configuración: no sirve
};

const size_t TAM_ATECC = 32;

EstadoAtecc ateccIniciar();  // inicia el bus la primera vez
EstadoAtecc ateccEstado();
const uint8_t *ateccSerie();  // 9 bytes, tras ateccIniciar

// Deja claveAdmin en la ranura ADMIN, una clave aleatoria en INTENTO y el
// límite inicial, y lo bloquea todo. IRREVERSIBLE.
bool ateccProvisionar(const uint8_t claveAdmin[TAM_ATECC]);

// Gasta un intento: respuesta = MAC de la clave INTENTO sobre el reto (siempre
// la misma para el mismo reto). false si ya no quedan.
bool ateccIntento(const uint8_t reto[TAM_ATECC], uint8_t respuesta[TAM_ATECC]);

bool ateccContador(uint32_t &contador, uint32_t &limite);

// Sube el límite para que queden al menos `minimo` intentos (redondeado a 32)
bool ateccSubirLimite(const uint8_t claveAdmin[TAM_ATECC], uint32_t minimo);

// 32 bytes del generador del chip (con la configuración sin bloquear da un patrón fijo)
bool ateccAleatorio(uint8_t salida[TAM_ATECC]);

// Claves P-256 en las ranuras 0-2 (configuración de fábrica: firma de mensajes
// externos y GenKey permitidos). La privada nunca sale del chip. OJO: la
// configuración ya bloqueada no exige autorización para usarlas: el PIN lo pide
// solo el firmware (ver «Agente SSH» en Ardutype_diseno.md).
const uint8_t RANURA_SSH = 0;
// La pública de la clave que haya; false si la ranura no tiene ninguna
bool ateccClavePublica(uint8_t ranura, uint8_t pub[64]);
// Crea una privada nueva (sustituye a la anterior, que se pierde) y da su pública
bool ateccGenerarClave(uint8_t ranura, uint8_t pub[64]);
// ECDSA P-256 sobre un resumen SHA-256 ya calculado: firma = r || s (big-endian)
bool ateccFirmar(uint8_t ranura, const uint8_t resumen[32], uint8_t firma[64]);

// Para las pruebas: una orden cualquiera y el último código de estado del chip
bool ateccOrdenCruda(uint8_t op, uint8_t p1, uint16_t p2, const uint8_t *datos, size_t largoDatos,
                     uint8_t *salida, size_t largoSalida);
uint8_t ateccUltimoEstado();
const char *ateccPasoFallido();  // de ateccSubirLimite: "Nonce", "GenDig", "Write" o ""

#endif
