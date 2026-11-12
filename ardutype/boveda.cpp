#include <LittleFS.h>

#include "config.h"
#include "cripto.h"
#include "almacen.h"
#include "atecc.h"
#include "boveda.h"
#include "dice_lista.h"

static const char *const FICHERO = "/boveda.bin";
static const char *const TEMPORAL = "/boveda.tmp";
static const char MAGIA[4] = {'A', 'R', 'D', 'T'};
static const uint8_t VERSION = 1;
static const char *const INFO_RECUPERACION = "ardutype recuperacion v1";
static const size_t BYTES_RECUPERACION = 16;
static const char *const FICHERO_CHIP = "/chip.bin";
static const char *const TEMPORAL_CHIP = "/chip.tmp";
static const char MAGIA_CHIP[4] = {'A', 'R', 'D', 'C'};
static const char *const INFO_PIN_CHIP = "ardutype pin chip v1";

// RANURA_PIN_CHIP: el PBKDF2 del PIN pasa por el ATECC608 antes de dar la KEK.
// RANURA_RECUPERACION es la clave base32 de antes; las bóvedas nuevas usan palabras.
enum : uint8_t {
  RANURA_VACIA = 0,
  RANURA_PIN = 1,
  RANURA_RECUPERACION = 2,
  RANURA_PIN_CHIP = 3,
  RANURA_PALABRAS = 4,
};

// Clave maestra envuelta. Lo que va antes de nonce (tipo, iteraciones y sal)
// se autentica al envolver: no se puede cambiar sin que se note.
struct __attribute__((packed)) Ranura {
  uint8_t tipo;
  uint8_t reservado[3];
  uint32_t iteraciones;
  uint8_t sal[16];
  uint8_t nonce[TAM_NONCE];
  uint8_t clave[TAM_CLAVE];
  uint8_t tag[TAM_TAG];
};
static const size_t AAD_RANURA = offsetof(Ranura, nonce);

// Cabecera en claro; entera es el AAD del cuerpo cifrado
struct __attribute__((packed)) Cabecera {
  char magia[4];
  uint8_t version;
  uint8_t reservado[3];
  Ajustes ajustes;
  Ranura pin;
  Ranura recuperacion;
  uint8_t nonce[TAM_NONCE];
  uint32_t largo;  // del cuerpo cifrado; detrás va su tag
};

// /chip.bin: la clave ADMIN del chip, cifrada con la clave maestra. Va aparte
// para no cambiar el formato de la bóveda. Lo que va antes de nonce se autentica.
struct __attribute__((packed)) FicheroChip {
  char magia[4];
  uint8_t version;
  uint8_t serie[9];  // del chip al que pertenece
  uint8_t nonce[TAM_NONCE];
  uint8_t clave[TAM_ATECC];
  uint8_t tag[TAM_TAG];
};
static const size_t AAD_CHIP = offsetof(FicheroChip, nonce);

static EstadoBoveda estado = BOVEDA_SIN_FS;
static Cabecera cabecera;
static uint8_t claveMaestra[TAM_CLAVE];
static uint8_t cuerpo[MAX_SERIALIZADO];
// En RAM con la bóveda abierta, y tras "Borrar todo" hasta crear la nueva
static uint8_t claveAdmin[TAM_ATECC];
static bool hayClaveAdmin = false;

static const Ajustes AJUSTES_INICIALES = {DISTRIBUCION_INICIAL, 1, 1, 2, 1, {0, 0, 0}};

// ---- Ranuras ----

static void envolver(Ranura &r, const uint8_t kek[TAM_CLAVE]) {
  criptoAleatorio(r.nonce, sizeof(r.nonce));
  memcpy(r.clave, claveMaestra, TAM_CLAVE);
  criptoCifrar(kek, r.nonce, r.clave, TAM_CLAVE, &r, AAD_RANURA, r.tag);
}

static bool desenvolver(const Ranura &r, const uint8_t kek[TAM_CLAVE], uint8_t clave[TAM_CLAVE]) {
  memcpy(clave, r.clave, TAM_CLAVE);
  return criptoDescifrar(kek, r.nonce, clave, TAM_CLAVE, &r, AAD_RANURA, r.tag);
}

static void kekDePin(const Ranura &r, const char *pin, uint8_t kek[TAM_CLAVE]) {
  criptoPbkdf2(pin, strlen(pin), r.sal, sizeof(r.sal), r.iteraciones, kek);
}

// ---- ATECC608 ----

static bool chipListo() {
  return ateccEstado() == ATECC_LISTO;
}

// Lee la cabecera en claro de /chip.bin
static bool leerFicheroChip(FicheroChip &fc) {
  File f = LittleFS.open(FICHERO_CHIP, "r");
  if (!f) return false;
  bool ok = (size_t)f.read((uint8_t *)&fc, sizeof(fc)) == sizeof(fc) && !memcmp(fc.magia, MAGIA_CHIP, 4) &&
            fc.version == 1;
  f.close();
  return ok;
}

// Si /chip.bin es de otro chip, el PIN no se prueba: gastaría intentos de un
// chip que no da la clave buena
static bool chipEsElNuestro() {
  FicheroChip fc;
  return !leerFicheroChip(fc) || !memcmp(fc.serie, ateccSerie(), sizeof(fc.serie));
}

// Con la clave maestra en RAM
static bool cargarClaveAdmin() {
  if (hayClaveAdmin) return true;
  FicheroChip fc;
  bool ok = leerFicheroChip(fc) && !memcmp(fc.serie, ateccSerie(), sizeof(fc.serie)) &&
            criptoDescifrar(claveMaestra, fc.nonce, fc.clave, TAM_ATECC, &fc, AAD_CHIP, fc.tag);
  if (ok) {
    memcpy(claveAdmin, fc.clave, TAM_ATECC);
    hayClaveAdmin = true;
  }
  criptoBorrar(&fc, sizeof(fc));
  return ok;
}

static bool guardarClaveAdmin() {
  FicheroChip fc;
  memset(&fc, 0, sizeof(fc));
  memcpy(fc.magia, MAGIA_CHIP, 4);
  fc.version = 1;
  memcpy(fc.serie, ateccSerie(), sizeof(fc.serie));
  criptoAleatorio(fc.nonce, sizeof(fc.nonce));
  memcpy(fc.clave, claveAdmin, TAM_ATECC);
  criptoCifrar(claveMaestra, fc.nonce, fc.clave, TAM_ATECC, &fc, AAD_CHIP, fc.tag);
  File f = LittleFS.open(TEMPORAL_CHIP, "w");
  bool ok = f && f.write((const uint8_t *)&fc, sizeof(fc)) == sizeof(fc);
  if (f) f.close();
  criptoBorrar(&fc, sizeof(fc));
  return ok && LittleFS.rename(TEMPORAL_CHIP, FICHERO_CHIP);
}

// Con la bóveda abierta: deja el chip provisionado y con su clave ADMIN en
// /chip.bin. Un chip virgen se provisiona aquí (IRREVERSIBLE).
static bool chipUsable() {
  switch (ateccEstado()) {
    case ATECC_LISTO:
      if (!cargarClaveAdmin()) {
        Serial.println("Chip: provisionado pero sin su clave ADMIN (otra bóveda o corte a medias): PIN sin chip");
        return false;
      }
      // Tras "Borrar todo" la clave está en RAM y falta el fichero
      return LittleFS.exists(FICHERO_CHIP) || guardarClaveAdmin();
    case ATECC_VIRGEN:
    case ATECC_A_MEDIAS:
      // La clave se guarda antes de provisionar: si se corta, se reutiliza
      if (!cargarClaveAdmin()) {
        criptoAleatorio(claveAdmin, TAM_ATECC);
        hayClaveAdmin = true;
      }
      if (!guardarClaveAdmin() || !ateccProvisionar(claveAdmin)) {
        Serial.println("Chip: error al provisionar");
        return false;
      }
      Serial.println("Chip: provisionado");
      return true;
    default:
      return false;
  }
}

// Gasta un intento del chip: KEK = HKDF(MAC del chip sobre el PBKDF2 del PIN)
static ResultadoApertura kekDePinChip(const Ranura &r, const char *pin, uint8_t kek[TAM_CLAVE]) {
  if (!chipListo() || !chipEsElNuestro()) return APERTURA_SIN_CHIP;
  uint8_t reto[TAM_CLAVE], mac[TAM_ATECC];
  kekDePin(r, pin, reto);
  bool ok = ateccIntento(reto, mac);
  criptoBorrar(reto, sizeof(reto));
  if (!ok) {
    uint32_t contador, limite;
    if (ateccContador(contador, limite) && contador >= limite) return APERTURA_SIN_INTENTOS;
    return APERTURA_SIN_CHIP;
  }
  criptoHkdf(mac, sizeof(mac), r.sal, sizeof(r.sal), INFO_PIN_CHIP, kek);
  criptoBorrar(mac, sizeof(mac));
  return APERTURA_OK;
}

// Tras un desbloqueo bueno: que vuelvan a quedar al menos INTENTOS_CHIP
static void reponerIntentos() {
  if (!chipListo() || !cargarClaveAdmin() || !ateccSubirLimite(claveAdmin, INTENTOS_CHIP))
    Serial.println("Chip: no se pueden reponer los intentos");
}

// Solo cambia la ranura si todo sale bien
static bool ranuraPin(const char *pin, uint32_t iteraciones, bool conChip) {
  Ranura r;
  memset(&r, 0, sizeof(r));
  r.tipo = conChip ? RANURA_PIN_CHIP : RANURA_PIN;
  r.iteraciones = iteraciones;
  criptoAleatorio(r.sal, sizeof(r.sal));
  uint8_t kek[TAM_CLAVE];
  if (conChip) {
    if (kekDePinChip(r, pin, kek) != APERTURA_OK) return false;
  } else {
    kekDePin(r, pin, kek);
  }
  envolver(r, kek);
  criptoBorrar(kek, sizeof(kek));
  cabecera.pin = r;
  if (conChip) reponerIntentos();
  return true;
}

// PIN nuevo: con el chip si se puede; si el PIN ya iba con el chip, sin él no
static bool ponerPin(const char *pin, uint32_t iteraciones) {
  if (chipUsable() && ranuraPin(pin, iteraciones, true)) return true;
  if (cabecera.pin.tipo == RANURA_PIN_CHIP) return false;
  return ranuraPin(pin, iteraciones, false);
}

// ---- Clave de recuperación en base32 (bóvedas anteriores; sin 0, 1, 8 ni 9) ----

static const char BASE32[] = "ABCDEFGHIJKLMNOPQRSTUVWXYZ234567";

// Admite minúsculas, guiones y espacios, y los parecidos 0/O, 1/I, 8/B
static bool deBase32(const char *texto, uint8_t bytes[BYTES_RECUPERACION]) {
  uint32_t acumulado = 0;
  int bits = 0;
  size_t n = 0;
  for (const char *p = texto; *p; p++) {
    char c = toupper(*p);
    if (c == '-' || c == ' ') continue;
    if (c == '0') c = 'O';
    if (c == '1') c = 'I';
    if (c == '8') c = 'B';
    const char *donde = strchr(BASE32, c);
    if (!donde || !c) return false;
    acumulado = (acumulado << 5) | (donde - BASE32);
    bits += 5;
    if (bits >= 8) {
      if (n >= BYTES_RECUPERACION) return false;
      bytes[n++] = (acumulado >> (bits - 8)) & 0xFF;
      bits -= 8;
    }
  }
  return n == BYTES_RECUPERACION;
}

static void kekDeRecuperacion(const Ranura &r, const uint8_t bytes[BYTES_RECUPERACION], uint8_t kek[TAM_CLAVE]) {
  criptoHkdf(bytes, BYTES_RECUPERACION, r.sal, sizeof(r.sal), INFO_RECUPERACION, kek);
}

// ---- Clave de recuperación en palabras ----

// Índice uniforme en [0, NUM_DICE): descarta lo que no llena un ciclo entero
static int palabraAlAzar() {
  const uint32_t tope = UINT32_MAX - UINT32_MAX % NUM_DICE;
  uint32_t r;
  do criptoAleatorio(&r, sizeof(r));
  while (r >= tope);
  return r % NUM_DICE;
}

static void generarPalabras(char texto[TAM_TEXTO_RECUPERACION]) {
  texto[0] = '\0';
  for (int i = 0; i < PALABRAS_RECUPERACION; i++) {
    if (i) strcat(texto, " ");
    strcat(texto, DICE_LISTA[palabraAlAzar()]);
  }
}

// Minúsculas y un solo espacio entre palabras, para que la KEK no dependa de cómo se escriba
static bool normalizarPalabras(const char *entrada, char salida[TAM_TEXTO_RECUPERACION]) {
  size_t n = 0;
  bool espacio = false;
  for (const char *p = entrada; *p; p++) {
    if (*p == ' ' || *p == '-') {
      espacio = n > 0;
      continue;
    }
    if (n + (espacio ? 2 : 1) >= TAM_TEXTO_RECUPERACION) return false;
    if (espacio) salida[n++] = ' ';
    espacio = false;
    salida[n++] = tolower(*p);
  }
  salida[n] = '\0';
  return n > 0;
}

static void kekDePalabras(const Ranura &r, const char *texto, uint8_t kek[TAM_CLAVE]) {
  criptoPbkdf2(texto, strlen(texto), r.sal, sizeof(r.sal), r.iteraciones, kek);
}

// ---- Fichero ----

static bool leerFichero(size_t &largo) {
  File f = LittleFS.open(FICHERO, "r");
  if (!f) return false;
  bool ok = (size_t)f.read((uint8_t *)&cabecera, sizeof(cabecera)) == sizeof(cabecera) &&
            !memcmp(cabecera.magia, MAGIA, sizeof(MAGIA)) && cabecera.version == VERSION;
  largo = cabecera.largo;
  f.close();
  return ok;
}

// Abre el almacén con la clave maestra ya en claveMaestra
static ResultadoApertura abrirCuerpo() {
  File f = LittleFS.open(FICHERO, "r");
  if (!f) return APERTURA_DANADA;
  uint8_t tag[TAM_TAG];
  size_t largo = cabecera.largo;
  bool leido = largo <= sizeof(cuerpo) && f.seek(sizeof(Cabecera)) &&
               (size_t)f.read(cuerpo, largo) == largo && (size_t)f.read(tag, TAM_TAG) == TAM_TAG;
  f.close();
  bool ok = leido && criptoDescifrar(claveMaestra, cabecera.nonce, cuerpo, largo, &cabecera, sizeof(cabecera), tag) &&
            almacenDeserializar(cuerpo, largo);
  criptoBorrar(cuerpo, sizeof(cuerpo));
  if (!ok) {
    criptoBorrar(claveMaestra, sizeof(claveMaestra));
    return APERTURA_DANADA;
  }
  estado = BOVEDA_ABIERTA;
  return APERTURA_OK;
}

EstadoBoveda bovedaIniciar() {
  cabecera.ajustes = AJUSTES_INICIALES;
  static const char *const NOMBRES[] = {"no está", "virgen", "a medias", "listo", "con otra configuración"};
  Serial.printf("Chip ATECC608: %s\n", NOMBRES[ateccIniciar()]);
  if (!LittleFS.begin()) return estado = BOVEDA_SIN_FS;
  size_t largo;
  if (!LittleFS.exists(FICHERO)) return estado = BOVEDA_NUEVA;
  if (!leerFichero(largo)) {
    cabecera.ajustes = AJUSTES_INICIALES;
    // Un fichero que no es nuestro o de otra versión: no se toca
    Serial.println("Bóveda: fichero no reconocido");
  }
  return estado = BOVEDA_CERRADA;
}

EstadoBoveda bovedaEstado() {
  return estado;
}

Ajustes &bovedaAjustes() {
  return cabecera.ajustes;
}

bool bovedaGuardar() {
  if (estado != BOVEDA_ABIERTA) return false;
  size_t largo = almacenSerializar(cuerpo, sizeof(cuerpo));
  if (!largo) return false;

  memcpy(cabecera.magia, MAGIA, sizeof(MAGIA));
  cabecera.version = VERSION;
  cabecera.largo = largo;
  criptoAleatorio(cabecera.nonce, sizeof(cabecera.nonce));
  uint8_t tag[TAM_TAG];
  criptoCifrar(claveMaestra, cabecera.nonce, cuerpo, largo, &cabecera, sizeof(cabecera), tag);

  File f = LittleFS.open(TEMPORAL, "w");
  bool ok = f && f.write((const uint8_t *)&cabecera, sizeof(cabecera)) == sizeof(cabecera) &&
            f.write(cuerpo, largo) == largo && f.write(tag, TAM_TAG) == TAM_TAG;
  if (f) f.close();
  criptoBorrar(cuerpo, sizeof(cuerpo));
  if (ok) ok = LittleFS.rename(TEMPORAL, FICHERO);
  if (!ok) Serial.println("Bóveda: error al guardar");
  return ok;
}

bool bovedaCrear(const char *pin, char recuperacion[TAM_TEXTO_RECUPERACION]) {
  if (estado == BOVEDA_SIN_FS) return false;
  Ajustes ajustes = cabecera.ajustes;
  memset(&cabecera, 0, sizeof(cabecera));
  cabecera.ajustes = ajustes;
  almacenVaciar();
  criptoAleatorio(claveMaestra, sizeof(claveMaestra));
  estado = BOVEDA_ABIERTA;  // ponerPin necesita la clave maestra para /chip.bin

  // Con chip, la clave maestra nunca queda en la flash envuelta solo con el PIN
  uint32_t iteraciones = criptoCalibrarPbkdf2(OBJETIVO_KDF_MS);
  ponerPin(pin, iteraciones);

  generarPalabras(recuperacion);
  Ranura &r = cabecera.recuperacion;
  r.tipo = RANURA_PALABRAS;
  r.iteraciones = iteraciones;
  criptoAleatorio(r.sal, sizeof(r.sal));
  uint8_t kek[TAM_CLAVE];
  kekDePalabras(r, recuperacion, kek);
  envolver(r, kek);
  criptoBorrar(kek, sizeof(kek));

  if (bovedaGuardar()) return true;
  bovedaCerrar();
  estado = BOVEDA_NUEVA;
  return false;
}

ResultadoApertura bovedaAbrirConPin(const char *pin) {
  size_t largo;
  if (!leerFichero(largo)) return APERTURA_DANADA;
  uint8_t kek[TAM_CLAVE];
  bool conChip = cabecera.pin.tipo == RANURA_PIN_CHIP;
  if (conChip) {
    ResultadoApertura r = kekDePinChip(cabecera.pin, pin, kek);
    if (r != APERTURA_OK) return r;
  } else if (cabecera.pin.tipo == RANURA_PIN) {
    kekDePin(cabecera.pin, pin, kek);
  } else {
    return APERTURA_DANADA;
  }
  bool ok = desenvolver(cabecera.pin, kek, claveMaestra);
  criptoBorrar(kek, sizeof(kek));
  if (!ok) return APERTURA_CLAVE_MAL;
  ResultadoApertura r = abrirCuerpo();
  if (r != APERTURA_OK) return r;
  if (conChip) {
    reponerIntentos();
  } else if (chipUsable() && ranuraPin(pin, cabecera.pin.iteraciones, true)) {
    // Bóveda de antes del chip: el PIN pasa a ir con él. Pueden quedar en la
    // flash restos de la ranura vieja (sin chip): para contraseñas reales,
    // mejor crear la bóveda de cero con el chip puesto.
    Serial.println("Chip: PIN ligado al chip");
    bovedaGuardar();
  }
  return APERTURA_OK;
}

ResultadoApertura bovedaAbrirConRecuperacion(const char *clave) {
  size_t largo;
  if (!leerFichero(largo)) return APERTURA_DANADA;
  const Ranura &r = cabecera.recuperacion;
  uint8_t kek[TAM_CLAVE];
  if (r.tipo == RANURA_PALABRAS) {
    char texto[TAM_TEXTO_RECUPERACION];
    bool valido = normalizarPalabras(clave, texto);
    if (valido) kekDePalabras(r, texto, kek);
    criptoBorrar(texto, sizeof(texto));
    if (!valido) return APERTURA_CLAVE_MAL;
  } else if (r.tipo == RANURA_RECUPERACION) {
    uint8_t bytes[BYTES_RECUPERACION];
    if (!deBase32(clave, bytes)) return APERTURA_CLAVE_MAL;
    kekDeRecuperacion(r, bytes, kek);
    criptoBorrar(bytes, sizeof(bytes));
  } else {
    return APERTURA_DANADA;
  }
  bool ok = desenvolver(r, kek, claveMaestra);
  criptoBorrar(kek, sizeof(kek));
  if (!ok) return APERTURA_CLAVE_MAL;
  ResultadoApertura resultado = abrirCuerpo();
  // La recuperación no depende del chip: devuelve los intentos aunque se hayan agotado
  if (resultado == APERTURA_OK && cabecera.pin.tipo == RANURA_PIN_CHIP) reponerIntentos();
  return resultado;
}

bool bovedaComprobarPin(const char *pin) {
  if (estado != BOVEDA_ABIERTA) return false;
  uint8_t kek[TAM_CLAVE], clave[TAM_CLAVE];
  bool conChip = cabecera.pin.tipo == RANURA_PIN_CHIP;
  if (conChip) {
    if (kekDePinChip(cabecera.pin, pin, kek) != APERTURA_OK) return false;
  } else {
    kekDePin(cabecera.pin, pin, kek);
  }
  bool ok = desenvolver(cabecera.pin, kek, clave) && criptoIguales(clave, claveMaestra, TAM_CLAVE);
  if (ok && conChip) reponerIntentos();
  criptoBorrar(kek, sizeof(kek));
  criptoBorrar(clave, sizeof(clave));
  return ok;
}

bool bovedaCambiarPin(const char *pin) {
  if (estado != BOVEDA_ABIERTA) return false;
  if (!ponerPin(pin, cabecera.pin.iteraciones ? cabecera.pin.iteraciones : criptoCalibrarPbkdf2(OBJETIVO_KDF_MS)))
    return false;
  return bovedaGuardar();
}

bool bovedaRecuperacionConPalabras() {
  return cabecera.recuperacion.tipo != RANURA_RECUPERACION;
}

bool bovedaIntentosChip(uint32_t &quedan) {
  uint32_t contador, limite;
  if (cabecera.pin.tipo != RANURA_PIN_CHIP || !chipListo() || !ateccContador(contador, limite)) return false;
  quedan = limite > contador ? limite - contador : 0;
  return true;
}

void bovedaCerrar() {
  criptoBorrar(claveMaestra, sizeof(claveMaestra));
  almacenVaciar();
  if (estado == BOVEDA_ABIERTA) {
    criptoBorrar(claveAdmin, sizeof(claveAdmin));
    hayClaveAdmin = false;
    estado = BOVEDA_CERRADA;
  }
}

// La clave ADMIN del chip se queda en RAM para la bóveda nueva: sin ella, el
// chip ya provisionado no se podría usar (si se reinicia antes, se pierde)
void bovedaBorrarTodo() {
  uint8_t admin[TAM_ATECC];
  bool conservar = estado == BOVEDA_ABIERTA && chipListo() && cargarClaveAdmin();
  if (conservar) memcpy(admin, claveAdmin, TAM_ATECC);
  bovedaCerrar();
  if (conservar) {
    memcpy(claveAdmin, admin, TAM_ATECC);
    hayClaveAdmin = true;
    criptoBorrar(admin, sizeof(admin));
  }
  LittleFS.remove(FICHERO);
  LittleFS.remove(TEMPORAL);
  LittleFS.remove(FICHERO_CHIP);
  LittleFS.remove(TEMPORAL_CHIP);
  Ajustes ajustes = cabecera.ajustes;
  memset(&cabecera, 0, sizeof(cabecera));
  cabecera.ajustes = ajustes;
  estado = BOVEDA_NUEVA;
}
