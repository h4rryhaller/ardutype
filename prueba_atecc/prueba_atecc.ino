// Ardutype: prueba de concepto del ATECC608B (solo lectura, no escribe ni bloquea nada)
//
// Placa: "Adafruit KB2040" del core arduino-pico. Sin librerías externas: el
// protocolo del chip (despertar, paquetes con CRC-16, sondeo mientras trabaja)
// va aquí mismo, que es lo que luego pasará al firmware.
//
// Conexión: TX = SDA, RX = SCL, a 3V3 y GND, con pull-ups de 4,7 kΩ de TX y
// RX a 3V3 (el OLED va en otro bus, A0/A1). El chip suelto en SOIC-8:
// patilla 4 GND, 5 SDA, 6 SCL, 8 VCC (el resto sin conectar) y un
// condensador entre VCC y GND.
//
// Monitor serie a 115200. La prueba se lanza al abrir el monitor y otra vez
// con cada línea que se envíe. Con la línea «c» empieza el modo continuo:
// despierta el chip, le pide INFO y un SHA-256 y lo duerme, diez veces por
// segundo, y cada segundo dice cuántas han fallado. Sirve para mover los
// cables y ver en qué momento falla. Otra línea lo para. La prueba normal hace:
//   1. Despertar el chip y buscarlo en el bus (dormido no contesta)
//   2. INFO: revisión del silicio (608A = 0x6002, 608B = 0x6003)
//   3. Leer la zona de configuración: número de serie, dirección, bloqueos,
//      configuración de cada ranura
//   4. SHA-256 de "abc" hecho por el chip, comparado con el valor conocido
//   5. RANDOM dos veces (con la configuración sin bloquear da un patrón fijo
//      FF FF 00 00, no aleatorios)

#include <Wire.h>
#include <hardware/gpio.h>

const uint8_t PIN_SDA = 0;  // TX
const uint8_t PIN_SCL = 1;  // RX
const uint8_t DIRECCION_OLED = 0x3C;

// Direcciones de fábrica conocidas: 0x60 genérico (ATECC608B-SSHDA, MAHDA...),
// 0x35 Trust&GO (TNGTLS), 0x36 TrustFLEX (TFLXTLS)
const uint8_t DIRECCIONES_ATECC[] = {0x60, 0x35, 0x36, 0x58, 0x6A, 0x6C};

// Byte de dirección de palabra (lo primero que se escribe)
enum : uint8_t { PALABRA_RESET = 0x00, PALABRA_DORMIR = 0x01, PALABRA_REPOSO = 0x02, PALABRA_ORDEN = 0x03 };

// Códigos de operación
enum : uint8_t { OP_LEER = 0x02, OP_ALEATORIO = 0x1B, OP_INFO = 0x30, OP_SHA = 0x47 };

const uint32_t SONDEO_MAX_MS = 300;  // la orden más lenta del 608 tarda ~250 ms (SelfTest)

static uint8_t direccion = 0;
static bool callado = false;  // en el modo continuo, los fallos solo se cuentan
static bool continuo = false;

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

// SDA a nivel bajo más de 60 µs y esperar 1,5 ms a que arranque. Se hace
// moviendo el pin a mano: a 400 kHz un byte a 0 no dura lo bastante.
static void despertar() {
  gpio_set_function(PIN_SDA, GPIO_FUNC_SIO);
  gpio_put(PIN_SDA, 0);
  gpio_set_dir(PIN_SDA, GPIO_OUT);
  delayMicroseconds(100);
  gpio_set_dir(PIN_SDA, GPIO_IN);
  gpio_set_function(PIN_SDA, GPIO_FUNC_I2C);
  delayMicroseconds(1500);
}

static void enviarPalabra(uint8_t palabra) {
  Wire.beginTransmission(direccion);
  Wire.write(palabra);
  Wire.endTransmission();
}

// Lee una respuesta de n bytes (contador + datos + CRC). Mientras el chip
// trabaja no contesta a su dirección: se reintenta cada milisegundo.
static bool leerRespuesta(uint8_t *resp, size_t n, uint32_t esperaMs) {
  uint32_t inicio = millis();
  while (millis() - inicio < esperaMs) {
    if (Wire.requestFrom(direccion, n) == n) {
      for (size_t i = 0; i < n; i++) resp[i] = Wire.read();
      if (resp[0] != n) {
        if (!callado) Serial.printf("  respuesta de %u bytes, se esperaban %u (estado 0x%02X)\n", resp[0], n, resp[1]);
        return false;
      }
      uint16_t crc = crc16(resp, n - 2);
      if (resp[n - 2] != (crc & 0xFF) || resp[n - 1] != (crc >> 8)) {
        if (!callado) Serial.println("  CRC de la respuesta incorrecto");
        return false;
      }
      return true;
    }
    while (Wire.available()) Wire.read();
    delay(1);
  }
  if (!callado) Serial.println("  el chip no contesta");
  return false;
}

// Manda una orden y deja en salida los datos de la respuesta (largoSalida
// bytes). Una respuesta de 4 bytes es un código de estado: 0x00 es correcto.
static bool orden(uint8_t op, uint8_t p1, uint16_t p2, const uint8_t *datos, size_t largoDatos,
                  uint8_t *salida, size_t largoSalida) {
  uint8_t paquete[7 + 64];
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

  Wire.beginTransmission(direccion);
  Wire.write(PALABRA_ORDEN);
  Wire.write(paquete, n);
  if (Wire.endTransmission() != 0) {
    if (!callado) Serial.println("  no acepta la orden");
    return false;
  }

  uint8_t resp[3 + 64];
  size_t largoResp = largoSalida ? largoSalida + 3 : 4;
  if (!leerRespuesta(resp, largoResp, SONDEO_MAX_MS)) {
    // Puede que haya contestado con un estado de error en lugar de datos
    return false;
  }
  if (!largoSalida) {
    if (resp[1] != 0x00) Serial.printf("  estado 0x%02X\n", resp[1]);
    return resp[1] == 0x00;
  }
  memcpy(salida, resp + 1, largoSalida);
  return true;
}

// ---- Pruebas ----

static void imprimirHex(const uint8_t *p, size_t n) {
  for (size_t i = 0; i < n; i++) Serial.printf("%02X%s", p[i], (i % 16 == 15 || i == n - 1) ? "\n" : " ");
}

static bool buscarChip() {
  despertar();
  for (uint8_t d : DIRECCIONES_ATECC) {
    Wire.beginTransmission(d);
    if (Wire.endTransmission() == 0) {
      direccion = d;
      return true;
    }
  }
  // No está en ninguna dirección conocida: enseñar qué hay en el bus
  Serial.print("  dispositivos en el bus:");
  for (uint8_t d = 0x08; d < 0x78; d++) {
    Wire.beginTransmission(d);
    if (Wire.endTransmission() == 0) Serial.printf(" 0x%02X%s", d, d == DIRECCION_OLED ? " (OLED)" : "");
  }
  Serial.println();
  return false;
}

static void comprobarDespertar() {
  // Recién despierto, el chip tiene preparado 04 11 33 43 (0x11 = "acabo de despertar")
  enviarPalabra(PALABRA_DORMIR);
  despertar();
  uint8_t resp[4];
  if (leerRespuesta(resp, 4, 10))
    Serial.printf("Despertar: %02X %02X %02X %02X %s\n", resp[0], resp[1], resp[2], resp[3],
                  resp[1] == 0x11 ? "(correcto, CRC bien)" : "(estado inesperado)");
}

static void probarInfo() {
  uint8_t rev[4];
  if (!orden(OP_INFO, 0x00, 0, nullptr, 0, rev, 4)) return;
  const char *modelo = rev[2] == 0x60 && rev[3] == 0x03 ? "ATECC608B"
                       : rev[2] == 0x60 && rev[3] == 0x02 ? "ATECC608A"
                       : rev[2] == 0x50                   ? "ATECC508A"
                                                          : "desconocido";
  Serial.printf("INFO: revisión %02X %02X %02X %02X -> %s\n", rev[0], rev[1], rev[2], rev[3], modelo);
}

static void probarConfiguracion() {
  uint8_t cfg[128];
  for (uint8_t bloque = 0; bloque < 4; bloque++) {
    // Zona 0 (configuración), bit 7 = leer 32 bytes; dirección = bloque << 3
    if (!orden(OP_LEER, 0x80, bloque << 3, nullptr, 0, cfg + 32 * bloque, 32)) {
      Serial.printf("Error leyendo el bloque %u de la configuración\n", bloque);
      return;
    }
  }
  Serial.println("Zona de configuración (128 bytes):");
  imprimirHex(cfg, sizeof(cfg));

  Serial.print("  Número de serie: ");
  imprimirHex(cfg, 4);
  Serial.print("                   ");
  imprimirHex(cfg + 8, 5);
  Serial.printf("  Dirección I2C: 0x%02X (7 bits: 0x%02X)\n", cfg[16], cfg[16] >> 1);
  Serial.printf("  ChipMode: 0x%02X  AES: 0x%02X  CountMatch: 0x%02X\n", cfg[19], cfg[13], cfg[18]);
  Serial.printf("  Configuración: %s\n", cfg[87] == 0x55 ? "SIN BLOQUEAR" : "bloqueada");
  Serial.printf("  Datos y OTP:   %s\n", cfg[86] == 0x55 ? "SIN BLOQUEAR" : "bloqueados");
  Serial.printf("  SlotLocked: %02X %02X  ChipOptions: %02X %02X\n", cfg[88], cfg[89], cfg[90], cfg[91]);
  Serial.println("  Ranura  SlotConfig  KeyConfig");
  for (uint8_t i = 0; i < 16; i++) {
    uint16_t slot = cfg[20 + 2 * i] | cfg[21 + 2 * i] << 8;
    uint16_t key = cfg[96 + 2 * i] | cfg[97 + 2 * i] << 8;
    Serial.printf("    %2u      0x%04X      0x%04X\n", i, slot, key);
  }
}

static void probarSha() {
  static const uint8_t ESPERADO[32] = {0xBA, 0x78, 0x16, 0xBF, 0x8F, 0x01, 0xCF, 0xEA, 0x41, 0x41, 0x40,
                                       0xDE, 0x5D, 0xAE, 0x22, 0x23, 0xB0, 0x03, 0x61, 0xA3, 0x96, 0x17,
                                       0x7A, 0x9C, 0xB4, 0x10, 0xFF, 0x61, 0xF2, 0x00, 0x15, 0xAD};
  uint8_t hash[32];
  // SHA en dos pasos: modo 0x00 inicia, modo 0x02 termina con el resto del mensaje (p2 = largo)
  if (!orden(OP_SHA, 0x00, 0, nullptr, 0, nullptr, 0)) return;
  if (!orden(OP_SHA, 0x02, 3, (const uint8_t *)"abc", 3, hash, 32)) return;
  Serial.printf("SHA-256(\"abc\"): %s\n", memcmp(hash, ESPERADO, 32) ? "DISTINTO del esperado" : "correcto");
}

static void probarAleatorio() {
  for (int i = 0; i < 2; i++) {
    uint8_t r[32];
    // p1 = 0x00: actualiza la semilla interna
    if (!orden(OP_ALEATORIO, 0x00, 0, nullptr, 0, r, 32)) return;
    Serial.printf("RANDOM %d: ", i + 1);
    imprimirHex(r, 32);
  }
}

static void prueba() {
  Serial.println("\n==== Prueba del ATECC608 ====");
  if (!buscarChip()) {
    Serial.println("No encuentro el ATECC. Revisa VCC/GND/SDA/SCL y los pull-ups.");
    return;
  }
  Serial.printf("ATECC en 0x%02X\n", direccion);
  comprobarDespertar();
  probarInfo();
  probarConfiguracion();
  probarSha();
  probarAleatorio();
  enviarPalabra(PALABRA_DORMIR);
  Serial.println("Hecho. Envía cualquier línea para repetir.");
}

// Una ronda del modo continuo: despertar, INFO, SHA-256 de "abc" y dormir
static bool ronda() {
  static const uint8_t ESPERADO_INICIO[4] = {0xBA, 0x78, 0x16, 0xBF};
  uint8_t rev[4], hash[32];
  despertar();
  bool ok = orden(OP_INFO, 0x00, 0, nullptr, 0, rev, 4) && orden(OP_SHA, 0x00, 0, nullptr, 0, nullptr, 0) &&
            orden(OP_SHA, 0x02, 3, (const uint8_t *)"abc", 3, hash, 32) && !memcmp(hash, ESPERADO_INICIO, 4);
  enviarPalabra(PALABRA_DORMIR);
  return ok;
}

static void rondasDeUnSegundo() {
  int fallos = 0;
  for (int i = 0; i < 10; i++) {
    if (!ronda()) fallos++;
    delay(100);
  }
  if (fallos) Serial.printf("FALLAN %d de 10   <-----\n", fallos);
  else Serial.println("bien 10 de 10");
}

void setup() {
  Serial.begin(115200);
  Wire.setSDA(PIN_SDA);
  Wire.setSCL(PIN_SCL);
  Wire.begin();
  Wire.setClock(100000);  // como en el firmware
  while (!Serial) delay(10);
  delay(200);
  prueba();
}

void loop() {
  if (Serial.available()) {
    char c = Serial.read();
    delay(10);
    while (Serial.available()) Serial.read();
    if (continuo) {
      continuo = callado = false;
      Serial.println("Modo continuo parado.");
    } else if (c == 'c') {
      if (!buscarChip()) Serial.println("Aviso: el chip no contesta en ninguna dirección conocida; se usa 0x60.");
      if (!direccion) direccion = 0x60;
      continuo = callado = true;
      Serial.println("Modo continuo: mueve los cables y mira cuándo falla. Otra línea para.");
    } else {
      prueba();
    }
  }
  if (continuo) rondasDeUnSegundo();
}
