#include <LittleFS.h>
#include <Keyboard.h>
#include "tusb.h"
#include "config.h"
#include "guion.h"
#include "led.h"
#include "mantener.h"
#include "zumbador.h"
#include "muerto.h"

static const char *const FICHERO = "/muerto.bin";
static const char *const TEMPORAL = "/muerto.tmp";
static const char *const FICHERO_GUION = "/muerto.txt";
static const char *const TEMPORAL_GUION = "/muerto.txg";
static const uint32_t MAGIA = 0x4D555201;  // «MUR» y versión 1

static const char *const NOMBRES[] = {"Bloquear PC", "Suspender PC", "Guion"};

struct Config {
  uint32_t magia;
  uint8_t activo;
  uint8_t activa[NUM_ACCIONES_MUERTO];
  uint16_t minutos[NUM_ACCIONES_MUERTO];
};
static Config cfg = {MAGIA, 0, {1, 0, 0}, {15, 60, 30}};

static char guion[LARGO_GUION_MUERTO] = "";
static uint32_t ultimaSenalMs = 0;
static bool hecha[NUM_ACCIONES_MUERTO];      // ya lanzada en esta ausencia
static bool avisada[NUM_ACCIONES_MUERTO];    // ya ha pitado su aviso
static uint32_t ultimoPitidoMs = 0;
static uint32_t eventos = 0;
static char ultimoEvento[32] = "";

// ---- Ficheros ----

static bool escribir(const char *temporal, const char *fichero, const void *datos, size_t n) {
  File f = LittleFS.open(temporal, "w");
  if (!f) return false;
  bool ok = f.write((const uint8_t *)datos, n) == n;
  f.close();
  return ok && LittleFS.rename(temporal, fichero);
}

static void guardar() {
  if (!escribir(TEMPORAL, FICHERO, &cfg, sizeof(cfg))) Serial.println("Hombre muerto: error al guardar");
}

void muertoIniciar() {
  File f = LittleFS.open(FICHERO, "r");
  if (f) {
    Config leida;
    if (f.read((uint8_t *)&leida, sizeof(leida)) == sizeof(leida) && leida.magia == MAGIA) cfg = leida;
    f.close();
  }
  for (int a = 0; a < NUM_ACCIONES_MUERTO; a++)
    cfg.minutos[a] = constrain(cfg.minutos[a], 1, MUERTO_PLAZO_MAX_MIN);
  f = LittleFS.open(FICHERO_GUION, "r");
  if (f) {
    size_t n = f.read((uint8_t *)guion, sizeof(guion) - 1);
    guion[n] = '\0';
    f.close();
    if (guionComprobar(guion)) guion[0] = '\0';  // estropeado: mejor nada
  }
  ultimaSenalMs = millis();
}

// ---- Estado ----

void muertoSenal() {
  ultimaSenalMs = millis();
  for (int a = 0; a < NUM_ACCIONES_MUERTO; a++) hecha[a] = avisada[a] = false;
  ledMuerto(false);
}

bool muertoActivo() { return cfg.activo; }

void muertoActivar(bool activo) {
  cfg.activo = activo;
  muertoSenal();  // el plazo cuenta desde ahora
  guardar();
}

const char *muertoNombre(AccionMuerto a) { return NOMBRES[a]; }
bool muertoAccionActiva(AccionMuerto a) { return cfg.activa[a]; }

void muertoAccionActivar(AccionMuerto a, bool activa) {
  cfg.activa[a] = activa;
  guardar();
}

uint16_t muertoPlazoMin(AccionMuerto a) { return cfg.minutos[a]; }

void muertoPlazo(AccionMuerto a, uint16_t minutos) {
  cfg.minutos[a] = constrain(minutos, 1, MUERTO_PLAZO_MAX_MIN);
  guardar();
}

bool muertoGuionTrasBloqueo() {
  return cfg.activa[MUERTO_GUION] && cfg.activa[MUERTO_BLOQUEAR] &&
         cfg.minutos[MUERTO_GUION] > cfg.minutos[MUERTO_BLOQUEAR];  // si empatan, va antes el guion
}

uint32_t muertoEventos() { return eventos; }
const char *muertoUltimoEvento() { return ultimoEvento; }

static void evento(const char *texto) {
  snprintf(ultimoEvento, sizeof(ultimoEvento), "%s", texto);
  eventos++;
  Serial.printf("Hombre muerto: %s\n", texto);
}

// ---- Guion ----

const char *muertoGuion() { return guion; }

int muertoLineasGuion() {
  if (!guion[0]) return 0;
  int n = 1;
  for (const char *p = guion; *p; p++)
    if (*p == '\n' && p[1]) n++;
  return n;
}

const char *muertoGuardarGuion(const char *nuevo) {
  if (strlen(nuevo) >= sizeof(guion)) return "largo";
  if (strstr(nuevo, "$usuario") || strstr(nuevo, "$contrasena")) return "variables";
  if (guionComprobar(nuevo)) return "sintaxis";
  if (!escribir(TEMPORAL_GUION, FICHERO_GUION, nuevo, strlen(nuevo))) return "guardar";
  strcpy(guion, nuevo);
  return nullptr;
}

// ---- Acciones ----

bool muertoEjecutar(AccionMuerto a) {
  if (!tud_mounted() || tud_suspended()) return false;
  switch (a) {
    case MUERTO_BLOQUEAR:
      mantenerDesactivar();  // ya no tiene sentido mantener la sesión
      guionEjecutar("TECLA super+l", "", "");
      break;
    case MUERTO_SUSPENDER:
      mantenerDesactivar();
      Keyboard.consumerPress(KEY_SLEEP);
      delay(50);
      Keyboard.consumerRelease();
      break;
    case MUERTO_GUION:
      if (!guion[0]) return true;
      ledTecleo(true);
      ledTick();
      guionEjecutar(guion, "", "");
      ledTecleo(false);
      break;
    default: break;
  }
  return true;
}

static void lanzar(AccionMuerto a) {
  hecha[a] = true;
  char t[32];
  if (a == MUERTO_GUION && hecha[MUERTO_BLOQUEAR] && cfg.activa[MUERTO_BLOQUEAR]) {
    snprintf(t, sizeof(t), "Guion omitido");  // el PC ya está bloqueado
  } else if (a == MUERTO_GUION && !guion[0]) {
    snprintf(t, sizeof(t), "Guion vacío");
  } else if (!muertoEjecutar(a)) {
    snprintf(t, sizeof(t), "%s: sin PC", NOMBRES[a]);
  } else {
    snprintf(t, sizeof(t), "%s: hecho", NOMBRES[a]);
  }
  evento(t);
}

// La siguiente acción pendiente: la de menor plazo (con el mismo plazo, el
// guion antes que el bloqueo, y el bloqueo antes que suspender)
static int siguiente() {
  int mejor = -1;
  for (int i = NUM_ACCIONES_MUERTO - 1; i >= 0; i--) {
    AccionMuerto a = i == 0 ? MUERTO_GUION : i == 1 ? MUERTO_BLOQUEAR : MUERTO_SUSPENDER;
    if (!cfg.activa[a] || hecha[a]) continue;
    if (mejor < 0 || cfg.minutos[a] <= cfg.minutos[mejor]) mejor = a;
  }
  return mejor;
}

int muertoAvisoSegundos(AccionMuerto &a) {
  if (!cfg.activo) return -1;
  int s = siguiente();
  if (s < 0) return -1;
  uint32_t plazo = cfg.minutos[s] * 60000UL, pasado = millis() - ultimaSenalMs;
  // Con plazos cortos el aviso no puede ocuparlo todo: como mucho la mitad
  uint32_t aviso = min((uint32_t)MUERTO_AVISO_MS, plazo / 2);
  if (pasado + aviso < plazo) return -1;
  a = (AccionMuerto)s;
  return pasado >= plazo ? 0 : (plazo - pasado + 999) / 1000;
}

void muertoTick() {
  if (!cfg.activo) return;
  uint32_t ahora = millis(), pasado = ahora - ultimaSenalMs;
  // Todas las que hayan vencido, en orden
  for (int s = siguiente(); s >= 0 && pasado >= cfg.minutos[s] * 60000UL; s = siguiente())
    lanzar((AccionMuerto)s);

  AccionMuerto a;
  int segundos = muertoAvisoSegundos(a);
  ledMuerto(segundos >= 0);
  if (segundos < 0) return;
  // Pitido al empezar el aviso y luego cada MUERTO_REPETIR_PITIDO_MS
  if (!avisada[a] || ahora - ultimoPitidoMs >= MUERTO_REPETIR_PITIDO_MS) {
    avisada[a] = true;
    ultimoPitidoMs = ahora;
    zumbadorPitido(MUERTO_PITIDO_HZ, MUERTO_PITIDO_MS);
  }
}
