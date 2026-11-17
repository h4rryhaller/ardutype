#include "config.h"
#include "almacen.h"
#include "boveda.h"
#include "cripto.h"
#include "led.h"
#include "ui.h"
#include "notificacion.h"
#include "guion.h"
#include "muerto.h"
#include "atecc.h"
#include "zumbador.h"
#include "admin.h"

// La orden más larga: SERV con todos los campos al máximo, en hex (2 caracteres
// por byte, sin el 0 final) y separados por espacios. Antes era 1200 fijo y un
// guion de más de ~500 bytes desbordaba la línea («@ERR largo», 2026-11-17).
static const size_t MAX_LINEA = 4 + 1 + 2 * TAM_UUID + 2 * (1 + 2 * (LARGO_NOMBRE - 1)) +
                                2 * (1 + 2 * (LARGO_CAMPO - 1)) + 1 + 2 * (LARGO_GUION - 1) + 16;
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
  PREGUNTANDO_MUERTO,   // ¿guardar el guion del hombre muerto?
  PREGUNTANDO_CLAVE,    // ¿crear (o sustituir) la clave SSH?
  PREGUNTANDO_FIRMA,    // ¿firmar lo que pide el agente SSH?
  PREGUNTANDO_ECDH,     // ¿descifrar (ECDH) lo que pide el PC?
};
static EstadoSesion sesion = INACTIVA;

static bool preguntando() {
  return sesion == PREGUNTANDO_ENVIO || sesion == PREGUNTANDO_CAMBIOS || sesion == PREGUNTANDO_MUERTO ||
         sesion == PREGUNTANDO_CLAVE || sesion == PREGUNTANDO_FIRMA || sesion == PREGUNTANDO_ECDH;
}
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
  if (preguntando()) error("cancelado");
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

// ---- Guion del hombre muerto (muerto.h) ----

static char guionMuerto[LARGO_GUION_MUERTO];

static void alResponderMuerto(bool si) {
  sesion = ESPERANDO;
  if (!si) {
    uiPcTexto("Guion rechazado");
    return error("cancelado");
  }
  const char *fallo = muertoGuardarGuion(guionMuerto);
  if (fallo) {
    uiPcTexto("Error con el guion");
    return error(fallo);
  }
  uiPcTexto(guionMuerto[0] ? "Guion guardado" : "Guion borrado");
  responder("OK");
}

// MUERTO -> @MUERTO <guion>;  MUERTO <guion|-> -> (confirmación) @OK
static void ordenMuerto(char **p, int n) {
  if (n == 1) {
    Serial.print("@MUERTO");
    escribirTexto(muertoGuion());
    Serial.println();
    return;
  }
  if (n != 2 || !leerTextoHex(p[1], guionMuerto, sizeof(guionMuerto), true)) return error("campos");
  if (strstr(guionMuerto, "$usuario") || strstr(guionMuerto, "$contrasena")) return error("variables");
  int linea = guionComprobar(guionMuerto);
  if (linea) {
    Serial.printf("@ERR sintaxis %d %s\n", linea, guionError());
    return;
  }
  int lineas = 0;
  for (const char *c = guionMuerto; *c; c++)
    if (*c == '\n' || !c[1]) lineas++;
  if (lineas) snprintf(pregunta, sizeof(pregunta), "¿Guion de %d línea%s?", lineas, lineas == 1 ? "" : "s");
  else snprintf(pregunta, sizeof(pregunta), "¿Borrar el guion?");
  sesion = PREGUNTANDO_MUERTO;
  uiPcPreguntar(pregunta, lineas ? "Sí, guardar" : "Sí, borrar", alResponderMuerto);
}

// ---- Agente SSH (herramienta/agente_ssh.py) ----
//
// La clave P-256 vive en la ranura RANURA_SSH del ATECC y nunca sale de él.
//   SSHPUB                  -> @SSHPUB <pública, 64 bytes X||Y> | @ERR sinclave
//                              (también bloqueado: la pública no es secreta)
//   SSHGENERAR              -> confirmación -> @SSHPUB <pública>  (CIFPUB/CIFGENERAR: cifrado)
//   SSHFIRMAR <resumen> <motivo>
//                           -> confirmación -> @FIRMA <r||s, 64 bytes>
// El resumen es el SHA-256 de lo que hay que firmar (lo calcula el PC: el
// Ardutype firma a ciegas); el motivo, un texto corto para la pregunta.

static uint8_t resumenSsh[32];

static uint8_t ranuraClave = RANURA_SSH;  // la que se va a crear

static void escribirPublica(uint8_t ranura, const uint8_t pub[64]) {
  Serial.print(ranura == RANURA_SSH ? "@SSHPUB " : "@CIFPUB ");
  escribirHex(pub, 64);
  Serial.println();
}

static void ordenClavePub(uint8_t ranura) {
  uint8_t pub[64];
  if (!ateccClavePublica(ranura, pub)) return error(ateccEstado() == ATECC_LISTO ? "sinclave" : "atecc");
  escribirPublica(ranura, pub);
}

static void alResponderClave(bool si) {
  sesion = ESPERANDO;
  if (!si) {
    uiPcTexto("Clave no creada");
    return error("cancelado");
  }
  uiPcTexto("Creando clave...");
  uint8_t pub[64];
  if (!ateccGenerarClave(ranuraClave, pub)) {
    uiPcTexto("Error del chip");
    return error("atecc");
  }
  uiPcTexto(ranuraClave == RANURA_SSH ? "Clave SSH creada" : "Clave de cifrado creada");
  escribirPublica(ranuraClave, pub);
}

static void ordenGenerar(uint8_t ranura) {
  uint8_t pub[64];
  bool hay = ateccClavePublica(ranura, pub);
  ranuraClave = ranura;
  sesion = PREGUNTANDO_CLAVE;
  if (ranura == RANURA_SSH)
    uiPcPreguntar(hay ? "¿Sustituir clave SSH?" : "¿Crear clave SSH?", hay ? "Sí, sustituir" : "Sí, crear",
                  alResponderClave);
  else  // sustituirla deja sin poder descifrar lo cifrado con la anterior
    uiPcPreguntar(hay ? "¿Sustituir cifrado?" : "¿Crear cifrado?", hay ? "Sí, sustituir" : "Sí, crear",
                  alResponderClave);
}

static void alResponderFirma(bool si) {
  sesion = ESPERANDO;
  if (!si) {
    criptoBorrar(resumenSsh, sizeof(resumenSsh));
    uiPcTexto("Firma rechazada");
    return error("cancelado");
  }
  uint8_t firma[64];
  bool ok = ateccFirmar(RANURA_SSH, resumenSsh, firma);
  criptoBorrar(resumenSsh, sizeof(resumenSsh));
  if (!ok) {
    uiPcTexto("Error al firmar");
    return error("atecc");
  }
  uiPcTexto("Firmado");
  Serial.print("@FIRMA ");
  escribirHex(firma, sizeof(firma));
  Serial.println();
}

static void ordenSshFirmar(char **p, int n) {
  char motivo[20];
  if (n != 3 || leerHex(p[1], resumenSsh, sizeof(resumenSsh)) != (int)sizeof(resumenSsh) ||
      !leerTextoHex(p[2], motivo, sizeof(motivo), false))
    return error("campos");
  snprintf(pregunta, sizeof(pregunta), "¿%s?", motivo);
  sesion = PREGUNTANDO_FIRMA;
  uiPcPreguntar(pregunta, "Sí, firmar", alResponderFirma);
  zumbadorPitido(NOTI_PITIDO_HZ, NOTI_PITIDO_MS);  // puede estar mirando a otra parte
}

// ---- Cifrado (herramienta/age_plugin_ardutype.py) ----
//
//   CIFPUB                 -> @CIFPUB <pública de RANURA_CIFRADO> | @ERR sinclave
//   CIFGENERAR             -> confirmación -> @CIFPUB <pública>
//   ECDH <pública> <motivo> -> confirmación -> @ECDH <secreto, 32 bytes>
// El secreto sale en claro hacia el PC (que deriva la clave del archivo): como
// la firma, protege que nada se descifre sin confirmarlo y que la privada no salga.

static uint8_t ajenaEcdh[64];

static void alResponderEcdh(bool si) {
  sesion = ESPERANDO;
  if (!si) {
    uiPcTexto("Descifrado rechazado");
    return error("cancelado");
  }
  uint8_t secreto[32];
  bool ok = ateccEcdh(ajenaEcdh, secreto);
  if (!ok) {
    uiPcTexto("Error del chip");
    return error("atecc");
  }
  uiPcTexto("Descifrado");
  Serial.print("@ECDH ");
  escribirHex(secreto, sizeof(secreto));
  Serial.println();
  criptoBorrar(secreto, sizeof(secreto));
}

static void ordenEcdh(char **p, int n) {
  char motivo[20];
  if (n != 3 || leerHex(p[1], ajenaEcdh, sizeof(ajenaEcdh)) != (int)sizeof(ajenaEcdh) ||
      !leerTextoHex(p[2], motivo, sizeof(motivo), false))
    return error("campos");
  snprintf(pregunta, sizeof(pregunta), "¿%s?", motivo);
  sesion = PREGUNTANDO_ECDH;
  uiPcPreguntar(pregunta, "Sí, descifrar", alResponderEcdh);
  zumbadorPitido(NOTI_PITIDO_HZ, NOTI_PITIDO_MS);
}

// ---- Notificaciones (notificacion.h) ----

static bool leerNumero(const char *s, uint32_t &valor, uint32_t maximo) {
  char *fin;
  unsigned long v = strtoul(s, &fin, 10);
  if (!*s || *fin || v > maximo) return false;
  valor = v;
  return true;
}

// AVISO [MUDO] <origen> <texto> | PREGUNTA <id> <segundos> <origen> <texto> | RETIRAR <id>
// | QUITAR <origen>
static void ordenNotificacion(char **p, int n) {
  if (bovedaEstado() != BOVEDA_CERRADA && bovedaEstado() != BOVEDA_ABIERTA) return error("estado");
  if (sesion != INACTIVA) return error("ocupado");  // sincronizando
  const char *orden = p[0];
  uint32_t id, segundos;
  if (!strcmp(orden, "RETIRAR")) {
    if (n != 2 || !leerNumero(p[1], id, UINT32_MAX)) return error("campos");
    return notiRetirar(id) ? responder("OK") : error("id");
  }
  if (!strcmp(orden, "QUITAR")) {
    char origen[LARGO_ORIGEN_NOTI];
    if (n != 2 || !leerTextoHex(p[1], origen, sizeof(origen), false)) return error("campos");
    notiQuitarAviso(origen);  // si ya no estaba, también vale
    return responder("OK");
  }
  bool pregunta = !strcmp(orden, "PREGUNTA");
  bool mudo = !pregunta && n == 4 && !strcmp(p[1], "MUDO");
  if (n != (pregunta ? 5 : mudo ? 4 : 3)) return error("campos");
  if (pregunta && (!leerNumero(p[1], id, UINT32_MAX) || !leerNumero(p[2], segundos, NOTI_MAX_SEGUNDOS) || !segundos))
    return error("campos");
  char origen[LARGO_ORIGEN_NOTI], texto[LARGO_TEXTO_NOTI];
  if (!leerTextoHex(p[n - 2], origen, sizeof(origen), false) || !leerTextoHex(p[n - 1], texto, sizeof(texto), false))
    return error("texto");
  bool ok = pregunta ? notiPreguntar(id, segundos, origen, texto) : notiAvisar(origen, texto, !mudo);
  ok ? responder("OK") : error("ocupado");
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

// Solo lectura, para estudiar el chip (herramienta/atecc_ranuras.py):
//   ATECC       -> @ATECC <zona de configuración, 128 bytes>
//   ATECCPUB n  -> @ATECCPUB <clave pública de la ranura n, 64 bytes>
// GenKey en modo 0 solo calcula la pública de la privada que ya hay: no escribe.
// La configuración la puede leer cualquiera que tenga el chip: no hace falta la bóveda.
static void ordenAtecc(char **p, int n) {
  if (ateccEstado() == ATECC_AUSENTE) return error("atecc");
  char codigo[16];
  if (!strcmp(p[0], "ATECC")) {
    uint8_t cfg[128];
    for (uint8_t b = 0; b < 4; b++)
      if (!ateccOrdenCruda(0x02, 0x80, b << 3, nullptr, 0, cfg + 32 * b, 32)) {
        snprintf(codigo, sizeof(codigo), "atecc %02x", ateccUltimoEstado());
        return error(codigo);
      }
    Serial.print("@ATECC ");
    escribirHex(cfg, sizeof(cfg));
    Serial.println();
    return;
  }
  int ranura = n == 2 ? atoi(p[1]) : -1;
  if (ranura < 0 || ranura > 15) return error("orden");
  uint8_t pub[64];
  if (!ateccOrdenCruda(0x40, 0x00, ranura, nullptr, 0, pub, sizeof(pub))) {
    snprintf(codigo, sizeof(codigo), "atecc %02x", ateccUltimoEstado());
    return error(codigo);
  }
  Serial.print("@ATECCPUB ");
  escribirHex(pub, sizeof(pub));
  Serial.println();
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
  if (!strcmp(orden, "AVISO") || !strcmp(orden, "PREGUNTA") || !strcmp(orden, "RETIRAR") ||
      !strcmp(orden, "QUITAR"))
    return ordenNotificacion(p, n);
  if (!strcmp(orden, "ATECC") || !strcmp(orden, "ATECCPUB")) return ordenAtecc(p, n);
  if (!strcmp(orden, "SSHPUB")) return ordenClavePub(RANURA_SSH);
  if (!strcmp(orden, "CIFPUB")) return ordenClavePub(RANURA_CIFRADO);
  if (bovedaEstado() != BOVEDA_ABIERTA) return error("bloqueado");
  if (preguntando()) return error("ocupado");
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
  } else if (!strcmp(orden, "MUERTO")) {
    if (sesion == RECIBIENDO) return error("orden");
    ordenMuerto(p, n);
  } else if (!strcmp(orden, "SSHGENERAR") || !strcmp(orden, "CIFGENERAR")) {
    if (sesion == RECIBIENDO) return error("orden");
    ordenGenerar(orden[0] == 'S' ? RANURA_SSH : RANURA_CIFRADO);
  } else if (!strcmp(orden, "ECDH")) {
    if (sesion == RECIBIENDO) return error("orden");
    ordenEcdh(p, n);
  } else if (!strcmp(orden, "SSHFIRMAR")) {
    if (sesion == RECIBIENDO) return error("orden");
    ordenSshFirmar(p, n);
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
  if (sesion != INACTIVA && !preguntando() && millis() - ultimaOrdenMs >= SESION_PC_MS) terminar();
}
