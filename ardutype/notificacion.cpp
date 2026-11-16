#include "tusb.h"

#include "config.h"
#include "led.h"
#include "zumbador.h"
#include "notificacion.h"

static TipoNotificacion tipo = NOTI_NINGUNA;
static uint32_t numero = 0;
static uint32_t id = 0;
static char origen[LARGO_ORIGEN_NOTI];
static char texto[LARGO_TEXTO_NOTI];
static uint32_t inicioMs = 0;
static uint32_t plazoMs = 0;
static uint32_t ultimoPitidoMs = 0;
static uint32_t sinPuertoDesdeMs = 0;  // 0: el puerto sigue abierto

static void copiar(char *dst, size_t tam, const char *src) {
  strncpy(dst, src, tam - 1);
  dst[tam - 1] = '\0';
  for (char *c = dst; *c; c++)
    if ((uint8_t)*c < 0x20) *c = ' ';  // saltos de línea y demás: espacio
}

static void pitar() {
  zumbadorPitido(NOTI_PITIDO_HZ, NOTI_PITIDO_MS);
  ultimoPitidoMs = millis();
}

static void empezar(TipoNotificacion t, const char *o, const char *x) {
  copiar(origen, sizeof(origen), o);
  copiar(texto, sizeof(texto), x);
  tipo = t;
  numero++;
  inicioMs = millis();
  sinPuertoDesdeMs = 0;
  pitar();
  ledNotificacion(t == NOTI_PREGUNTA ? LED_NOTI_RAPIDO : LED_NOTI_LENTO);
}

static void terminar() {
  tipo = NOTI_NINGUNA;
  ledNotificacion(LED_NOTI_NADA);
}

static void contestar(const char *respuesta) {
  Serial.printf("@RESPUESTA %lu %s\n", (unsigned long)id, respuesta);
  terminar();
}

bool notiAvisar(const char *o, const char *x) {
  if (tipo == NOTI_PREGUNTA) return false;
  empezar(NOTI_AVISO, o, x);
  return true;
}

bool notiPreguntar(uint32_t n, uint32_t segundos, const char *o, const char *x) {
  if (tipo == NOTI_PREGUNTA) return false;
  id = n;
  plazoMs = segundos * 1000;
  empezar(NOTI_PREGUNTA, o, x);
  return true;
}

bool notiRetirar(uint32_t n) {
  if (tipo != NOTI_PREGUNTA || n != id) return false;
  terminar();
  return true;
}

void notiResponder(bool si) {
  if (tipo == NOTI_PREGUNTA) contestar(si ? "SI" : "NO");
}

void notiDescartar() {
  if (tipo == NOTI_AVISO) terminar();
}

void notiTick() {
  if (tipo == NOTI_NINGUNA) return;
  uint32_t ahora = millis();
  if (tipo == NOTI_AVISO) {
    if (ahora - inicioMs >= NOTI_CADUCA_AVISO_MS) terminar();
    return;
  }
  if (ahora - inicioMs >= plazoMs) return contestar("TIEMPO");
  // Quien preguntó ha cerrado el puerto (Ctrl+C, se ha matado el script):
  // nadie recogería la respuesta. Se exige un rato seguido sin él.
  if (tud_cdc_connected()) {
    sinPuertoDesdeMs = 0;
  } else if (!sinPuertoDesdeMs) {
    sinPuertoDesdeMs = ahora | 1;
  } else if (ahora - sinPuertoDesdeMs >= NOTI_SIN_PUERTO_MS) {
    Serial.println("Pregunta retirada: puerto cerrado");
    return terminar();
  }
  if (ahora - ultimoPitidoMs >= NOTI_REPETIR_PITIDO_MS) pitar();
}

TipoNotificacion notiTipo() {
  return tipo;
}

uint32_t notiNumero() {
  return numero;
}

const char *notiOrigen() {
  return origen;
}

const char *notiTexto() {
  return texto;
}

uint32_t notiRestanteMs() {
  if (tipo != NOTI_PREGUNTA) return 0;
  uint32_t pasado = millis() - inicioMs;
  return pasado < plazoMs ? plazoMs - pasado : 0;
}
