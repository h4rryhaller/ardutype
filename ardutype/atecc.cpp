#include <Wire.h>

#include <bearssl/bearssl.h>

// El firmware va en la KB2040 (RP2040); el banco de pruebas destructivas puede
// ir en una ESP8266. Lo que cambia es el despertar y los aleatorios.
#if defined(ARDUINO_ARCH_RP2040)
#include <hardware/gpio.h>
#elif !defined(ARDUINO_ARCH_ESP8266)
#error "atecc.cpp: solo RP2040 o ESP8266"
#endif

#include "config.h"
#include "atecc.h"

static const uint8_t DIRECCION = 0x60;
enum : uint8_t { PALABRA_DORMIR = 0x01, PALABRA_ORDEN = 0x03 };
enum : uint8_t {
  OP_LEER = 0x02,
  OP_MAC = 0x08,
  OP_ESCRIBIR = 0x12,
  OP_GENDIG = 0x15,
  OP_NONCE = 0x16,
  OP_BLOQUEAR = 0x17,
  OP_ALEATORIO = 0x1B,
  OP_CONTADOR = 0x24,
};
// Zonas en p1 de leer/escribir; 0x80 = 32 bytes en vez de 4
enum : uint8_t { ZONA_CONFIG = 0x00, ZONA_DATOS = 0x02, ZONA_32 = 0x80 };

static const uint8_t RANURA_ADMIN = 3;
static const uint8_t RANURA_INTENTO = 4;
static const uint8_t RANURA_LIMITE = 6;
static const uint32_t LIMITE_INICIAL = 32;  // intentos hasta el primer desbloqueo bueno

static const uint32_t SONDEO_MAX_MS = 300;  // la orden más lenta tarda ~250 ms
static const uint32_t VIGILANTE_MS = 1000;  // el chip se duerme solo a los ~1,3 s de despertar

// Lo que cambiamos de la configuración de fábrica (byte, valor)
struct Cambio {
  uint8_t byte;
  uint8_t valor;
};
static const Cambio CONFIG[] = {
    {18, 0x61},                // CountMatch activo, límite en la ranura 6
    // SlotConfig 3 ADMIN: secreta, no se lee ni se escribe. Sin NoMac: con él, el
    // GenDig marca TempKey y el chip rechaza la escritura cifrada (sospecha tras el
    // primer chip de pruebas, 2026-11-12; se confirma con el segundo).
    {26, 0x8F}, {27, 0x80},
    {28, 0xAF}, {29, 0x80},    // SlotConfig 4 INTENTO: secreta, LimitedUse, no se lee ni se escribe
    {32, 0x00}, {33, 0x43},    // SlotConfig 6 LIMITE: en claro; se escribe cifrado con la clave 3
    {102, 0x1C}, {103, 0x00},  // KeyConfig 3, 4 y 6: clave SHA/HMAC sin más requisitos
    {104, 0x1C}, {105, 0x00},
    {108, 0x1C}, {109, 0x00},
};

static EstadoAtecc estado = ATECC_AUSENTE;
static uint8_t serie[9];
static uint8_t ultimoEstado = 0;
static const char *paso = "";  // el paso de ateccSubirLimite que ha fallado
static uint32_t despierto = 0;  // millis() del último despertar, 0 = dormido

// ---- Protocolo ----

// CRC-16 del ATECC: polinomio 0x8005, bits de cada byte del menos significativo al más
static uint16_t crc16(const uint8_t *datos, size_t n) {
  uint16_t crc = 0;
  for (size_t i = 0; i < n; i++) {
    for (uint8_t bit = 0x01; bit; bit <<= 1) {
      bool bitDato = datos[i] & bit;
      bool bitCrc = crc >> 15;
      crc <<= 1;
      if (bitDato != bitCrc) crc ^= 0x8005;
    }
  }
  return crc;
}

// SDA a nivel bajo más de 60 µs y 1,5 ms de arranque. Moviendo el pin a mano:
// a 400 kHz un byte a 0 no dura lo bastante.
static void despertar() {
#if defined(ARDUINO_ARCH_RP2040)
  gpio_set_function(PIN_SDA, GPIO_FUNC_SIO);
  gpio_put(PIN_SDA, 0);
  gpio_set_dir(PIN_SDA, GPIO_OUT);
  delayMicroseconds(100);
  gpio_set_dir(PIN_SDA, GPIO_IN);
  gpio_set_function(PIN_SDA, GPIO_FUNC_I2C);
#else
  pinMode(PIN_SDA, OUTPUT);
  digitalWrite(PIN_SDA, LOW);
  delayMicroseconds(100);
  Wire.begin(PIN_SDA, PIN_SCL);  // devuelve los pines al I2C
#endif
  delayMicroseconds(1500);
  despierto = millis() | 1;
}

// Dormir borra el estado interno (TempKey incluida)
static void dormir() {
  Wire.beginTransmission(DIRECCION);
  Wire.write(PALABRA_DORMIR);
  Wire.endTransmission();
  despierto = 0;
}

// nueva = empezar de cero, para secuencias que necesitan que TempKey sobreviva
static void sesion(bool nueva) {
  if (!nueva && despierto && millis() - despierto < VIGILANTE_MS) return;
  if (despierto) dormir();
  despertar();
}

// Mientras trabaja, el chip no contesta a su dirección: se reintenta cada ms
static bool leerRespuesta(uint8_t *resp, size_t n) {
  uint32_t inicio = millis();
  while (millis() - inicio < SONDEO_MAX_MS) {
    if ((size_t)Wire.requestFrom((int)DIRECCION, (int)n) == n) {
      for (size_t i = 0; i < n; i++) resp[i] = Wire.read();
      // Una respuesta de 4 bytes cuando se esperaban datos es un código de error
      size_t largo = resp[0] == 4 ? 4 : n;
      uint16_t crc = crc16(resp, largo - 2);
      if (resp[0] != largo || resp[largo - 2] != (crc & 0xFF) || resp[largo - 1] != (crc >> 8)) {
        ultimoEstado = 0xFF;
        return false;
      }
      if (largo == n) return true;
      ultimoEstado = resp[1];
      return false;
    }
    while (Wire.available()) Wire.read();
    delay(1);
  }
  ultimoEstado = 0xEE;
  return false;
}

static bool orden(uint8_t op, uint8_t p1, uint16_t p2, const uint8_t *datos, size_t largoDatos,
                  uint8_t *salida, size_t largoSalida) {
  uint8_t paquete[7 + 64];
  if (largoDatos > 64) return false;
  sesion(false);
  size_t n = 7 + largoDatos;
  paquete[0] = n;
  paquete[1] = op;
  paquete[2] = p1;
  paquete[3] = p2 & 0xFF;
  paquete[4] = p2 >> 8;
  if (largoDatos) memcpy(paquete + 5, datos, largoDatos);
  uint16_t crc = crc16(paquete, n - 2);
  paquete[n - 2] = crc & 0xFF;
  paquete[n - 1] = crc >> 8;

  Wire.beginTransmission(DIRECCION);
  Wire.write(PALABRA_ORDEN);
  Wire.write(paquete, n);
  bool ok = Wire.endTransmission() == 0;
  memset(paquete, 0, sizeof(paquete));
  if (!ok) {
    ultimoEstado = 0xEE;
    return false;
  }

  uint8_t resp[3 + 64];
  size_t largoResp = largoSalida ? largoSalida + 3 : 4;
  ok = leerRespuesta(resp, largoResp);
  if (ok && !largoSalida) {
    ultimoEstado = resp[1];
    ok = resp[1] == 0x00;
  } else if (ok) {
    ultimoEstado = 0x00;
    memcpy(salida, resp + 1, largoSalida);
  }
  memset(resp, 0, sizeof(resp));
  return ok;
}

static bool fallo() {
  dormir();
  return false;
}

static void sha256(uint8_t salida[32], const void *a, size_t na, const void *b, size_t nb,
                   const void *c = nullptr, size_t nc = 0, const void *d = nullptr, size_t nd = 0) {
  const void *partes[] = {a, b, c, d};
  const size_t largos[] = {na, nb, nc, nd};
  br_sha256_context ctx;
  br_sha256_init(&ctx);
  for (int i = 0; i < 4; i++)
    if (largos[i]) br_sha256_update(&ctx, partes[i], largos[i]);
  br_sha256_out(&ctx, salida);
}

static void aleatorioPlaca(uint8_t *p, size_t n) {
  while (n) {
#if defined(ARDUINO_ARCH_RP2040)
    uint32_t r = rp2040.hwrand32();
#else
    uint32_t r = ESP.random();
#endif
    size_t k = n < 4 ? n : 4;
    memcpy(p, &r, k);
    p += k;
    n -= k;
  }
}

static bool leerConfig(uint8_t cfg[128]) {
  for (uint8_t bloque = 0; bloque < 4; bloque++)
    if (!orden(OP_LEER, ZONA_CONFIG | ZONA_32, bloque << 3, nullptr, 0, cfg + 32 * bloque, 32)) return false;
  return true;
}

static bool configNuestra(const uint8_t cfg[128]) {
  for (const Cambio &c : CONFIG)
    if (cfg[c.byte] != c.valor) return false;
  return true;
}

static bool escribirRanura(uint8_t ranura, const uint8_t datos[32]) {
  return orden(OP_ESCRIBIR, ZONA_DATOS | ZONA_32, ranura << 3, datos, 32, nullptr, 0);
}

static void bloqueLimite(uint32_t limite, uint8_t bloque[32]) {
  memset(bloque, 0, 32);
  memcpy(bloque, &limite, 4);
  memcpy(bloque + 4, &limite, 4);
}

static uint32_t redondearLimite(uint32_t contador, uint32_t minimo) {
  return (contador + minimo + 31) & ~31u;
}

// ---- Interfaz ----

// El chip va solo en su bus: en el del OLED, el tráfico de la pantalla lo
// despertaba. Necesita pull-ups de 4,7 kΩ a 3V3 en SDA y SCL (las internas no
// bastan: el chip dejaba de contestar).
static void iniciarBus() {
  static bool iniciado = false;
  if (iniciado) return;
  iniciado = true;
#if defined(ARDUINO_ARCH_RP2040)
  Wire.setSDA(PIN_SDA);
  Wire.setSCL(PIN_SCL);
  Wire.begin();
#else
  Wire.begin(PIN_SDA, PIN_SCL);
#endif
  Wire.setClock(100000);
}

EstadoAtecc ateccIniciar() {
  uint8_t cfg[128];
  iniciarBus();
  sesion(true);
  if (!leerConfig(cfg)) {
    dormir();
    return estado = ATECC_AUSENTE;
  }
  dormir();
  memcpy(serie, cfg, 4);
  memcpy(serie + 4, cfg + 8, 5);
  bool configBloqueada = cfg[87] != 0x55;
  bool datosBloqueados = cfg[86] != 0x55;
  if (!configBloqueada) estado = ATECC_VIRGEN;
  else if (!configNuestra(cfg)) estado = ATECC_AJENO;
  else estado = datosBloqueados ? ATECC_LISTO : ATECC_A_MEDIAS;
  return estado;
}

EstadoAtecc ateccEstado() {
  return estado;
}

const uint8_t *ateccSerie() {
  return serie;
}

bool ateccProvisionar(const uint8_t claveAdmin[TAM_ATECC]) {
  if (estado != ATECC_VIRGEN && estado != ATECC_A_MEDIAS) return false;

  if (estado == ATECC_VIRGEN) {
    uint8_t cfg[128], leida[128];
    sesion(true);
    if (!leerConfig(cfg)) return fallo();
    for (const Cambio &c : CONFIG) cfg[c.byte] = c.valor;
    // Solo las palabras de 4 bytes que tocan los cambios
    for (uint8_t palabra = 4; palabra < 32; palabra++) {
      bool toca = false;
      for (const Cambio &c : CONFIG)
        if (c.byte / 4 == palabra) toca = true;
      if (toca && !orden(OP_ESCRIBIR, ZONA_CONFIG, palabra, cfg + 4 * palabra, 4, nullptr, 0)) return fallo();
    }
    // Se bloquea con el CRC de lo que queríamos: si el chip tiene otra cosa, se niega
    if (!leerConfig(leida) || memcmp(leida, cfg, sizeof(cfg))) return fallo();
    if (!orden(OP_BLOQUEAR, 0x00, crc16(cfg, sizeof(cfg)), nullptr, 0, nullptr, 0)) return fallo();
    estado = ATECC_A_MEDIAS;
  }

  // Datos, en claro mientras la zona no esté bloqueada
  uint8_t delChip[32], dePlaca[32], clave[32], bloque[32], contador[4];
  sesion(true);
  if (!ateccAleatorio(delChip)) return fallo();
  aleatorioPlaca(dePlaca, sizeof(dePlaca));
  sha256(clave, delChip, sizeof(delChip), dePlaca, sizeof(dePlaca));
  bool ok = escribirRanura(RANURA_INTENTO, clave) && escribirRanura(RANURA_ADMIN, claveAdmin) &&
            orden(OP_CONTADOR, 0x00, 0, nullptr, 0, contador, 4);
  memset(clave, 0, sizeof(clave));
  memset(dePlaca, 0, sizeof(dePlaca));
  if (!ok) return fallo();
  uint32_t c;
  memcpy(&c, contador, 4);
  bloqueLimite(redondearLimite(c, LIMITE_INICIAL), bloque);
  if (!escribirRanura(RANURA_LIMITE, bloque)) return fallo();
  // Zona de datos (y OTP), sin CRC
  if (!orden(OP_BLOQUEAR, 0x81, 0, nullptr, 0, nullptr, 0)) return fallo();
  dormir();
  estado = ATECC_LISTO;
  return true;
}

bool ateccIntento(const uint8_t reto[TAM_ATECC], uint8_t respuesta[TAM_ATECC]) {
  if (estado != ATECC_LISTO) return false;
  // MAC modo 0: SHA-256 de la clave, el reto y datos fijos del chip
  bool ok = orden(OP_MAC, 0x00, RANURA_INTENTO, reto, TAM_ATECC, respuesta, TAM_ATECC);
  dormir();
  return ok;
}

bool ateccContador(uint32_t &contador, uint32_t &limite) {
  if (estado != ATECC_LISTO && estado != ATECC_A_MEDIAS) return false;
  uint8_t c[4], l[4];
  bool ok = orden(OP_CONTADOR, 0x00, 0, nullptr, 0, c, 4) &&
            orden(OP_LEER, ZONA_DATOS, RANURA_LIMITE << 3, nullptr, 0, l, 4);
  dormir();
  if (!ok) return false;
  memcpy(&contador, c, 4);
  memcpy(&limite, l, 4);
  return true;
}

// Escritura cifrada en la ranura LIMITE: Nonce, GenDig con la clave ADMIN, y
// los datos XOR el resumen, más un MAC que prueba que conocemos la clave.
bool ateccSubirLimite(const uint8_t claveAdmin[TAM_ATECC], uint32_t minimo) {
  uint32_t contador, limite;
  if (estado != ATECC_LISTO || !ateccContador(contador, limite)) return false;
  uint32_t nuevo = redondearLimite(contador, minimo);
  if (nuevo <= limite) return true;

  static const uint8_t CEROS[25] = {0};
  uint8_t numIn[20], randOut[32], tempKey[32], resumen[32], claro[32], datos[64];
  aleatorioPlaca(numIn, sizeof(numIn));
  sesion(true);
  paso = "Nonce";
  if (!orden(OP_NONCE, 0x00, 0, numIn, sizeof(numIn), randOut, sizeof(randOut))) return fallo();
  static const uint8_t COLA_NONCE[3] = {OP_NONCE, 0x00, 0x00};
  sha256(tempKey, randOut, 32, numIn, 20, COLA_NONCE, 3);

  paso = "GenDig";
  if (!orden(OP_GENDIG, ZONA_DATOS, RANURA_ADMIN, nullptr, 0, nullptr, 0)) return fallo();
  const uint8_t datosGendig[7] = {OP_GENDIG, ZONA_DATOS, RANURA_ADMIN, 0x00, serie[8], serie[0], serie[1]};
  sha256(resumen, claveAdmin, TAM_ATECC, datosGendig, 7, CEROS, 25, tempKey, 32);

  uint16_t p2 = RANURA_LIMITE << 3;
  bloqueLimite(nuevo, claro);
  for (int i = 0; i < 32; i++) datos[i] = claro[i] ^ resumen[i];
  const uint8_t datosEscribir[7] = {OP_ESCRIBIR, ZONA_DATOS | ZONA_32, (uint8_t)(p2 & 0xFF), (uint8_t)(p2 >> 8),
                                    serie[8], serie[0], serie[1]};
  sha256(datos + 32, resumen, 32, datosEscribir, 7, CEROS, 25, claro, 32);
  paso = "Write";
  bool ok = orden(OP_ESCRIBIR, ZONA_DATOS | ZONA_32, p2, datos, sizeof(datos), nullptr, 0);
  if (ok) paso = "";
  memset(tempKey, 0, sizeof(tempKey));
  memset(resumen, 0, sizeof(resumen));
  dormir();
  return ok;
}

bool ateccAleatorio(uint8_t salida[TAM_ATECC]) {
  // Sin dormir: provisionar lo usa en mitad de una sesión
  return orden(OP_ALEATORIO, 0x00, 0, nullptr, 0, salida, TAM_ATECC);
}

bool ateccOrdenCruda(uint8_t op, uint8_t p1, uint16_t p2, const uint8_t *datos, size_t largoDatos,
                     uint8_t *salida, size_t largoSalida) {
  bool ok = orden(op, p1, p2, datos, largoDatos, salida, largoSalida);
  dormir();
  return ok;
}

const char *ateccPasoFallido() {
  return paso;
}

uint8_t ateccUltimoEstado() {
  return ultimoEstado;
}
