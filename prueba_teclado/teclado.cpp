#include <Keyboard.h>
#include "teclado.h"

// Codificación de las tablas ASCII del core (KeyboardLayout.h, no es API
// pública): bit 7 = Shift, bits 7 y 6 = AltGr; 0x32 sustituye a la tecla ISO
// (la de < > junto al Shift izquierdo).
static const uint8_t MAPA_SHIFT = 0x80;
static const uint8_t MAPA_ALTGR = 0xC0;
static const uint8_t TECLA_ISO = 0x64;
static const uint8_t REEMPLAZO_ISO = 0x32;

// Keyboard.press(): por encima de 136 se manda el código de uso HID tal cual
static const uint8_t TECLA_CRUDA = 136;

// Códigos de uso HID (posición física en un teclado ISO)
static const uint8_t U_E = 0x08;
static const uint8_t U_3 = 0x20;
static const uint8_t U_4 = 0x21;
static const uint8_t U_6 = 0x23;
static const uint8_t U_ESPACIO = 0x2C;
static const uint8_t U_APERTURA = 0x2E;  // ¡ ¿ (a la derecha del ')
static const uint8_t U_GRAVE = 0x2F;     // ` ^ (a la derecha de la P)
static const uint8_t U_CEDILLA = 0x31;   // ç Ç
static const uint8_t U_ENYE = 0x33;      // ñ Ñ
static const uint8_t U_AGUDO = 0x34;     // ´ ¨ (a la derecha de la Ñ)
static const uint8_t U_ORDINAL = 0x35;   // º ª (a la izquierda del 1)

enum : uint8_t { MOD_NADA = 0, MOD_SHIFT = 1, MOD_ALTGR = 2 };

struct Golpe {
  uint8_t mod;
  uint8_t tecla;  // código de uso HID
};

static Distribucion distribucion = DIST_ES_LINUX;
static uint16_t retardoMs = 10;
static bool compensar = true;
static volatile bool ledMayus = false;
static volatile bool ledConocido = false;
static uint32_t ultimoNoSoportado = 0;

// ---- Tablas del español ----

// Caracteres de una sola pulsación que no están en la tabla ASCII
struct Directo {
  uint16_t cp;
  Golpe golpe;
};
static const Directo DIRECTOS_ES[] = {
  {0x00F1, {MOD_NADA, U_ENYE}},      // ñ
  {0x00D1, {MOD_SHIFT, U_ENYE}},     // Ñ
  {0x00E7, {MOD_NADA, U_CEDILLA}},   // ç
  {0x00C7, {MOD_SHIFT, U_CEDILLA}},  // Ç
  {0x20AC, {MOD_ALTGR, U_E}},        // €
  {0x00A1, {MOD_NADA, U_APERTURA}},  // ¡
  {0x00BF, {MOD_SHIFT, U_APERTURA}}, // ¿
  {0x00BA, {MOD_NADA, U_ORDINAL}},   // º
  {0x00AA, {MOD_SHIFT, U_ORDINAL}},  // ª
  {0x00B7, {MOD_SHIFT, U_3}},        // ·
  {0x00AC, {MOD_ALTGR, U_6}},        // ¬
};

// Teclas muertas: solas + espacio dan el signo; seguidas de vocal, la acentúan
enum { AGUDO, GRAVE, CIRCUNFLEJO, DIERESIS };
static const Golpe MUERTAS[] = {
  {MOD_NADA, U_AGUDO},    // ´
  {MOD_NADA, U_GRAVE},    // `
  {MOD_SHIFT, U_GRAVE},   // ^
  {MOD_SHIFT, U_AGUDO},   // ¨
};

struct Acentuada {
  uint16_t cp;
  uint8_t muerta;
  char base;
};
static const Acentuada ACENTUADAS_ES[] = {
  {0xE1, AGUDO, 'a'}, {0xE9, AGUDO, 'e'}, {0xED, AGUDO, 'i'}, {0xF3, AGUDO, 'o'}, {0xFA, AGUDO, 'u'},
  {0xC1, AGUDO, 'A'}, {0xC9, AGUDO, 'E'}, {0xCD, AGUDO, 'I'}, {0xD3, AGUDO, 'O'}, {0xDA, AGUDO, 'U'},
  {0xE0, GRAVE, 'a'}, {0xE8, GRAVE, 'e'}, {0xEC, GRAVE, 'i'}, {0xF2, GRAVE, 'o'}, {0xF9, GRAVE, 'u'},
  {0xC0, GRAVE, 'A'}, {0xC8, GRAVE, 'E'}, {0xCC, GRAVE, 'I'}, {0xD2, GRAVE, 'O'}, {0xD9, GRAVE, 'U'},
  {0xE2, CIRCUNFLEJO, 'a'}, {0xEA, CIRCUNFLEJO, 'e'}, {0xEE, CIRCUNFLEJO, 'i'}, {0xF4, CIRCUNFLEJO, 'o'}, {0xFB, CIRCUNFLEJO, 'u'},
  {0xC2, CIRCUNFLEJO, 'A'}, {0xCA, CIRCUNFLEJO, 'E'}, {0xCE, CIRCUNFLEJO, 'I'}, {0xD4, CIRCUNFLEJO, 'O'}, {0xDB, CIRCUNFLEJO, 'U'},
  {0xE4, DIERESIS, 'a'}, {0xEB, DIERESIS, 'e'}, {0xEF, DIERESIS, 'i'}, {0xF6, DIERESIS, 'o'}, {0xFC, DIERESIS, 'u'},
  {0xC4, DIERESIS, 'A'}, {0xCB, DIERESIS, 'E'}, {0xCF, DIERESIS, 'I'}, {0xD6, DIERESIS, 'O'}, {0xDC, DIERESIS, 'U'},
};

// Signos que en español son teclas muertas: muerta + espacio
struct Espaciado {
  uint16_t cp;
  uint8_t muerta;
};
static const Espaciado ESPACIADOS_ES[] = {
  {'^', CIRCUNFLEJO},
  {'`', GRAVE},
  {0xB4, AGUDO},     // ´
  {0xA8, DIERESIS},  // ¨
};

// ---- Teclear ----

static void alCambiarLed(bool, bool mayus, bool, bool, bool, void *) {
  ledMayus = mayus;
  ledConocido = true;
}

static void golpear(Golpe g) {
  if (g.mod == MOD_SHIFT) Keyboard.press(KEY_LEFT_SHIFT);
  if (g.mod == MOD_ALTGR) Keyboard.press(KEY_RIGHT_ALT);
  Keyboard.press(TECLA_CRUDA + g.tecla);
  delay(retardoMs);
  Keyboard.releaseAll();
  delay(retardoMs);
}

// Carácter ASCII según la tabla del core para la distribución actual
static bool golpeAscii(uint32_t c, Golpe &g) {
  if (c >= 128) return false;
  const uint8_t *mapa = distribucion == DIST_US ? KeyboardLayout_en_US : KeyboardLayout_es_ES;
  uint8_t k = pgm_read_byte(mapa + c);
  if (!k) return false;
  g.mod = MOD_NADA;
  if ((k & MAPA_ALTGR) == MAPA_ALTGR) {
    g.mod = MOD_ALTGR;
    k &= 0x3F;
  } else if (k & MAPA_SHIFT) {
    g.mod = MOD_SHIFT;
    k &= 0x7F;
  }
  g.tecla = k == REEMPLAZO_ISO ? TECLA_ISO : k;
  return true;
}

// Pulsaciones para un carácter Unicode: 0 si no se puede, 1 o 2 si sí
static uint8_t golpesPara(uint32_t cp, Golpe golpes[2]) {
  if (distribucion != DIST_US) {
    if (cp == '~') {
      golpes[0] = {MOD_ALTGR, U_4};
      if (distribucion == DIST_ES_LINUX) return 1;
      golpes[1] = {MOD_NADA, U_ESPACIO};
      return 2;
    }
    for (const Espaciado &e : ESPACIADOS_ES) {
      if (e.cp == cp) {
        golpes[0] = MUERTAS[e.muerta];
        golpes[1] = {MOD_NADA, U_ESPACIO};
        return 2;
      }
    }
    for (const Directo &d : DIRECTOS_ES) {
      if (d.cp == cp) {
        golpes[0] = d.golpe;
        return 1;
      }
    }
    for (const Acentuada &a : ACENTUADAS_ES) {
      if (a.cp == cp) {
        golpes[0] = MUERTAS[a.muerta];
        return golpeAscii(a.base, golpes[1]) ? 2 : 0;
      }
    }
  }
  return golpeAscii(cp, golpes[0]) ? 1 : 0;
}

// Siguiente carácter UTF-8; avanza p. Devuelve 0xFFFD si la secuencia es inválida.
static uint32_t siguienteUtf8(const char *&p) {
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

static void pulsarBloqMayus() {
  // macOS ignora las pulsaciones muy cortas de Bloq Mayús
  Keyboard.press(KEY_CAPS_LOCK);
  delay(150);
  Keyboard.release(KEY_CAPS_LOCK);
  delay(retardoMs);
}

static bool esperarLed(bool estado, uint32_t ms) {
  uint32_t inicio = millis();
  while (ledMayus != estado) {
    if (millis() - inicio >= ms) return false;
    delay(5);
  }
  return true;
}

size_t tecladoEscribir(const char *utf8) {
  bool restaurar = false;
  if (compensar && ledMayus) {
    pulsarBloqMayus();
    esperarLed(false, 500);
    restaurar = true;
  }

  size_t saltados = 0;
  const char *p = utf8;
  while (*p) {
    uint32_t cp = siguienteUtf8(p);
    if (cp == '\r') continue;
    Golpe golpes[2];
    uint8_t n = golpesPara(cp, golpes);
    if (!n) {
      saltados++;
      ultimoNoSoportado = cp;
      continue;
    }
    for (uint8_t i = 0; i < n; i++) golpear(golpes[i]);
  }

  if (restaurar) pulsarBloqMayus();
  return saltados;
}

void tecladoTecla(uint8_t tecla) {
  Keyboard.press(tecla);
  delay(retardoMs);
  Keyboard.releaseAll();
  delay(retardoMs);
}

void tecladoIniciar() {
  Keyboard.begin(KeyboardLayout_es_ES);  // la tabla da igual: se elige en golpeAscii
  Keyboard.onLED(alCambiarLed);
}

void tecladoDistribucion(Distribucion d) {
  if (d < NUM_DISTRIBUCIONES) distribucion = d;
}

Distribucion tecladoDistribucionActual() {
  return distribucion;
}

const char *tecladoNombre(Distribucion d) {
  switch (d) {
    case DIST_ES_LINUX: return "ES Linux";
    case DIST_ES_WINDOWS: return "ES Windows";
    case DIST_US: return "US";
    default: return "?";
  }
}

void tecladoRetardo(uint16_t ms) {
  retardoMs = ms;
}

void tecladoCompensarMayusculas(bool activa) {
  compensar = activa;
}

bool tecladoMayusculas() {
  return ledMayus;
}

bool tecladoLedConocido() {
  return ledConocido;
}

uint32_t tecladoUltimoNoSoportado() {
  return ultimoNoSoportado;
}
