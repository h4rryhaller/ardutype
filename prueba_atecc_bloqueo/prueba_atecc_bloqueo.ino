// Ardutype: prueba del ATECC608B provisionado y BLOQUEADO
//
// ¡DESTRUCTIVA! Bloquear el chip es irreversible y aquí la clave ADMIN es fija
// y conocida: el chip que pase por esta prueba queda solo para pruebas.
// Márcalo con rotulador y no lo uses en el Ardutype de verdad.
//
// Placa: "Adafruit KB2040" o una ESP8266 (banco de pruebas aparte; pines en
// config.h: D2 = SDA, D1 = SCL en NodeMCU y Wemos D1 mini). atecc.h/.cpp son
// copia de los del firmware, que inician el bus a 100 kHz. En la ESP8266 el
// ATECC va igual (4 GND, 5 SDA, 6 SCL, 8 a 3V3), con pull-ups de 4,7 kΩ de
// SDA y SCL a 3V3.
// Monitor serie a 115200, con fin de línea. Órdenes (una letra + Intro):
//   e  estado: contador, límite e intentos que quedan
//   B  provisionar y bloquear (pide escribir BLOQUEAR)
//   i  un intento: MAC con la ranura INTENTO (debe sumar 1 al contador y dar
//      siempre la misma respuesta)
//   g  gastar intentos hasta que el chip se niegue
//   s  subir el límite (que queden al menos 10) con la clave ADMIN buena
//   f  intentar subirlo con una clave ADMIN falsa (el chip debe negarse)
//   x  ataques: leer las claves, escribir en claro el límite o la clave INTENTO
//      (el chip debe negarse a todo)
//   a  RANDOM dos veces (con la configuración bloqueada ya es aleatorio)

#include <Wire.h>

#include "config.h"
#include "atecc.h"

// Fija y pública: solo para chips de prueba
static const uint8_t claveAdmin[TAM_ATECC] = {'a', 'r', 'd', 'u', 't', 'y', 'p', 'e', ' ', 'c', 'h',
                                              'i', 'p', ' ', 'd', 'e', ' ', 'P', 'R', 'U', 'E', 'B',
                                              'A', 'S', '.', '.', '.', '.', '.', '.', '.', '.'};
static const uint8_t RETO[TAM_ATECC] = {'r', 'e', 't', 'o', ' ', 'd', 'e', ' ', 'p', 'r', 'u',
                                        'e', 'b', 'a', 0,   0,   0,   0,   0,   0,   0,   0,
                                        0,   0,   0,   0,   0,   0,   0,   0,   0,   0};

static const char *nombreEstado(EstadoAtecc e) {
  switch (e) {
    case ATECC_AUSENTE: return "no aparece";
    case ATECC_VIRGEN: return "virgen (sin bloquear)";
    case ATECC_A_MEDIAS: return "a medias (configuración bloqueada, datos no)";
    case ATECC_LISTO: return "listo (provisionado y bloqueado)";
    case ATECC_AJENO: return "bloqueado con otra configuración";
  }
  return "?";
}

static void imprimirHex(const uint8_t *p, size_t n) {
  for (size_t i = 0; i < n; i++) Serial.printf("%02X ", p[i]);
  Serial.println();
}

static bool contador(uint32_t &c, uint32_t &l) {
  if (!ateccContador(c, l)) {
    Serial.printf("  no se puede leer el contador (estado 0x%02X)\n", ateccUltimoEstado());
    return false;
  }
  return true;
}

static void estado() {
  Serial.printf("Estado: %s\n", nombreEstado(ateccIniciar()));
  uint32_t c, l;
  if (ateccEstado() != ATECC_VIRGEN && ateccEstado() != ATECC_AUSENTE && contador(c, l))
    Serial.printf("  contador %lu, límite %lu -> quedan %ld intentos\n", (unsigned long)c, (unsigned long)l, (long)l - (long)c);
}

static String leerLinea() {
  String s;
  while (true) {
    if (Serial.available()) {
      char ch = Serial.read();
      if (ch == '\n' || ch == '\r') {
        if (s.length()) return s;
      } else {
        s += ch;
      }
    }
    delay(1);
  }
}

static void provisionar() {
  Serial.println("Esto BLOQUEA el chip para siempre. Escribe BLOQUEAR para seguir:");
  if (leerLinea() != "BLOQUEAR") {
    Serial.println("Cancelado.");
    return;
  }
  bool ok = ateccProvisionar(claveAdmin);
  Serial.printf("Provisionar: %s (estado 0x%02X)\n", ok ? "correcto" : "ERROR", ateccUltimoEstado());
  estado();
}

static void intento() {
  uint8_t r[TAM_ATECC];
  uint32_t c0, l0, c1, l1;
  if (!contador(c0, l0)) return;
  bool ok = ateccIntento(RETO, r);
  uint8_t e = ateccUltimoEstado();
  if (!contador(c1, l1)) return;
  if (ok) {
    Serial.print("Intento: respuesta ");
    imprimirHex(r, 8);
  } else {
    Serial.printf("Intento: el chip se niega (estado 0x%02X)\n", e);
  }
  Serial.printf("  contador %lu -> %lu (%s), límite %lu\n", (unsigned long)c0, (unsigned long)c1, c1 == c0 + 1 ? "suma 1, bien" : "¡no suma 1!", (unsigned long)l1);
}

static void gastar() {
  uint32_t c, l;
  if (!contador(c, l)) return;
  Serial.printf("Gastando desde contador %lu, límite %lu...\n", (unsigned long)c, (unsigned long)l);
  uint8_t r[TAM_ATECC];
  int hechos = 0;
  while (hechos < 100 && ateccIntento(RETO, r)) hechos++;
  uint8_t e = ateccUltimoEstado();
  if (!contador(c, l)) return;
  Serial.printf("  %d intentos aceptados; luego se niega con estado 0x%02X\n", hechos, e);
  Serial.printf("  contador %lu, límite %lu -> %s\n", (unsigned long)c, (unsigned long)l, c == l ? "se para justo en el límite, bien" : "¡no cuadra!");
}

// Pide un intento más de los que quedan: siempre obliga a escribir (límite + 32)
static void subir(bool buena) {
  uint8_t falsa[TAM_ATECC];
  memset(falsa, 0x5A, sizeof(falsa));
  uint32_t c0, l0, c, l;
  if (!contador(c0, l0)) return;
  bool ok = ateccSubirLimite(buena ? claveAdmin : falsa, l0 - c0 + 1);
  uint8_t e = ateccUltimoEstado();
  const char *paso = ateccPasoFallido();
  if (!contador(c, l)) return;
  Serial.printf("Subir límite con clave %s: %s", buena ? "buena" : "falsa", ok ? "aceptado\n" : "rechazado");
  if (!ok) Serial.printf(" en %s (estado 0x%02X)\n", paso, e);
  Serial.printf("  contador %lu, límite %lu -> %lu, quedan %ld\n", (unsigned long)c, (unsigned long)l0,
                (unsigned long)l, (long)l - (long)c);
  bool bien = buena ? ok && l == l0 + 32 : !ok && l == l0;
  Serial.println(bien ? "  bien" : "  ¡MAL!");
}

static void debeFallar(const char *que, bool ok) {
  if (ok) Serial.printf("  %s: ¡MAL! el chip lo ha permitido\n", que);
  else Serial.printf("  %s: bien, se niega (estado 0x%02X)\n", que, ateccUltimoEstado());
}

static void ataques() {
  uint8_t buf[TAM_ATECC];
  uint32_t c, l;
  if (!contador(c, l)) return;
  Serial.println("Ataques:");
  debeFallar("leer la ranura INTENTO (4)", ateccOrdenCruda(0x02, 0x82, 4 << 3, nullptr, 0, buf, 32));
  debeFallar("leer la ranura ADMIN (3)", ateccOrdenCruda(0x02, 0x82, 3 << 3, nullptr, 0, buf, 32));
  // Si lo permitiera, escribe el mismo límite que ya hay
  uint8_t bloque[32] = {0};
  memcpy(bloque, &l, 4);
  memcpy(bloque + 4, &l, 4);
  debeFallar("escribir el límite (6) en claro", ateccOrdenCruda(0x12, 0x82, 6 << 3, bloque, 32, nullptr, 0));
  memset(bloque, 0, sizeof(bloque));
  debeFallar("escribir la ranura INTENTO (4) en claro", ateccOrdenCruda(0x12, 0x82, 4 << 3, bloque, 32, nullptr, 0));
  // MAC con ADMIN sí se permite (sin NoMac, que impedía la escritura cifrada): da
  // un resumen de la clave, no la clave, y no sirve para falsificar la escritura
}

static void aleatorio() {
  for (int i = 0; i < 2; i++) {
    uint8_t r[TAM_ATECC];
    if (!ateccAleatorio(r)) {
      Serial.printf("RANDOM: error (estado 0x%02X)\n", ateccUltimoEstado());
      return;
    }
    Serial.printf("RANDOM %d: ", i + 1);
    imprimirHex(r, 32);
  }
}

void setup() {
  Serial.begin(115200);
  while (!Serial) delay(10);
  delay(200);
  Serial.println("\n==== ATECC608B: prueba con bloqueo (e B i g s f x a) ====");
  estado();
}

void loop() {
  if (!Serial.available()) return;
  char ch = Serial.read();
  if (ch == '\n' || ch == '\r' || ch == ' ') return;
  bool listo = ateccEstado() == ATECC_LISTO;
  switch (ch) {
    case 'e': estado(); break;
    case 'B': provisionar(); break;
    case 'a': aleatorio(); break;
    case 'i': case 'g': case 's': case 'f': case 'x':
      if (!listo) {
        Serial.println("Primero hay que provisionar (B).");
        break;
      }
      if (ch == 'i') intento();
      if (ch == 'g') gastar();
      if (ch == 's') subir(true);
      if (ch == 'f') subir(false);
      if (ch == 'x') ataques();
      break;
    default: Serial.println("Órdenes: e B i g s f x a");
  }
}
