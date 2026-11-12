#include "config.h"
#include "almacen.h"
#include "boveda.h"
#include "cripto.h"
#include "led.h"
#include "ui.h"
#include "admin.h"

static const size_t MAX_LINEA = 1200;  // SERV con todos los campos al máximo (el guion manda)
static const int MAX_PALABRAS = 8;

static char linea[MAX_LINEA];
static size_t largoLinea = 0;
static bool desbordada = false;

enum EstadoSesion : uint8_t {
  INACTIVA,
  ESPERANDO,            // pantalla del PC abierta, sin nada en curso
  PREGUNTANDO_ENVIO,    // ¿enviar los cambios al PC?
  ENVIADO,              // cambios enviados, falta CONFIRMADO
  RECIBIENDO,           // llegan las entradas de KeePassXC
  PREGUNTANDO_CAMBIOS,  // ¿aplicar lo que ha llegado?
};
static EstadoSesion sesion = INACTIVA;
static uint32_t ultimaOrdenMs = 0;

// Lo que llega de KeePassXC, hasta que se confirma. Los guiones van en un
// almacén aparte (como en almacen.cpp) para no inflar cada entrante.
struct Entrante {
  uint8_t uuid[TAM_UUID];
  int8_t guion;  // ranura en entGuiones, o SIN_GUION
  char categoria[LARGO_NOMBRE];
  char nombre[LARGO_NOMBRE];
  char usuario[LARGO_CAMPO];
  char contrasena[LARGO_CAMPO];
};
static Entrante entrantes[MAX_SERVICIOS];
static int numEntrantes = 0;
static char entGuiones[MAX_GUIONES][LARGO_GUION];
static int numEntGuiones = 0;
static char pregunta[32];

static const char *guionEntrante(const Entrante &e) {
  return e.guion != SIN_GUION ? entGuiones[e.guion] : "";
}

// ---- Salida ----

static void escribirHex(const void *datos, size_t n) {
  static const char HEX_[] = "0123456789abcdef";
  const uint8_t *p = (const uint8_t *)datos;
  if (!n) Serial.write('-');
  for (size_t i = 0; i < n; i++) {
    Serial.write(HEX_[p[i] >> 4]);
    Serial.write(HEX_[p[i] & 15]);
  }
}

static void escribirTexto(const char *texto) {
  Serial.write(' ');
  escribirHex(texto, strlen(texto));
}

static void responder(const char *texto) {
  Serial.print('@');
  Serial.println(texto);
}

static void error(const char *motivo) {
  Serial.print("@ERR ");
  Serial.println(motivo);
}

// ---- Entrada ----

static int valorHex(char c) {
  if (c >= '0' && c <= '9') return c - '0';
  if (c >= 'a' && c <= 'f') return c - 'a' + 10;
  if (c >= 'A' && c <= 'F') return c - 'A' + 10;
  return -1;
}

// Hexadecimal a bytes; -1 si no es válido o no cabe en tam
static int leerHex(const char *hex, uint8_t *dst, size_t tam) {
  if (!strcmp(hex, "-")) return 0;
  size_t n = strlen(hex);
  if (n % 2 || n / 2 > tam) return -1;
  for (size_t i = 0; i < n / 2; i++) {
    int a = valorHex(hex[2 * i]), b = valorHex(hex[2 * i + 1]);
    if (a < 0 || b < 0) return -1;
    dst[i] = a << 4 | b;
  }
  return n / 2;
}

// Texto en hexadecimal a un campo con su 0 final; vacío solo si se permite
static bool leerTextoHex(const char *hex, char *campo, size_t tam, bool vacio) {
  int n = leerHex(hex, (uint8_t *)campo, tam - 1);
  if (n < 0 || (!n && !vacio) || memchr(campo, '\0', n)) return false;
  campo[n] = '\0';
  return true;
}

// ---- Sesión ----

static void borrarEntrantes() {
  criptoBorrar(entrantes, sizeof(entrantes));
  criptoBorrar(entGuiones, sizeof(entGuiones));
  numEntrantes = 0;
  numEntGuiones = 0;
}

static void terminar() {
  borrarEntrantes();
  sesion = INACTIVA;
  uiPcCerrar();
}

void adminCancelar() {
  if (sesion == PREGUNTANDO_ENVIO || sesion == PREGUNTANDO_CAMBIOS) error("cancelado");
  borrarEntrantes();
  sesion = INACTIVA;
}

// ---- Del dispositivo al PC ----

static void enviarCambios() {
  for (int id = 0; id < MAX_SERVICIOS; id++) {
    Servicio *s = almacenServicio(id);
    if (!s || !s->cambiado) continue;
    Serial.printf("@SERV %d ", id);
    escribirHex(s->uuid, s->enlazado ? TAM_UUID : 0);
    escribirTexto(almacenCategoria(s->categoria));
    escribirTexto(s->nombre);
    escribirTexto(s->usuario);
    escribirTexto(s->contrasena);
    escribirTexto(almacenGuion(id));
    Serial.println();
  }
  for (int i = 0; i < almacenNumBorrados(); i++) {
    const Borrado *b = almacenBorrado(i);
    Serial.print("@BORRADO ");
    escribirHex(b->uuid, TAM_UUID);
    escribirTexto(b->nombre);
    Serial.println();
  }
  responder("FIN");
  sesion = ENVIADO;
  uiPcTexto("Cambios enviados");
}

static void alResponderEnvio(bool si) {
  if (!si) {
    sesion = ESPERANDO;
    uiPcTexto("Envío cancelado");
    return error("cancelado");
  }
  enviarCambios();
}

// ---- Del PC al dispositivo ----

static int entranteConUuid(const uint8_t uuid[TAM_UUID]) {
  for (int i = 0; i < numEntrantes; i++)
    if (!memcmp(entrantes[i].uuid, uuid, TAM_UUID)) return i;
  return -1;
}

static bool distinto(const Entrante &e, int id) {
  Servicio *s = almacenServicio(id);
  return !s || strcmp(e.categoria, almacenCategoria(s->categoria)) || strcmp(e.nombre, s->nombre) ||
         strcmp(e.usuario, s->usuario) || strcmp(e.contrasena, s->contrasena) ||
         strcmp(guionEntrante(e), almacenGuion(id));
}

struct Resumen {
  int nuevos, cambiados, borrados, categoriasNuevas;
};

static Resumen calcularResumen() {
  Resumen r = {0, 0, 0, 0};
  for (int i = 0; i < numEntrantes; i++) {
    const Entrante &e = entrantes[i];
    int id = almacenBuscarUuid(e.uuid);
    if (id < 0) r.nuevos++;
    else if (distinto(e, id)) r.cambiados++;

    bool categoriaNueva = almacenBuscarCategoria(e.categoria) < 0;
    for (int j = 0; j < i && categoriaNueva; j++)
      if (!strcmp(entrantes[j].categoria, e.categoria)) categoriaNueva = false;
    r.categoriasNuevas += categoriaNueva;
  }
  for (int id = 0; id < MAX_SERVICIOS; id++) {
    Servicio *s = almacenServicio(id);
    if (s && s->enlazado && entranteConUuid(s->uuid) < 0) r.borrados++;
  }
  return r;
}

static void aplicarEntrantes() {
  for (int id = 0; id < MAX_SERVICIOS; id++) {
    Servicio *s = almacenServicio(id);
    if (s && s->enlazado && entranteConUuid(s->uuid) < 0) almacenOlvidarServicio(id);
  }
  for (int i = 0; i < numEntrantes; i++) {
    const Entrante &e = entrantes[i];
    int c = almacenBuscarCategoria(e.categoria);
    if (c < 0) c = almacenNuevaCategoria(e.categoria);  // ya se comprobó que caben
    int id = almacenBuscarUuid(e.uuid);
    if (id < 0) id = almacenNuevoServicio(c, e.nombre, e.usuario, e.contrasena);
    Servicio *s = almacenServicio(id);
    if (!s) continue;
    s->categoria = c;
    almacenCopiar(s->nombre, sizeof(s->nombre), e.nombre);
    almacenCopiar(s->usuario, sizeof(s->usuario), e.usuario);
    almacenCopiar(s->contrasena, sizeof(s->contrasena), e.contrasena);
    almacenPonerGuion(id, guionEntrante(e));
    memcpy(s->uuid, e.uuid, TAM_UUID);
    s->enlazado = true;
    s->cambiado = false;
  }
}

static void alResponderCambios(bool si) {
  if (!si) {
    borrarEntrantes();
    sesion = ESPERANDO;
    uiPcTexto("Cambios rechazados");
    return error("cancelado");
  }
  aplicarEntrantes();
  borrarEntrantes();
  sesion = ESPERANDO;
  if (!bovedaGuardar()) {
    ledError();
    uiPcTexto("Error al guardar");
    return error("guardar");
  }
  ledOk();
  uiPcTexto("Sincronizado");
  responder("OK");
}

static void recibirServicio(char **p, int n) {
  if (n != 7) return error("campos");
  if (numEntrantes >= MAX_SERVICIOS) return error("lleno");
  Entrante &e = entrantes[numEntrantes];
  e.guion = SIN_GUION;
  if (leerHex(p[1], e.uuid, TAM_UUID) != TAM_UUID) return error("uuid");
  if (entranteConUuid(e.uuid) >= 0) return error("repetido");
  if (!leerTextoHex(p[2], e.categoria, sizeof(e.categoria), false) ||
      !leerTextoHex(p[3], e.nombre, sizeof(e.nombre), false) ||
      !leerTextoHex(p[4], e.usuario, sizeof(e.usuario), true) ||
      !leerTextoHex(p[5], e.contrasena, sizeof(e.contrasena), true)) {
    criptoBorrar(&e, sizeof(e));
    return error("texto");
  }
  if (strcmp(p[6], "-")) {  // trae guion: a una ranura del almacén de entrada
    if (numEntGuiones >= MAX_GUIONES) return error("guiones");
    if (!leerTextoHex(p[6], entGuiones[numEntGuiones], LARGO_GUION, false)) {
      criptoBorrar(&e, sizeof(e));
      return error("texto");
    }
    e.guion = numEntGuiones++;
  }
  numEntrantes++;
  responder("OK");
}

static void terminarRecepcion() {
  Resumen r = calcularResumen();
  if (almacenNumCategorias() + r.categoriasNuevas > MAX_CATEGORIAS) {
    borrarEntrantes();
    sesion = ESPERANDO;
    return error("categorias");
  }
  if (almacenTotalServicios() - r.borrados + r.nuevos > MAX_SERVICIOS) {
    borrarEntrantes();
    sesion = ESPERANDO;
    return error("lleno");
  }
  Serial.printf("@RESUMEN %d %d %d\n", r.nuevos, r.cambiados, r.borrados);
  if (!r.nuevos && !r.cambiados && !r.borrados) {
    borrarEntrantes();
    sesion = ESPERANDO;
    uiPcTexto("Ya estaba al día");
    return responder("OK");
  }
  snprintf(pregunta, sizeof(pregunta), "¿Aplicar +%d ~%d -%d?", r.nuevos, r.cambiados, r.borrados);
  sesion = PREGUNTANDO_CAMBIOS;
  uiPcPreguntar(pregunta, "Sí, aplicar", alResponderCambios);
}

// ---- Órdenes ----

static const char *nombreEstado() {
  switch (bovedaEstado()) {
    case BOVEDA_SIN_FS: return "SIN_FS";
    case BOVEDA_NUEVA: return "NUEVA";
    case BOVEDA_CERRADA: return "CERRADA";
    default: return "ABIERTA";
  }
}

static void procesar(char *texto) {
  char *p[MAX_PALABRAS];
  int n = 0;
  for (char *t = strtok(texto, " \r"); t && n < MAX_PALABRAS; t = strtok(nullptr, " \r")) p[n++] = t;
  if (!n) return;
  const char *orden = p[0];

  if (!strcmp(orden, "HOLA")) {
    Serial.printf("@ARDUTYPE 1 %s\n", nombreEstado());
    return;
  }
  if (!strcmp(orden, "ADIOS")) {
    if (sesion != INACTIVA) terminar();
    return responder("OK");
  }
  if (bovedaEstado() != BOVEDA_ABIERTA) return error("bloqueado");
  if (sesion == PREGUNTANDO_ENVIO || sesion == PREGUNTANDO_CAMBIOS) return error("ocupado");
  if (sesion == INACTIVA) {
    if (!uiPcAbrir()) return error("ocupado");  // a medias de algo que no se puede cortar
    sesion = ESPERANDO;
  }

  if (!strcmp(orden, "PENDIENTES")) {
    Serial.printf("@PENDIENTES %d\n", almacenPendientes());
  } else if (!strcmp(orden, "RECIBIR")) {
    if (sesion == RECIBIENDO) return error("orden");
    int pendientes = almacenPendientes();
    if (!pendientes) return enviarCambios();
    snprintf(pregunta, sizeof(pregunta), "¿Enviar %d cambio%s?", pendientes, pendientes == 1 ? "" : "s");
    sesion = PREGUNTANDO_ENVIO;
    uiPcPreguntar(pregunta, "Sí, enviar", alResponderEnvio);
  } else if (!strcmp(orden, "ENLAZAR")) {
    uint8_t uuid[TAM_UUID];
    Servicio *s = n == 3 ? almacenServicio(atoi(p[1])) : nullptr;
    if (sesion != ENVIADO || !s || s->enlazado) return error("orden");
    if (leerHex(p[2], uuid, TAM_UUID) != TAM_UUID || almacenBuscarUuid(uuid) >= 0) return error("uuid");
    memcpy(s->uuid, uuid, TAM_UUID);
    s->enlazado = true;
    responder("OK");
  } else if (!strcmp(orden, "CONFIRMADO")) {
    if (sesion != ENVIADO) return error("orden");
    almacenOlvidarPendientes();
    sesion = ESPERANDO;
    if (!bovedaGuardar()) return error("guardar");
    uiPcTexto("Cambios recogidos");
    responder("OK");
  } else if (!strcmp(orden, "ENVIAR")) {
    if (almacenPendientes()) return error("pendientes");
    borrarEntrantes();
    sesion = RECIBIENDO;
    uiPcTexto("Recibiendo...");
    responder("OK");
  } else if (!strcmp(orden, "SERV")) {
    if (sesion != RECIBIENDO) return error("orden");
    recibirServicio(p, n);
  } else if (!strcmp(orden, "FIN")) {
    if (sesion != RECIBIENDO) return error("orden");
    terminarRecepcion();
  } else {
    error("orden");
  }
}

void adminTick() {
  while (Serial.available()) {
    char c = Serial.read();
    if (c == '\n') {
      linea[largoLinea] = '\0';
      if (desbordada) error("largo");
      else {
        ultimaOrdenMs = millis();
        procesar(linea);
      }
      criptoBorrar(linea, largoLinea);  // puede llevar contraseñas
      largoLinea = 0;
      desbordada = false;
    } else if (largoLinea < MAX_LINEA - 1) {
      linea[largoLinea++] = c;
    } else {
      desbordada = true;
    }
  }

  // Si el PC desaparece a medias, la sesión caduca (no mientras se pregunta)
  bool preguntando = sesion == PREGUNTANDO_ENVIO || sesion == PREGUNTANDO_CAMBIOS;
  if (sesion != INACTIVA && !preguntando && millis() - ultimaOrdenMs >= SESION_PC_MS) terminar();
}
