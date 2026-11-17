#include <functional>
#include <LittleFS.h>
#include "tusb.h"

#include "config.h"
#include "joystick.h"
#include "pantalla.h"
#include "teclado.h"
#include "almacen.h"
#include "generador.h"
#include "guion.h"
#include "utf8.h"
#include "led.h"
#include "cripto.h"
#include "boveda.h"
#include "admin.h"
#include "ui.h"
#include "pomodoro.h"
#include "mantener.h"
#include "notificacion.h"
#include "zumbador.h"
#include "azar.h"
#include "dice_lista.h"   // lista de palabras (castellano): clave de recuperación

using AlAceptarTexto = std::function<void(const char *)>;
using Accion = std::function<void()>;

class Pantalla {
 public:
  virtual void entrar() {}      // al ponerla encima de la pila
  virtual void volver() {}      // al quitar la que tenía encima
  virtual void tecla(Tecla t) = 0;
  virtual void dibujar() = 0;
  virtual void tick() {}
  virtual bool apagable() { return true; }  // el OLED puede apagarse por inactividad
  virtual bool atenuable() { return false; } // en vez de apagarse, baja el brillo
  // Si salta el bloqueo automático con esta pantalla arriba, la que sigue a la
  // vista encima de la de bloqueo (una que no necesite la bóveda)
  virtual Pantalla *trasBloqueo() { return nullptr; }
};

// ---- Pila de pantallas, avisos y bloqueo ----

static const int MAX_PILA = 10;
static Pantalla *pila[MAX_PILA];
static int numPila = 0;
static bool sucio = true;

static char textoAviso[32];
static bool avisoActivo = false;
static uint32_t avisoHastaMs = 0;

static uint32_t ultimaTeclaMs = 0;
static bool atenuada = false;  // OLED al mínimo por inactividad (pantalla del pomodoro)
static uint8_t fallosPin = 0;
static uint32_t esperaPinHastaMs = 0;
static bool aceptarTrasPin = false;    // pregunta del PC aceptada con el Ardutype bloqueado
static uint32_t notiDespertoMs = 0;    // una notificación encendió el OLED (no cuenta para el bloqueo)

static Pantalla *cima() {
  return pila[numPila - 1];
}

static void uiPush(Pantalla *p) {
  if (numPila >= MAX_PILA) return;
  pila[numPila++] = p;
  p->entrar();
  sucio = true;
}

static void uiPop() {
  if (numPila > 1) numPila--;
  cima()->volver();
  sucio = true;
}

// Desapila hasta dejar p arriba (si está en la pila)
static void uiVolverA(Pantalla *p) {
  while (numPila > 1 && cima() != p) numPila--;
  cima()->volver();
  sucio = true;
}

static void uiRaiz(Pantalla *p) {
  numPila = 0;
  uiPush(p);
}

static void aviso(const char *texto) {
  strncpy(textoAviso, texto, sizeof(textoAviso) - 1);
  textoAviso[sizeof(textoAviso) - 1] = '\0';
  avisoActivo = true;
  avisoHastaMs = millis() + AVISO_MS;
  sucio = true;
}

// Aviso de error: además, dos destellos rojos
static void fallo(const char *texto) {
  aviso(texto);
  ledError();
}

static void dibujarAviso() {
  oled.drawFrame(0, 0, 128, 32);
  oled.setFont(FUENTE_NORMAL);
  char texto[CARACTERES_FILA * 3 + 1];
  utf8Tramo(textoAviso, 0, CARACTERES_FILA, texto, sizeof(texto));
  pantallaCentrado(20, texto);
}

static void bloquear();

// ---- Ajustes (en RAM hasta la fase 4) ----

static const uint16_t RETARDOS_MS[] = {5, 10, 20, 40};
static const uint8_t BLOQUEOS_MIN[] = {5, 15, 30, 60};
static const uint8_t CONTRASTES[] = {8, 80, 255};
static const uint8_t BRILLOS_LED[] = {2, 8, 22};
static const char *const NOMBRES_BRILLO[] = {"bajo", "medio", "alto"};

#define NUM(a) (int)(sizeof(a) / sizeof(a[0]))

// Los ajustes viven en la cabecera de la bóveda (índices de estas tablas)
static Ajustes &aj() {
  return bovedaAjustes();
}

static void aplicarAjustes() {
  Ajustes &a = aj();
  if (a.distribucion >= NUM_DISTRIBUCIONES) a.distribucion = 0;
  if (a.retardo >= NUM(RETARDOS_MS)) a.retardo = 1;
  if (a.bloqueo >= NUM(BLOQUEOS_MIN)) a.bloqueo = 1;
  if (a.brillo >= NUM(CONTRASTES)) a.brillo = 2;
  tecladoDistribucion((Distribucion)a.distribucion);
  tecladoRetardo(RETARDOS_MS[a.retardo]);
  pantallaContraste(CONTRASTES[a.brillo]);
  ledBrillo(BRILLOS_LED[a.brillo]);
  ledActivo(a.led);
}

// Tras cualquier cambio: cifra y escribe la bóveda
static void guardar() {
  if (!bovedaGuardar()) fallo("Error al guardar");
}

// Para las operaciones lentas (PBKDF2): se pinta antes de bloquear el bucle
static void pintarAhora(const char *titulo, const char *texto) {
  oled.clearBuffer();
  pantallaCabecera(titulo);
  oled.setFont(FUENTE_NORMAL);
  pantallaCentrado(24, texto);
  oled.sendBuffer();
}

// ---- Lista: base de todos los menús ----

class Lista : public Pantalla {
 protected:
  int sel = 0;
  int primera = 0;

  virtual void titulo(char *buf, size_t tam) = 0;
  virtual int numItems() = 0;
  virtual void item(int i, char *buf, size_t tam) = 0;
  virtual void elegir(int i) = 0;
  virtual void atras() { uiPop(); }

  void ajustar() {
    int n = numItems();
    if (sel >= n) sel = n - 1;
    if (sel < 0) sel = 0;
    if (sel < primera) primera = sel;
    if (sel > primera + 1) primera = sel - 1;
    if (primera > n - 2) primera = n - 2;
    if (primera < 0) primera = 0;
  }

 public:
  void seleccionar(int i) {
    sel = i;
    ajustar();
    sucio = true;
  }

  void entrar() override {
    sel = primera = 0;
  }

  void volver() override {
    ajustar();
  }

  void tecla(Tecla t) override {
    int n = numItems();
    switch (t) {
      case ARRIBA: seleccionar((sel - 1 + n) % n); break;
      case ABAJO: seleccionar((sel + 1) % n); break;
      case DERECHA:
      case CENTRO: elegir(sel); break;
      case IZQUIERDA: atras(); break;
      case CENTRO_LARGO: bloquear(); break;
      default: break;
    }
  }

  void dibujar() override {
    ajustar();
    char texto[LARGO_CAMPO + 32];
    char posicion[12];
    titulo(texto, sizeof(texto));
    snprintf(posicion, sizeof(posicion), "%d/%d", sel + 1, numItems());
    pantallaCabecera(texto, posicion);
    for (int fila = 0; fila < 2 && primera + fila < numItems(); fila++) {
      item(primera + fila, texto, sizeof(texto));
      pantallaFila(fila, texto, primera + fila == sel);
    }
  }
};

// ---- Confirmación ("No" primero, para que un centro por inercia no borre) ----

class Confirmar : public Lista {
  const char *pregunta = "";
  const char *textoSi = "";
  Accion alConfirmar;

 public:
  // Sin bloqueo por pulsación larga: en «¿La has apuntado?» perdería la clave
  void tecla(Tecla t) override {
    if (t != CENTRO_LARGO) Lista::tecla(t);
  }

 protected:
  void titulo(char *buf, size_t tam) override { snprintf(buf, tam, "%s", pregunta); }
  int numItems() override { return 2; }
  void item(int i, char *buf, size_t tam) override { snprintf(buf, tam, "%s", i ? textoSi : "No"); }
  void elegir(int i) override {
    if (!i) return uiPop();
    Accion accion = alConfirmar;  // la acción puede reutilizar esta pantalla
    accion();
  }

 public:
  void abrir(const char *p, const char *si, Accion accion) {
    pregunta = p;
    textoSi = si;
    alConfirmar = accion;
    uiPush(this);
  }
};
static Confirmar confirmar;

// ---- Rueda de caracteres ----
//
// Siempre se edita el último carácter: arriba/abajo lo cambian dentro de su
// grupo, derecha añade otro igual (los caracteres cercanos salen rápido),
// izquierda lo borra, centro cambia de grupo y centro mantenido acepta.

static const char *const GRUPOS_UTF8[] = {
  "abcdefghijklmnopqrstuvwxyz",
  "ABCDEFGHIJKLMNOPQRSTUVWXYZ",
  "0123456789",
  " .-_@!?#$%&*+/=:;,()[]{}<>\\|^",
  // Solo para la clave de recuperación: el alfabeto base32
  "ABCDEFGHIJKLMNOPQRSTUVWXYZ",
  "234567",
};
static const char *const NOMBRES_GRUPO[] = {"abc", "ABC", "123", "#@!", "ABC", "2-7"};
static const int NUM_GRUPOS = NUM(GRUPOS_UTF8);
static const int MAX_GRUPO = 48;
static uint16_t grupos[NUM_GRUPOS][MAX_GRUPO];
static int largoGrupo[NUM_GRUPOS];

// Juegos de grupos que puede usar la rueda: {primer grupo, cuántos}
struct Juego {
  int primero, num;
};
static const Juego TEXTO_LIBRE = {0, 4};
static const Juego CLAVE_RECUPERACION = {4, 2};

static void prepararGrupos() {
  for (int g = 0; g < NUM_GRUPOS; g++) {
    const char *p = GRUPOS_UTF8[g];
    largoGrupo[g] = 0;
    while (*p && largoGrupo[g] < MAX_GRUPO) grupos[g][largoGrupo[g]++] = utf8Siguiente(p);
  }
}

// Grupo y posición de un carácter dentro del juego; el primero si no está
static int buscarEnGrupos(const Juego &j, uint16_t c, int &posicion) {
  for (int g = j.primero; g < j.primero + j.num; g++) {
    for (int i = 0; i < largoGrupo[g]; i++) {
      if (grupos[g][i] == c) {
        posicion = i;
        return g;
      }
    }
  }
  posicion = 0;
  return j.primero;
}

class Rueda : public Pantalla {
  static const int MAX = LARGO_CAMPO;
  uint16_t txt[MAX];
  int largo = 0;
  size_t maxBytes = 0;
  bool vacioPermitido = false;
  const char *nombre = "";
  Juego juego = TEXTO_LIBRE;
  AlAceptarTexto alAceptar;

  int buscar(uint16_t c, int &pos) { return buscarEnGrupos(juego, c, pos); }
  uint16_t primero() { return grupos[juego.primero][0]; }

  size_t bytes() {
    size_t n = 0;
    for (int i = 0; i < largo; i++) n += utf8Bytes(txt[i]);
    return n;
  }

  bool cabe(uint16_t c) {
    return largo < MAX && bytes() + utf8Bytes(c) < maxBytes;
  }

  void anadir(uint16_t c) {
    if (cabe(c)) txt[largo++] = c;
    else fallo("No caben más");
  }

  void cambiar(int paso) {
    int pos;
    int g = buscar(txt[largo - 1], pos);
    uint16_t nuevo = grupos[g][(pos + paso + largoGrupo[g]) % largoGrupo[g]];
    // Un carácter de más bytes podría no caber en el último hueco
    if (bytes() - utf8Bytes(txt[largo - 1]) + utf8Bytes(nuevo) < maxBytes) txt[largo - 1] = nuevo;
  }

  void aceptar() {
    if (!largo && !vacioPermitido) return fallo("No puede quedar vacío");
    char buf[LARGO_CAMPO];
    size_t n = 0;
    for (int i = 0; i < largo; i++) n += utf8Codificar(txt[i], buf + n);
    buf[n] = '\0';
    AlAceptarTexto alTerminar = alAceptar;  // puede reconfigurar esta rueda
    alTerminar(buf);
  }

 public:
  // tam = tamaño del campo de destino, con el 0 final
  void configurar(const char *titulo, const char *inicial, size_t tam, bool vacio, AlAceptarTexto cb,
                  const Juego &j = TEXTO_LIBRE) {
    nombre = titulo;
    juego = j;
    maxBytes = tam;
    vacioPermitido = vacio;
    alAceptar = cb;
    largo = 0;
    const char *p = inicial;
    while (*p && largo < MAX) txt[largo++] = utf8Siguiente(p);
    sucio = true;
  }

  void abrir(const char *titulo, const char *inicial, size_t tam, bool vacio, AlAceptarTexto cb,
             const Juego &j = TEXTO_LIBRE) {
    configurar(titulo, inicial, tam, vacio, cb, j);
    uiPush(this);
  }

  void tecla(Tecla t) override {
    switch (t) {
      case ARRIBA:
      case ABAJO:
        if (!largo) anadir(primero());
        else cambiar(t == ARRIBA ? 1 : -1);
        break;
      case DERECHA:
        anadir(largo ? txt[largo - 1] : primero());
        break;
      case IZQUIERDA:
        if (largo) largo--;
        else uiPop();  // vacía: cancelar
        break;
      case CENTRO: {
        if (!largo) {
          anadir(primero());
          break;
        }
        int pos;
        int g = buscar(txt[largo - 1], pos);
        uint16_t nuevo = grupos[juego.primero + (g - juego.primero + 1) % juego.num][0];
        txt[largo - 1] = nuevo;
        if (bytes() >= maxBytes) largo--;
        break;
      }
      case CENTRO_LARGO:
        aceptar();
        break;
      default:
        break;
    }
  }

  void dibujar() override {
    int pos = 0;
    int g = largo ? buscar(txt[largo - 1], pos) : juego.primero;
    pantallaCabecera(nombre, NOMBRES_GRUPO[g]);
    oled.setFont(FUENTE_NORMAL);

    if (!largo) {
      oled.drawUTF8(2, 18, "(vacío)");
      oled.setFont(FUENTE_PEQUENA);
      oled.drawUTF8(0, 30, "arriba empieza, izq sale");
      return;
    }

    // Texto: los últimos 20 caracteres, el que se edita en vídeo inverso
    int desde = largo > CARACTERES_FILA ? largo - CARACTERES_FILA : 0;
    char c[4];
    for (int i = desde; i < largo; i++) {
      int x = 2 + (i - desde) * 6;
      c[utf8Codificar(txt[i], c)] = '\0';
      if (i == largo - 1) {
        oled.drawBox(x - 1, 9, 8, 11);
        oled.setDrawColor(0);
      }
      oled.drawUTF8(x, 18, c);
      oled.setDrawColor(1);
    }

    // Vecinos en el grupo: lo que sale con arriba (derecha) y abajo (izquierda)
    for (int k = -4; k <= 4; k++) {
      uint16_t vecino = grupos[g][(pos + k + largoGrupo[g] * 2) % largoGrupo[g]];
      c[utf8Codificar(vecino, c)] = '\0';
      int x = 61 + k * 13;
      if (vecino == ' ') strcpy(c, "_");
      if (!k) oled.drawFrame(x - 3, 21, 12, 11);
      oled.drawUTF8(x, 30, c);
    }
  }
};
static Rueda rueda;

// ---- Entrada por palabras (Diceware en castellano) ----
//
// Para la clave de recuperación. En vez de teclear
// caracteres arbitrarios, se escriben palabras: con la rueda (solo a-z)
// tecleas letras y se filtra la lista por prefijo; arriba/abajo saltan las
// letras sin palabra, así que solo se ven prefijos válidos. La lista
// (dice_lista.h) es castellano sin tildes ni ñ, con UNA palabra por prefijo de
// 3 letras: tecleas 3 letras y la palabra queda determinada; centro la acepta.
// Con un número fijo de palabras (recuperación) se entrega sola al completarse;
// si no, con centro largo.

class RuedaPalabras : public Pantalla {
  static const int MAX_PRE = 11;   // la palabra más larga tiene 10 letras
  char pre[MAX_PRE + 1];
  int largoPre = 0;
  char frase[LARGO_CAMPO];
  int numPal = 0;
  const char *titulo = "";
  int total = 0;  // 0 = las que se quieran
  AlAceptarTexto alAceptar;

  void entregar() {
    AlAceptarTexto cb = alAceptar;
    uiPop();
    cb(frase);
    criptoBorrar(frase, sizeof(frase));
  }

  // Cuenta coincidencias por prefijo (la lista está ordenada: bloque contiguo).
  int coincidencias(int &primero) {
    primero = -1;
    if (largoPre == 0) return 0;
    int n = 0;
    for (int i = 0; i < NUM_DICE; i++) {
      if (strncmp(DICE_LISTA[i], pre, largoPre) == 0) {
        if (primero < 0) primero = i;
        n++;
      } else if (primero >= 0) {
        break;
      }
    }
    return n;
  }

  // Deja la última letra en una que tenga al menos una palabra, avanzando en
  // el sentido dir (+1/-1). Así la rueda nunca se queda en "(ninguna)".
  void ajustarAValida(int dir) {
    int p;
    for (int i = 0; i < 26; i++) {
      if (coincidencias(p) > 0) return;
      char &c = pre[largoPre - 1];
      c = 'a' + (c - 'a' + (dir > 0 ? 1 : 25)) % 26;
    }
  }

  void aceptarPalabra() {
    int primero;
    int n = coincidencias(primero);
    if (largoPre == 0 || n == 0) return fallo("Sin palabra");
    const char *pal = DICE_LISTA[primero];   // la palabra del prefijo
    size_t len = strlen(frase);
    size_t add = strlen(pal) + (numPal ? 1 : 0);
    if (len + add + 1 >= sizeof(frase)) return fallo("Frase llena");
    if (numPal) frase[len++] = ' ';
    strcpy(frase + len, pal);
    numPal++;
    largoPre = 0;
    pre[0] = '\0';
    sucio = true;
    if (numPal == total) entregar();
  }

 public:
  void abrir(const char *t, int palabras, AlAceptarTexto cb) {
    titulo = t;
    total = palabras;
    alAceptar = cb;
    largoPre = 0; pre[0] = '\0';
    criptoBorrar(frase, sizeof(frase)); numPal = 0;
    uiPush(this);
  }

  void tecla(Tecla t) override {
    switch (t) {
      case ARRIBA:
      case ABAJO:
        // Arriba/abajo cambian SIEMPRE la última letra (saltando las inválidas).
        if (largoPre == 0) { pre[0] = 'a'; pre[1] = '\0'; largoPre = 1; ajustarAValida(1); }
        else {
          char &c = pre[largoPre - 1];
          c = 'a' + (c - 'a' + (t == ARRIBA ? 1 : 25)) % 26;
          ajustarAValida(t == ARRIBA ? 1 : -1);
        }
        sucio = true;
        break;
      case DERECHA:
        if (largoPre < MAX_PRE) {
          pre[largoPre++] = 'a'; pre[largoPre] = '\0';
          ajustarAValida(1);   // primera letra que dé palabra
          int p;
          if (!coincidencias(p)) pre[--largoPre] = '\0';  // ninguna palabra sigue: no se añade
          sucio = true;
        }
        break;
      case IZQUIERDA:
        if (largoPre > 0) { pre[--largoPre] = '\0'; sucio = true; }
        else if (numPal > 0) {
          char *sp = strrchr(frase, ' ');
          if (sp) *sp = '\0'; else frase[0] = '\0';
          numPal--;
          sucio = true;
        } else {
          criptoBorrar(frase, sizeof(frase));
          uiPop();
        }
        break;
      case CENTRO:
        aceptarPalabra();
        break;
      case CENTRO_LARGO:
        if (total) break;  // con número fijo se entrega sola
        if (numPal == 0) return fallo("Frase vacía");
        entregar();
        break;
      default:
        break;
    }
  }

  void dibujar() override {
    char cab[16];
    if (total) snprintf(cab, sizeof(cab), "%d/%d", numPal + 1, total);
    else snprintf(cab, sizeof(cab), "%dp ~%db", numPal, (int)(numPal * BITS_PALABRA + 0.5));
    pantallaCabecera(titulo, cab);

    int primero;
    int n = coincidencias(primero);

    oled.setFont(FUENTE_NORMAL);
    char linea[28];
    if (largoPre == 0) {
      oled.drawUTF8(2, 18, "nueva palabra");
    } else if (n == 0) {
      snprintf(linea, sizeof(linea), "%s (ninguna)", pre);
      oled.drawUTF8(2, 18, linea);
    } else if (n == 1) {
      // Resuelta: una sola palabra, centro la acepta.
      snprintf(linea, sizeof(linea), "%s>%s", pre, DICE_LISTA[primero]);
      oled.drawUTF8(2, 18, linea);
    } else {
      // Aún varias: sigue tecleando (muestra la 1ª y cuántas quedan).
      snprintf(linea, sizeof(linea), "%s>%s +%d", pre, DICE_LISTA[primero], n - 1);
      oled.drawUTF8(2, 18, linea);
    }

    oled.setFont(FUENTE_PEQUENA);
    if (numPal == 0) {
      oled.drawUTF8(0, 30, total ? "3 letras, centro: palabra" : "centro ok, largo teclea");
    } else {
      const char *f = frase;
      int l = strlen(f);
      if (l > 25) f += l - 25;   // los últimos caracteres
      oled.drawUTF8(0, 30, f);
    }
  }
};
static RuedaPalabras ruedaPalabras;

// ---- Tecleo con cuenta atrás ----

class Tecleo : public Pantalla {
  const char *nombre = "";
  std::function<size_t()> accion;  // teclea y devuelve cuántos caracteres se saltaron
  Accion alTerminar;
  uint32_t inicioMs = 0;
  int ultimoSegundo = -1;

 public:
  // Teclear un texto suelto (usuario, contraseña)
  void abrir(const char *titulo, const char *t, Accion cb) {
    abrir(titulo, [t]() { return tecladoEscribir(t); }, cb);
  }

  // Teclear lo que haga la acción (p. ej. ejecutar un guion)
  void abrir(const char *titulo, std::function<size_t()> a, Accion cb) {
    if (!tud_mounted()) return fallo("Sin conexión USB");
    nombre = titulo;
    accion = a;
    alTerminar = cb;
    uiPush(this);
  }

  void entrar() override {
    inicioMs = millis();
    ultimoSegundo = -1;
    ledTecleo(true);
  }

  bool apagable() override { return false; }

  void tecla(Tecla) override {
    ledTecleo(false);
    uiPop();
    aviso("Cancelado");
  }

  void tick() override {
    uint32_t pasado = millis() - inicioMs;
    if (pasado < CUENTA_ATRAS_MS) {
      int segundo = (CUENTA_ATRAS_MS - pasado + 999) / 1000;
      if (segundo != ultimoSegundo) {
        ultimoSegundo = segundo;
        sucio = true;
      }
      return;
    }
    oled.clearBuffer();
    pantallaCabecera(nombre);
    oled.setFont(FUENTE_NORMAL);
    pantallaCentrado(24, "Tecleando...");
    oled.sendBuffer();

    ledTick();
    size_t saltados = accion();
    ledTecleo(false);
    Serial.printf("Tecleado: %s\n", nombre);  // nunca el contenido
    Accion cb = alTerminar;
    uiPop();
    if (saltados) {
      char t[32];
      snprintf(t, sizeof(t), "%u sin teclear", (unsigned)saltados);
      fallo(t);
    } else {
      ledOk();
    }
    if (cb) cb();
  }

  void dibujar() override {
    pantallaCabecera(nombre);
    oled.setFont(FUENTE_GRANDE);
    char t[4];
    snprintf(t, sizeof(t), "%d", ultimoSegundo);
    oled.drawUTF8(4, 26, t);
    oled.setFont(FUENTE_PEQUENA);
    oled.drawUTF8(22, 18, "Pon el cursor.");
    oled.drawUTF8(22, 27, "Una tecla cancela");
  }
};
static Tecleo tecleo;

// ---- Mostrar usuario y contraseña ----

class Mostrar : public Pantalla {
  int id = -1;
  int desplaz = 0;
  uint32_t inicioMs = 0;

  void fila(int f, const char *etiqueta, const char *valor) {
    char tramo[CARACTERES_FILA * 3 + 1];
    char texto[sizeof(tramo) + 8];
    utf8Tramo(valor, desplaz, CARACTERES_FILA - 2, tramo, sizeof(tramo));
    snprintf(texto, sizeof(texto), "%s %s", etiqueta, tramo);
    pantallaFila(f, texto, false);
  }

 public:
  void abrir(int servicio) {
    id = servicio;
    uiPush(this);
  }

  void entrar() override {
    desplaz = 0;
    inicioMs = millis();
  }

  void tecla(Tecla t) override {
    Servicio *s = almacenServicio(id);
    int largo = s ? max(utf8Contar(s->usuario), utf8Contar(s->contrasena)) : 0;
    if (t == DERECHA && desplaz + CARACTERES_FILA - 2 < largo) desplaz += 6;
    else if (t == IZQUIERDA && desplaz > 0) desplaz = max(0, desplaz - 6);
    else if (t == IZQUIERDA || t != DERECHA) uiPop();
    inicioMs = millis();
  }

  void tick() override {
    if (millis() - inicioMs >= MOSTRAR_MS) uiPop();
  }

  void dibujar() override {
    Servicio *s = almacenServicio(id);
    if (!s) return;
    pantallaCabecera(s->nombre, desplaz ? "<>" : ">");
    if (s->usuario[0]) fila(0, "U", s->usuario);
    fila(s->usuario[0] ? 1 : 0, "C", s->contrasena);
  }
};
static Mostrar mostrar;

// ---- Ajustes ----

class MenuAjustes : public Lista {
 protected:
  void titulo(char *buf, size_t tam) override { snprintf(buf, tam, "Ajustes"); }
  int numItems() override { return 7; }

  void item(int i, char *buf, size_t tam) override {
    Ajustes &a = aj();
    switch (i) {
      case 0: snprintf(buf, tam, "Teclado: %s", tecladoNombre((Distribucion)a.distribucion)); break;
      case 1: snprintf(buf, tam, "Retardo: %u ms", RETARDOS_MS[a.retardo]); break;
      case 2: snprintf(buf, tam, "Bloqueo: %u min", BLOQUEOS_MIN[a.bloqueo]); break;
      case 3: snprintf(buf, tam, "Brillo: %s", NOMBRES_BRILLO[a.brillo]); break;
      case 4: snprintf(buf, tam, "LED: %s", a.led ? "sí" : "no"); break;
      case 5: snprintf(buf, tam, "Cambiar PIN"); break;
      case 6: snprintf(buf, tam, "Borrar todo"); break;
    }
  }

  void elegir(int i) override;  // más abajo: usa la pantalla del PIN
};
static MenuAjustes ajustes;

// ---- Contraseña de un servicio nuevo: generarla o escribirla ----

class ElegirContrasena : public Lista {
  AlAceptarTexto alElegir;

 public:
  // En mitad del alta, mantener el centro acepta (como en la rueda), no bloquea
  void tecla(Tecla t) override { Lista::tecla(t == CENTRO_LARGO ? CENTRO : t); }

 protected:
  void titulo(char *buf, size_t tam) override { snprintf(buf, tam, "Contraseña"); }
  int numItems() override { return 2; }
  void item(int i, char *buf, size_t tam) override {
    if (i == 0) snprintf(buf, tam, "Generar (%u)", LARGO_GENERADA);
    else snprintf(buf, tam, "Escribir");
  }
  void elegir(int i) override {
    AlAceptarTexto cb = alElegir;
    if (i == 0) {
      char nueva[LARGO_GENERADA + 1];
      generarContrasena(nueva, LARGO_GENERADA);
      cb(nueva);
      memset(nueva, 0, sizeof(nueva));
    } else {
      uiPop();  // la rueda queda arriba y se reutiliza
      rueda.configurar("Contraseña", "", LARGO_CAMPO, false, cb);
    }
  }

 public:
  void abrir(AlAceptarTexto cb) {
    alElegir = cb;
    uiPush(this);
  }
};
static ElegirContrasena elegirContrasena;

// ---- Editar un servicio ----

class Editar : public Lista {
  int id = -1;

 protected:
  void titulo(char *buf, size_t tam) override {
    Servicio *s = almacenServicio(id);
    snprintf(buf, tam, "Editar %s", s ? s->nombre : "");
  }
  int numItems() override { return 4; }

  void item(int i, char *buf, size_t tam) override {
    Servicio *s = almacenServicio(id);
    if (!s) return (void)snprintf(buf, tam, "-");
    switch (i) {
      case 0: snprintf(buf, tam, "Nombre: %s", s->nombre); break;
      case 1: snprintf(buf, tam, "Usuario: %s", s->usuario[0] ? s->usuario : "(ninguno)"); break;
      case 2: snprintf(buf, tam, "Contraseña: ****"); break;
      case 3: snprintf(buf, tam, "Generar contraseña"); break;
    }
  }

  void elegir(int i) override {
    Servicio *s = almacenServicio(id);
    if (!s) return uiPop();
    switch (i) {
      case 0:
        rueda.abrir("Nombre", s->nombre, sizeof(s->nombre), false, [s](const char *t) {
          almacenCopiar(s->nombre, sizeof(s->nombre), t);
          s->cambiado = true;
          guardar();
          uiPop();
        });
        break;
      case 1:
        rueda.abrir("Usuario", s->usuario, sizeof(s->usuario), true, [s](const char *t) {
          almacenCopiar(s->usuario, sizeof(s->usuario), t);
          s->cambiado = true;
          guardar();
          uiPop();
        });
        break;
      case 2:
        rueda.abrir("Contraseña", s->contrasena, sizeof(s->contrasena), false, [s](const char *t) {
          almacenCopiar(s->contrasena, sizeof(s->contrasena), t);
          s->cambiado = true;
          guardar();
          uiPop();
        });
        break;
      case 3:
        confirmar.abrir("¿Sustituir contraseña?", "Sí, generar otra", [s]() {
          generarContrasena(s->contrasena, LARGO_GENERADA);
          s->cambiado = true;
          guardar();
          uiPop();
          aviso("Generada");
          ledOk();
        });
        break;
    }
  }

 public:
  void abrir(int servicio) {
    id = servicio;
    uiPush(this);
  }
};
static Editar editar;

// ---- Acciones de un servicio ----

class ListaServicios;
extern ListaServicios servicios;

class Acciones : public Lista {
  enum { TECLEAR_USUARIO, TECLEAR_CONTRASENA, EJECUTAR_GUION, MOSTRAR, EDITAR, BORRAR };
  int opciones[6];
  int num = 0;

  void construir() {
    Servicio *s = almacenServicio(id);
    num = 0;
    if (s && s->usuario[0]) opciones[num++] = TECLEAR_USUARIO;
    opciones[num++] = TECLEAR_CONTRASENA;
    if (s && s->guion != SIN_GUION) opciones[num++] = EJECUTAR_GUION;
    opciones[num++] = MOSTRAR;
    opciones[num++] = EDITAR;
    opciones[num++] = BORRAR;
  }

  void seleccionarAccion(int a) {
    for (int i = 0; i < num; i++)
      if (opciones[i] == a) seleccionar(i);
  }

 protected:
  void titulo(char *buf, size_t tam) override {
    Servicio *s = almacenServicio(id);
    snprintf(buf, tam, "%s", s ? s->nombre : "");
  }
  int numItems() override { return num; }

  void item(int i, char *buf, size_t tam) override {
    // El usuario no es secreto: se ve siempre, y elegirlo lo teclea
    Servicio *s = almacenServicio(id);
    if (opciones[i] == TECLEAR_USUARIO && s) return (void)snprintf(buf, tam, "U: %s", s->usuario);
    static const char *const TEXTOS[] = {"", "Teclear contraseña", "Ejecutar guion",
                                         "Mostrar contraseña", "Editar", "Borrar"};
    snprintf(buf, tam, "%s", TEXTOS[opciones[i]]);
  }

  void elegir(int i) override;

 public:
  int id = -1;

  void abrir(int servicio) {
    id = servicio;
    uiPush(this);
  }

  void entrar() override {
    construir();
    Lista::entrar();
  }

  void volver() override {
    construir();
    Lista::volver();
  }
};
static Acciones acciones;

// ---- Servicios de una categoría ----

class ListaServicios : public Lista {
  int ids[MAX_SERVICIOS];
  int num = 0;

  // El servicio en preparación mientras se piden sus campos
  char nombreNuevo[LARGO_NOMBRE];
  char usuarioNuevo[LARGO_CAMPO];

 protected:
  void titulo(char *buf, size_t tam) override { snprintf(buf, tam, "%s", almacenCategoria(cat)); }
  int numItems() override { return num + 3; }

  void item(int i, char *buf, size_t tam) override {
    if (i < num) snprintf(buf, tam, "%s", almacenServicio(ids[i])->nombre);
    else if (i == num) snprintf(buf, tam, "+ Nuevo servicio");
    else if (i == num + 1) snprintf(buf, tam, "Renombrar categoría");
    else snprintf(buf, tam, "Borrar categoría");
  }

  void elegir(int i) override;

 public:
  int cat = 0;

  void abrir(int c) {
    cat = c;
    uiPush(this);
  }

  void entrar() override {
    num = almacenListarServicios(cat, ids, MAX_SERVICIOS);
    Lista::entrar();
  }

  void volver() override {
    num = almacenListarServicios(cat, ids, MAX_SERVICIOS);
    Lista::volver();
  }

  // Tras crear o editar, deja seleccionado ese servicio
  void seleccionarServicio(int id) {
    num = almacenListarServicios(cat, ids, MAX_SERVICIOS);
    for (int i = 0; i < num; i++)
      if (ids[i] == id) seleccionar(i);
  }

  void nuevoServicio();
};
ListaServicios servicios;

// ---- Pomodoro ----

// "12:34" redondeando hacia arriba (nunca enseña 00:00 con tiempo pendiente)
static void minutosSegundos(uint32_t ms, char *buf, size_t tam, bool redondearArriba = true) {
  uint32_t s = redondearArriba ? (ms + 999) / 1000 : ms / 1000;
  snprintf(buf, tam, "%02lu:%02lu", (unsigned long)(s / 60), (unsigned long)(s % 60));
}

// Una línea con el estado, para la pantalla de bloqueo
static void textoPomodoro(char *buf, size_t tam) {
  char t[8];
  const char *nombre = pomodoroNombre(pomodoroFase());
  switch (pomodoroEstado()) {
    case POMO_CORRIENDO:
      minutosSegundos(pomodoroRestanteMs(), t, sizeof(t));
      snprintf(buf, tam, "%s %s", nombre, t);
      break;
    case POMO_PAUSADO:
      minutosSegundos(pomodoroRestanteMs(), t, sizeof(t));
      snprintf(buf, tam, "Pausa: %s %s", nombre, t);
      break;
    case POMO_TERMINADO: snprintf(buf, tam, "Fin: %s", nombre); break;
    default: buf[0] = '\0'; break;
  }
}

// Lo que se ve cambia como mucho una vez por segundo
static uint32_t huellaPomodoro() {
  uint32_t ms = pomodoroEstado() == POMO_TERMINADO ? pomodoroPasadoFinMs() : pomodoroRestanteMs();
  return (ms / 1000) * 16 + pomodoroEstado() * 4 + pomodoroFase();
}

class MenuPomodoro : public Lista {
  enum Opcion : uint8_t { PAUSAR, REANUDAR, FASE_TRABAJO, FASE_DESCANSO, FASE_LARGO, DETENER };
  Opcion opciones[6];

  int construir() {
    int n = 0;
    EstadoPomodoro e = pomodoroEstado();
    if (e == POMO_CORRIENDO || e == POMO_TERMINADO) opciones[n++] = PAUSAR;
    if (pomodoroPuedeReanudar()) opciones[n++] = REANUDAR;
    opciones[n++] = FASE_TRABAJO;
    opciones[n++] = FASE_DESCANSO;
    opciones[n++] = FASE_LARGO;
    if (e != POMO_INACTIVO) opciones[n++] = DETENER;
    return n;
  }

 protected:
  void titulo(char *buf, size_t tam) override { snprintf(buf, tam, "Pomodoro"); }
  int numItems() override { return construir(); }

  void item(int i, char *buf, size_t tam) override {
    construir();
    switch (opciones[i]) {
      case PAUSAR: snprintf(buf, tam, "Pausar"); break;
      case REANUDAR: snprintf(buf, tam, "Reanudar"); break;
      case DETENER: snprintf(buf, tam, "Detener"); break;
      default: {
        FasePomodoro f = (FasePomodoro)(opciones[i] - FASE_TRABAJO);
        snprintf(buf, tam, "%s %lu min", f == DESCANSO_LARGO ? "D. largo" : pomodoroNombre(f),
                 (unsigned long)(pomodoroDuracionMs(f) / 60000));
      }
    }
  }

  void elegir(int i) override {
    construir();
    switch (opciones[i]) {
      case PAUSAR: pomodoroPausar(); break;
      case REANUDAR: pomodoroReanudar(); break;
      case DETENER: pomodoroDetener(); break;
      default: pomodoroEmpezar((FasePomodoro)(opciones[i] - FASE_TRABAJO)); break;
    }
    uiPop();
  }

 public:
  Pantalla *trasBloqueo() override;

  // El cursor empieza en lo más probable: la fase que toca si la anterior ha
  // terminado o está parada, «Pausar»/«Reanudar» si hay una a medias.
  void abrir() {
    uiPush(this);
    EstadoPomodoro e = pomodoroEstado();
    bool aMedias = e == POMO_CORRIENDO || pomodoroPuedeReanudar();
    if (aMedias) return;
    int n = construir();
    int fase = e == POMO_INACTIVO ? TRABAJO : pomodoroSugerida();
    Opcion buscada = (Opcion)(FASE_TRABAJO + fase);
    for (int i = 0; i < n; i++)
      if (opciones[i] == buscada) seleccionar(i);
  }
};
static MenuPomodoro menuPomodoro;

class PantallaPomodoro : public Pantalla {
  uint32_t huella = 0;

 public:
  Pantalla *trasBloqueo() override { return this; }
  bool atenuable() override { return pomodoroEstado() != POMO_INACTIVO; }

  void tecla(Tecla t) override {
    switch (t) {
      case CENTRO:
      case DERECHA: menuPomodoro.abrir(); break;
      case IZQUIERDA: uiPop(); break;
      case CENTRO_LARGO: bloquear(); break;
      default: break;
    }
  }

  void tick() override {
    uint32_t h = huellaPomodoro();
    if (h != huella) {
      huella = h;
      sucio = true;
    }
  }

  void dibujar() override {
    EstadoPomodoro e = pomodoroEstado();
    if (e == POMO_INACTIVO) {
      pantallaCabecera("Pomodoro");
      oled.setFont(FUENTE_NORMAL);
      pantallaCentrado(19, "Centro: elegir fase");
      pantallaCentrado(30, "Izquierda: volver");
      return;
    }
    const char *estado = e == POMO_PAUSADO ? "pausa" : e == POMO_TERMINADO ? "¡fin!" : "";
    pantallaCabecera(pomodoroNombre(pomodoroFase()), estado);
    char t[10];
    if (e == POMO_TERMINADO) {
      // Tiempo pasado desde el final
      t[0] = '+';
      minutosSegundos(pomodoroPasadoFinMs(), t + 1, sizeof(t) - 1, false);
    } else {
      minutosSegundos(pomodoroRestanteMs(), t, sizeof(t));
    }
    oled.setFont(FUENTE_GRANDE);
    pantallaCentrado(28, t);
  }
};
static PantallaPomodoro pantallaPomodoro;

Pantalla *MenuPomodoro::trasBloqueo() { return &pantallaPomodoro; }

// ---- Mantener sesión ----

// "1 h 23 min", "45 min", "<1 min"
static void textoDuracion(uint32_t ms, char *buf, size_t tam) {
  uint32_t min = (ms + 59999) / 60000;
  if (ms < 60000) snprintf(buf, tam, "<1 min");
  else if (min < 60) snprintf(buf, tam, "%lu min", (unsigned long)min);
  else if (min % 60 == 0) snprintf(buf, tam, "%lu h", (unsigned long)(min / 60));
  else snprintf(buf, tam, "%lu h %lu min", (unsigned long)(min / 60), (unsigned long)(min % 60));
}

static void textoMantener(char *buf, size_t tam) {
  if (mantenerSinLimite()) {
    snprintf(buf, tam, "Sesión: sin límite");
  } else {
    char d[16];
    textoDuracion(mantenerRestanteMs(), d, sizeof(d));
    snprintf(buf, tam, "Sesión: %s", d);
  }
}

static const uint32_t DURACIONES_MANTENER_MS[] = {30 * 60000UL, 60 * 60000UL, 120 * 60000UL,
                                                   240 * 60000UL, 0};

class MenuMantener : public Lista {
  int primeraDuracion() { return mantenerActivo() ? 1 : 0; }

 protected:
  void titulo(char *buf, size_t tam) override { snprintf(buf, tam, "Mantener sesión"); }
  int numItems() override { return primeraDuracion() + NUM(DURACIONES_MANTENER_MS); }

  void item(int i, char *buf, size_t tam) override {
    if (i < primeraDuracion()) {
      char d[24];
      if (mantenerSinLimite()) snprintf(d, sizeof(d), "sin límite");
      else textoDuracion(mantenerRestanteMs(), d, sizeof(d));
      snprintf(buf, tam, "Desactivar (%s)", d);
      return;
    }
    uint32_t ms = DURACIONES_MANTENER_MS[i - primeraDuracion()];
    if (!ms) snprintf(buf, tam, "Sin límite");
    else textoDuracion(ms, buf, tam);
  }

  void elegir(int i) override {
    uiPop();
    if (i < primeraDuracion()) {
      mantenerDesactivar();
      return aviso("Desactivado");
    }
    uint32_t ms = DURACIONES_MANTENER_MS[i - primeraDuracion()];
    mantenerActivar(ms);
    char t[32], d[16];
    if (ms) textoDuracion(ms, d, sizeof(d));
    snprintf(t, sizeof(t), "Activo: %s", ms ? d : "sin límite");
    aviso(t);
  }
};
static MenuMantener menuMantener;

// ---- Azar (azar.h): moneda, dados, contraseñas, colores... ----
//
// No toca la bóveda, así que funciona también bloqueado (izquierda en la
// pantalla de bloqueo). Los parámetros se recuerdan en /azar.bin, aparte de
// la bóveda (no son secretos y así se guardan aunque esté bloqueado).

static const char *const FICHERO_AZAR = "/azar.bin";
static const char *const TEMPORAL_AZAR = "/azar.tmp";
static const uint32_t MAGIA_AZAR = 0x415A5201;  // «AZR» y versión 1

static const int32_t CARAS_DADO[] = {4, 6, 8, 10, 12, 20, 100};
static const char SEPARADORES[] = {'-', ' ', '.', 0};
static const char *const NOMBRES_SEPARADOR[] = {"-", "espacio", ".", "ninguno"};

struct AjustesAzar {
  uint32_t magia;
  int32_t largo, minusculas, mayusculas, cifras, simbolos, sinParecidos;
  int32_t palabras, separador;  // separador: índice de SEPARADORES
  int32_t cifrasPin;
  int32_t monedas;
  int32_t dados, caras;         // caras: índice de CARAS_DADO
  int32_t desde, hasta, cuantos, repetidos;
  int32_t sonido;
};
static const AjustesAzar AZAR_INICIAL = {MAGIA_AZAR, 16, 1, 1, 1, 1, 1, 6, 0, 6, 1, 2, 1, 1, 99, 6, 0, 1};
static AjustesAzar az = AZAR_INICIAL;

static void cargarAzar() {
  File f = LittleFS.open(FICHERO_AZAR, "r");
  if (!f) return;
  AjustesAzar leidos;
  bool ok = f.read((uint8_t *)&leidos, sizeof(leidos)) == sizeof(leidos) && leidos.magia == MAGIA_AZAR;
  f.close();
  if (ok) az = leidos;
}

static void guardarAzar() {
  File f = LittleFS.open(TEMPORAL_AZAR, "w");
  if (!f) return fallo("Error al guardar");
  bool ok = f.write((const uint8_t *)&az, sizeof(az)) == sizeof(az);
  f.close();
  if (!ok || !LittleFS.rename(TEMPORAL_AZAR, FICHERO_AZAR)) fallo("Error al guardar");
}

// ---- Parámetros de cada generador ----

enum TipoParam : uint8_t { P_NUMERO, P_CASILLA, P_LISTA };

struct Param {
  const char *nombre;
  int32_t *valor;
  TipoParam tipo;
  int32_t min, max;                 // P_LISTA: max = número de opciones
  const char *(*opcion)(int32_t);   // P_LISTA: nombre de cada opción
};

static const char *nombreCaras(int32_t i) {
  static char t[6];
  snprintf(t, sizeof(t), "d%ld", (long)CARAS_DADO[i]);
  return t;
}
static const char *nombreSeparador(int32_t i) { return NOMBRES_SEPARADOR[i]; }

static const Param PARAMS_CONTRASENA[] = {
  {"Longitud", &az.largo, P_NUMERO, 4, 64, nullptr},
  {"a-z", &az.minusculas, P_CASILLA, 0, 1, nullptr},
  {"A-Z", &az.mayusculas, P_CASILLA, 0, 1, nullptr},
  {"0-9", &az.cifras, P_CASILLA, 0, 1, nullptr},
  {"Símbolos", &az.simbolos, P_CASILLA, 0, 1, nullptr},
  {"Excl. parecidos", &az.sinParecidos, P_CASILLA, 0, 1, nullptr},
};
static const Param PARAMS_DICEWARE[] = {
  {"Palabras", &az.palabras, P_NUMERO, 3, 10, nullptr},
  {"Separador", &az.separador, P_LISTA, 0, NUM(SEPARADORES), nombreSeparador},
};
static const Param PARAMS_PIN[] = {{"Cifras", &az.cifrasPin, P_NUMERO, 4, 12, nullptr}};
static const Param PARAMS_MONEDA[] = {{"Monedas", &az.monedas, P_NUMERO, 1, 20, nullptr}};
static const Param PARAMS_DADOS[] = {
  {"Dados", &az.dados, P_NUMERO, 1, 10, nullptr},
  {"Caras", &az.caras, P_LISTA, 0, NUM(CARAS_DADO), nombreCaras},
};
static const Param PARAMS_NUMEROS[] = {
  {"Desde", &az.desde, P_NUMERO, 0, 999999, nullptr},
  {"Hasta", &az.hasta, P_NUMERO, 0, 999999, nullptr},
  {"Cuántos", &az.cuantos, P_NUMERO, 1, 20, nullptr},
  {"Repetidos", &az.repetidos, P_CASILLA, 0, 1, nullptr},
};

// Lo leído del fichero puede venir de otra versión: se ajusta a los límites
static void validarAzar() {
  const Param *todos[] = {PARAMS_CONTRASENA, PARAMS_DICEWARE, PARAMS_PIN,
                          PARAMS_MONEDA, PARAMS_DADOS, PARAMS_NUMEROS};
  const int num[] = {NUM(PARAMS_CONTRASENA), NUM(PARAMS_DICEWARE), NUM(PARAMS_PIN),
                     NUM(PARAMS_MONEDA), NUM(PARAMS_DADOS), NUM(PARAMS_NUMEROS)};
  for (int g = 0; g < NUM(todos); g++) {
    for (int i = 0; i < num[g]; i++) {
      const Param &p = todos[g][i];
      int32_t tope = p.tipo == P_LISTA ? p.max - 1 : p.max;
      if (*p.valor < p.min || *p.valor > tope) *p.valor = p.min;
    }
  }
  if (!az.minusculas && !az.mayusculas && !az.cifras && !az.simbolos) az.minusculas = 1;
  az.sonido = az.sonido ? 1 : 0;
}

// ---- Generadores ----

struct Resultado {
  char valor[200];
  char info[16];   // a la derecha de la cabecera: «95 bits», «suma 9»
  uint32_t color;
  bool hayColor;
};

static void bits(Resultado &r, float b) {
  snprintf(r.info, sizeof(r.info), "%d bits", (int)b);
}

static bool genContrasena(Resultado &r) {
  uint8_t c = (az.minusculas ? AZAR_MINUSCULAS : 0) | (az.mayusculas ? AZAR_MAYUSCULAS : 0) |
              (az.cifras ? AZAR_CIFRAS : 0) | (az.simbolos ? AZAR_SIMBOLOS : 0);
  bits(r, azarContrasena(r.valor, sizeof(r.valor), az.largo, c, az.sinParecidos));
  return true;
}
static bool genDiceware(Resultado &r) {
  bits(r, azarDiceware(r.valor, sizeof(r.valor), az.palabras, SEPARADORES[az.separador]));
  return true;
}
static bool genPin(Resultado &r) {
  bits(r, azarPin(r.valor, sizeof(r.valor), az.cifrasPin));
  return true;
}
static bool genMoneda(Resultado &r) {
  azarMonedas(r.valor, sizeof(r.valor), r.info, sizeof(r.info), az.monedas);
  return true;
}
static bool genDados(Resultado &r) {
  azarDados(r.valor, sizeof(r.valor), r.info, sizeof(r.info), az.dados, CARAS_DADO[az.caras]);
  return true;
}
static bool genNumeros(Resultado &r) {
  snprintf(r.info, sizeof(r.info), "%ld-%ld", (long)min(az.desde, az.hasta), (long)max(az.desde, az.hasta));
  return azarNumeros(r.valor, sizeof(r.valor), az.desde, az.hasta, az.cuantos, az.repetidos);
}
static bool genColor(Resultado &r) {
  r.color = azarColor();
  r.hayColor = true;
  snprintf(r.valor, sizeof(r.valor), "#%06lX", (unsigned long)r.color);
  bits(r, 24);
  return true;
}
static bool genAlias(Resultado &r) {
  bits(r, azarAlias(r.valor, sizeof(r.valor)));
  return true;
}
static bool genMac(Resultado &r) {
  bits(r, azarMac(r.valor, sizeof(r.valor)));
  return true;
}
static bool genAlfanumerico(Resultado &r) {
  bits(r, azarFormato(r.valor, sizeof(r.valor), AZAR_ALFANUMERICO, 32));
  return true;
}
static bool genToken(Resultado &r) {
  bits(r, azarFormato(r.valor, sizeof(r.valor), AZAR_TOKEN, 64));
  return true;
}
static bool genHex(Resultado &r) {
  bits(r, azarFormato(r.valor, sizeof(r.valor), AZAR_HEX, 32));
  return true;
}
static bool genUuid(Resultado &r) {
  bits(r, azarUuid(r.valor, sizeof(r.valor)));
  return true;
}

// Para que la pantalla no cambie de fuente ni se mueva entre valores de la
// ruleta, el tamaño se decide con el caso más largo posible:
//   - texto: largoMax() caracteres como mucho;
//   - fichas (números, dados, monedas, PIN): fichas() valores separados por
//     espacios, de hueco() caracteres como mucho; cada uno se alinea a la
//     derecha en su hueco (el 7 ocupa el sitio del 43).
struct Generador {
  const char *nombre;
  const char *accion;   // primera entrada de su menú: «Generar», «Lanzar»
  const Param *params;  // nullptr: sin menú, genera directamente
  int numParams;
  bool (*generar)(Resultado &);
  int (*largoMax)();    // texto (nullptr si son fichas)
  int (*hueco)();       // fichas (nullptr si es texto)
  int (*fichas)();
};

static int cifras(int32_t v) {
  int n = 1;
  for (v = v < 0 ? -v : v; v >= 10; v /= 10) n++;
  return n;
}
static int largoContrasena() { return az.largo; }
static int largoDiceware() { return az.palabras * 11; }  // palabras de hasta 10 letras
static int largoAlias() { return 10 + 1 + 10 + 3; }
static int largoColor() { return 7; }
static int largoMac() { return 17; }
static int largo32() { return 32; }
static int largo64() { return 64; }
static int largoUuid() { return 36; }
static int huecoPin() { return az.cifrasPin; }
static int huecoUno() { return 1; }
static int huecoDado() { return cifras(CARAS_DADO[az.caras]); }
static int huecoNumero() { return max(cifras(az.desde), cifras(az.hasta)); }
static int fichasUna() { return 1; }
static int fichasMonedas() { return az.monedas; }
static int fichasDados() { return az.dados; }
static int fichasNumeros() { return az.cuantos; }

static const Generador GENERADORES[] = {
  {"Contraseña", "Generar", PARAMS_CONTRASENA, NUM(PARAMS_CONTRASENA), genContrasena, largoContrasena},
  {"Diceware", "Generar", PARAMS_DICEWARE, NUM(PARAMS_DICEWARE), genDiceware, largoDiceware},
  {"PIN", "Generar", PARAMS_PIN, NUM(PARAMS_PIN), genPin, nullptr, huecoPin, fichasUna},
  {"Moneda", "Lanzar", PARAMS_MONEDA, NUM(PARAMS_MONEDA), genMoneda, nullptr, huecoUno, fichasMonedas},
  {"Dados", "Lanzar", PARAMS_DADOS, NUM(PARAMS_DADOS), genDados, nullptr, huecoDado, fichasDados},
  {"Números", "Generar", PARAMS_NUMEROS, NUM(PARAMS_NUMEROS), genNumeros, nullptr, huecoNumero, fichasNumeros},
  {"Color", nullptr, nullptr, 0, genColor, largoColor},
  {"Alias", nullptr, nullptr, 0, genAlias, largoAlias},
  {"MAC", nullptr, nullptr, 0, genMac, largoMac},
  {"Alfanumérico 32", nullptr, nullptr, 0, genAlfanumerico, largo32},
  {"Token 64", nullptr, nullptr, 0, genToken, largo64},
  {"Hexadecimal 32", nullptr, nullptr, 0, genHex, largo32},
  {"UUID", nullptr, nullptr, 0, genUuid, largoUuid},
};

// ---- Editar un número ----
//
// Hasta 999: arriba/abajo suman o restan 1 (mantenidos, se repiten y
// aceleran). Desde 1000: por cifras; izquierda/derecha eligen la cifra y
// arriba/abajo la cambian. Centro acepta; izquierda (en la primera cifra) cancela.

using AlAceptarNumero = std::function<void(int32_t)>;

class EditarNumero : public Pantalla {
  const char *nombre = "";
  int32_t valor = 0, minimo = 0, maximo = 0;
  int digitos = 0;   // 0: de uno en uno
  int cursor = 0;    // cifra que se cambia: 0 = unidades
  AlAceptarNumero alAceptar;

  int32_t potencia(int n) {
    int32_t p = 1;
    while (n--) p *= 10;
    return p;
  }

  void sumar(int32_t paso) {
    valor = constrain(valor + paso, minimo, maximo);
  }

  void aceptar() {
    AlAceptarNumero cb = alAceptar;
    int32_t v = valor;
    uiPop();
    cb(v);
  }

 public:
  void abrir(const char *titulo, int32_t inicial, int32_t mn, int32_t mx, AlAceptarNumero cb) {
    nombre = titulo;
    minimo = mn;
    maximo = mx;
    valor = constrain(inicial, mn, mx);
    digitos = 0;
    if (mx >= 1000)
      for (int32_t m = mx; m; m /= 10) digitos++;
    cursor = 0;
    alAceptar = cb;
    uiPush(this);
  }

  void tecla(Tecla t) override {
    int32_t paso = digitos ? potencia(cursor) : 1;
    switch (t) {
      case ARRIBA: sumar(paso); break;
      case ABAJO: sumar(-paso); break;
      case DERECHA:
        if (!digitos) aceptar();
        else if (cursor > 0) cursor--;
        break;
      case IZQUIERDA:
        if (digitos && cursor < digitos - 1) cursor++;
        else uiPop();  // cancelar
        break;
      case CENTRO:
      case CENTRO_LARGO: aceptar(); break;
      default: break;
    }
  }

  void dibujar() override {
    char rango[24];
    snprintf(rango, sizeof(rango), "%ld-%ld", (long)minimo, (long)maximo);
    pantallaCabecera(nombre, rango);
    oled.setFont(FUENTE_GRANDE);
    char t[12];
    if (digitos) snprintf(t, sizeof(t), "%0*ld", digitos, (long)valor);
    else snprintf(t, sizeof(t), "%ld", (long)valor);
    int ancho = oled.getStrWidth(t);
    int x = (128 - ancho) / 2;
    oled.drawStr(x, 27, t);
    if (digitos) {
      // Raya bajo la cifra que se cambia
      int i = digitos - 1 - cursor;
      char antes[12];
      memcpy(antes, t, i);
      antes[i] = '\0';
      char cifra[2] = {t[i], '\0'};
      oled.drawHLine(x + oled.getStrWidth(antes), 30, oled.getStrWidth(cifra));
    }
  }
};
static EditarNumero editarNumero;

// ---- Resultado, con efecto ruleta ----
//
// Antes del resultado pasan valores al azar cada vez más despacio (con un
// clic del zumbador en cada uno si el sonido está activo). Una tecla lo
// salta. Después: centro teclea, derecha genera otro, izquierda sale (y
// borra el resultado de la RAM), arriba/abajo desplazan si no cabe.

static const uint16_t RULETA_PRIMER_PASO_MS = 35;
static const uint16_t RULETA_ULTIMO_PASO_MS = 300;
static const uint16_t RULETA_CLIC_HZ = 1800;
static const uint16_t RULETA_FIN_HZ = 2400;

class PantallaResultado : public Pantalla {
  const Generador *gen = nullptr;
  Resultado r, candidato;
  bool activo = false;
  bool animando = false;
  uint32_t proximoMs = 0;
  uint32_t pasoMs = 0;
  int desplaz = 0;          // primera fila a la vista
  uint32_t mitad = 0;       // la cabecera alterna cada 3 s
  bool grande = false;      // fuente grande (se decide una vez por generación)
  int hueco = 0;            // fichas: caracteres de cada una (0: es texto)
  int numFichas = 0;
  int avance = 0;           // fichas en grande: ancho de cada carácter

  // Texto en grande: helvB12 es proporcional; 11 px por carácter es holgado
  static const int ANCHO_TEXTO_GRANDE = 11;
  static const int ANCHO_UTIL = 124;

  void decidirFormato() {
    hueco = gen->hueco ? gen->hueco() : 0;
    numFichas = gen->fichas ? gen->fichas() : 0;
    oled.setFont(FUENTE_GRANDE);
    if (hueco) {
      avance = 0;
      for (const char *c = "0123456789CX"; *c; c++) {
        char t[2] = {*c, '\0'};
        avance = max(avance, (int)oled.getStrWidth(t));
      }
      grande = anchoFichas() <= ANCHO_UTIL;
    } else {
      grande = gen->largoMax() * ANCHO_TEXTO_GRANDE <= ANCHO_UTIL;
    }
  }

  int anchoFichas() { return numFichas * hueco * avance + (numFichas - 1) * (avance / 2); }
  int fichasPorFila() { return max(1, (CARACTERES_FILA + 1) / (hueco + 1)); }

  // Parte el valor en fichas (separadas por espacios); devuelve cuántas
  int partir(const char *valor, const char *trozos[], int largos[], int max) {
    int n = 0;
    for (const char *p = valor; *p && n < max;) {
      while (*p == ' ') p++;
      if (!*p) break;
      trozos[n] = p;
      while (*p && *p != ' ') p++;
      largos[n] = p - trozos[n];
      n++;
    }
    return n;
  }

  bool generar() {
    memset(&r, 0, sizeof(r));
    azarSembrar();
    if (!gen->generar(r)) {
      criptoBorrar(&r, sizeof(r));
      return false;
    }
    animando = true;
    pasoMs = RULETA_PRIMER_PASO_MS;
    proximoMs = millis();
    desplaz = 0;
    decidirFormato();
    return true;
  }

  void terminarRuleta() {
    animando = false;
    criptoBorrar(&candidato, sizeof(candidato));
    if (r.hayColor) ledColorAzar(r.color, true);
    if (az.sonido) zumbadorPitido(RULETA_FIN_HZ, 40);
    sucio = true;
  }

  int filas(const char *t) {
    if (grande) return 1;
    int n = hueco ? numFichas : utf8Contar(t);
    int porFila = hueco ? fichasPorFila() : CARACTERES_FILA;
    return n ? (n + porFila - 1) / porFila : 1;
  }

 public:
  void abrir(const Generador *g) {
    gen = g;
    if (!generar()) return fallo("No caben sin repetir");
    activo = true;
    uiPush(this);
  }

  // Al salir (o si la pantalla desaparece por el bloqueo): fuera de la RAM y del LED
  void soltar() {
    if (!activo) return;
    activo = animando = false;
    criptoBorrar(&r, sizeof(r));
    criptoBorrar(&candidato, sizeof(candidato));
    ledColorAzar(0, false);
  }

  void tecla(Tecla t) override {
    if (animando) return terminarRuleta();
    switch (t) {
      case ARRIBA:
        if (desplaz > 0) desplaz--;
        break;
      case ABAJO:
        if (desplaz + 2 < filas(r.valor)) desplaz++;
        break;
      case CENTRO: tecleo.abrir(gen->nombre, r.valor, nullptr); break;
      case DERECHA:
        if (!generar()) fallo("No caben sin repetir");
        break;
      case IZQUIERDA:
        soltar();
        uiPop();
        break;
      case CENTRO_LARGO: bloquear(); break;
      default: break;
    }
  }

  void tick() override {
    uint32_t ahora = millis();
    if (!animando) {
      uint32_t m = ahora / 3000;
      if (m != mitad) {
        mitad = m;
        sucio = true;
      }
      return;
    }
    if ((int32_t)(ahora - proximoMs) < 0) return;
    if (pasoMs > RULETA_ULTIMO_PASO_MS) return terminarRuleta();
    memset(&candidato, 0, sizeof(candidato));
    gen->generar(candidato);
    if (candidato.hayColor) ledColorAzar(candidato.color, true);
    if (az.sonido) zumbadorPitido(RULETA_CLIC_HZ, 4);
    proximoMs = ahora + pasoMs;
    pasoMs = pasoMs * 118 / 100;  // cada vez más despacio
    sucio = true;
  }

  void dibujar() override {
    const Resultado &v = animando ? candidato : r;
    bool ayuda = !animando && (millis() / 3000) % 2;
    pantallaCabecera(ayuda ? "C teclea, D otra" : gen->nombre, animando ? "" : r.info);
    if (hueco) return dibujarFichas(v.valor);
    if (grande) {
      oled.setFont(FUENTE_GRANDE);
      pantallaCentrado(27, v.valor);
      return;
    }
    char tramo[CARACTERES_FILA * 3 + 1];
    for (int f = 0; f < 2; f++) {
      utf8Tramo(v.valor, (desplaz + f) * CARACTERES_FILA, CARACTERES_FILA, tramo, sizeof(tramo));
      pantallaFila(f, tramo, false);
    }
  }

  // Cada ficha alineada a la derecha en su hueco
  void dibujarFichas(const char *valor) {
    static const int MAX_FICHAS = 50;
    const char *trozos[MAX_FICHAS];
    int largos[MAX_FICHAS];
    int n = partir(valor, trozos, largos, MAX_FICHAS);
    if (grande) {
      oled.setFont(FUENTE_GRANDE);
      int paso = hueco * avance + avance / 2;
      int x0 = (128 - anchoFichas()) / 2;
      for (int i = 0; i < n; i++) {
        int x = x0 + i * paso + (hueco - largos[i]) * avance;
        for (int j = 0; j < largos[i]; j++) {
          char c[2] = {trozos[i][j], '\0'};
          oled.drawStr(x + j * avance + (avance - oled.getStrWidth(c)) / 2, 27, c);
        }
      }
      return;
    }
    // En filas (fuente de ancho fijo): rellenando con espacios a la izquierda
    int porFila = fichasPorFila();
    for (int f = 0; f < 2; f++) {
      char linea[CARACTERES_FILA + 1] = "";
      size_t usado = 0;
      for (int k = 0; k < porFila; k++) {
        int i = (desplaz + f) * porFila + k;
        if (i >= n) break;
        int w = snprintf(linea + usado, sizeof(linea) - usado, k ? " %*.*s" : "%*.*s", hueco, largos[i], trozos[i]);
        if (w < 0 || usado + w >= sizeof(linea)) break;
        usado += w;
      }
      pantallaFila(f, linea, false);
    }
  }
};
static PantallaResultado resultado;

// ---- Prueba del azar ----
//
// Tira 6000 dados y dibuja cuántas veces sale cada cara. Con un buen azar,
// las seis barras salen casi iguales; chi² (5 grados de libertad) lo mide:
// por debajo de 11,07 es normal; por encima de 15,09 pasa menos del 1 % de las veces.

static const uint32_t PRUEBA_TIRADAS = 6000;
static const uint32_t PRUEBA_POR_TICK = 120;

class PantallaPrueba : public Pantalla {
  uint32_t veces[6];
  uint32_t total = 0;

  void empezar() {
    memset(veces, 0, sizeof(veces));
    total = 0;
    azarSembrar();
    sucio = true;
  }

  float chi2() {
    float esperado = total / 6.0f, s = 0;
    for (int i = 0; i < 6; i++) s += (veces[i] - esperado) * (veces[i] - esperado) / esperado;
    return s;
  }

 public:
  void entrar() override { empezar(); }

  void tecla(Tecla t) override {
    switch (t) {
      case CENTRO:
      case DERECHA: empezar(); break;
      case IZQUIERDA: uiPop(); break;
      case CENTRO_LARGO: bloquear(); break;
      default: break;
    }
  }

  void tick() override {
    if (total >= PRUEBA_TIRADAS) return;
    for (uint32_t i = 0; i < PRUEBA_POR_TICK; i++) veces[azarUniforme(6)]++;
    total += PRUEBA_POR_TICK;
    if (total >= PRUEBA_TIRADAS && az.sonido) zumbadorPitido(RULETA_FIN_HZ, 40);
    sucio = true;
  }

  void dibujar() override {
    char izq[32], der[16];
    if (total < PRUEBA_TIRADAS) {
      snprintf(izq, sizeof(izq), "Prueba del azar");
      snprintf(der, sizeof(der), "%lu", (unsigned long)total);
    } else {
      float c = chi2();
      const char *veredicto = c < 11.07f ? "bien" : c < 15.09f ? "dudoso" : "raro";
      snprintf(izq, sizeof(izq), "chi2 %.1f: %s", c, veredicto);
      snprintf(der, sizeof(der), "%s", azarConChip() ? "ATECC" : "sin chip");
    }
    pantallaCabecera(izq, der);

    uint32_t mayor = 1;
    for (int i = 0; i < 6; i++) mayor = max(mayor, veces[i]);
    oled.setFont(FUENTE_PEQUENA);
    for (int i = 0; i < 6; i++) {
      int x = i * 21, alto = veces[i] * 22 / mayor;
      oled.drawBox(x, 32 - alto, 20, alto);
      char n[8];
      snprintf(n, sizeof(n), "%lu", (unsigned long)veces[i]);
      if (alto >= 9) {  // el número dentro de la barra, en negativo
        oled.setDrawColor(0);
        oled.drawStr(x + (20 - oled.getStrWidth(n)) / 2, 32 - alto + 8, n);
        oled.setDrawColor(1);
      }
    }
  }
};
static PantallaPrueba pantallaPrueba;

// ---- Menús ----

class MenuParametros : public Lista {
  const Generador *gen = nullptr;

  static int conjuntos() { return az.minusculas + az.mayusculas + az.cifras + az.simbolos; }

 protected:
  void titulo(char *buf, size_t tam) override { snprintf(buf, tam, "%s", gen->nombre); }
  int numItems() override { return gen->numParams + 1; }

  void item(int i, char *buf, size_t tam) override {
    if (!i) return (void)snprintf(buf, tam, "%s", gen->accion);
    const Param &p = gen->params[i - 1];
    switch (p.tipo) {
      case P_NUMERO: snprintf(buf, tam, "%s: %ld", p.nombre, (long)*p.valor); break;
      case P_CASILLA: snprintf(buf, tam, "[%c] %s", *p.valor ? 'x' : ' ', p.nombre); break;
      case P_LISTA: snprintf(buf, tam, "%s: %s", p.nombre, p.opcion(*p.valor)); break;
    }
  }

  void elegir(int i) override {
    if (!i) return resultado.abrir(gen);
    const Param &p = gen->params[i - 1];
    int32_t *v = p.valor;
    switch (p.tipo) {
      case P_NUMERO:
        return editarNumero.abrir(p.nombre, *v, p.min, p.max, [v](int32_t n) {
          *v = n;
          guardarAzar();
        });
      case P_CASILLA:
        // Las casillas de conjuntos: siempre al menos uno marcado
        if (*v && gen->params == PARAMS_CONTRASENA && i - 1 < 5 && i - 1 >= 1 && conjuntos() == 1)
          return fallo("Al menos un conjunto");
        *v = !*v;
        break;
      case P_LISTA: *v = (*v + 1) % p.max; break;
    }
    guardarAzar();
  }

 public:
  void abrir(const Generador *g) {
    gen = g;
    uiPush(this);
  }
};
static MenuParametros menuParametros;

class MenuAzar : public Lista {
 protected:
  void titulo(char *buf, size_t tam) override { snprintf(buf, tam, "Azar"); }
  int numItems() override { return NUM(GENERADORES) + 2; }

  void item(int i, char *buf, size_t tam) override {
    if (i < NUM(GENERADORES)) snprintf(buf, tam, "%s", GENERADORES[i].nombre);
    else if (i == NUM(GENERADORES)) snprintf(buf, tam, "Prueba del azar");
    else snprintf(buf, tam, "Sonido ruleta: %s", az.sonido ? "sí" : "no");
  }

  void elegir(int i) override {
    if (i < NUM(GENERADORES)) {
      const Generador &g = GENERADORES[i];
      if (g.params) menuParametros.abrir(&g);
      else resultado.abrir(&g);
    } else if (i == NUM(GENERADORES)) {
      uiPush(&pantallaPrueba);
    } else {
      az.sonido = !az.sonido;
      guardarAzar();
    }
  }
};
static MenuAzar menuAzar;

// ---- Menú principal: categorías ----

class MenuPrincipal : public Lista {
 protected:
  void titulo(char *buf, size_t tam) override { snprintf(buf, tam, "Ardutype"); }
  int numItems() override { return almacenNumCategorias() + 6; }

  void item(int i, char *buf, size_t tam) override {
    int n = almacenNumCategorias();
    if (i < n) snprintf(buf, tam, "%s (%d)", almacenCategoria(i), almacenContarServicios(i));
    else if (i == n) snprintf(buf, tam, "+ Nueva categoría");
    else if (i == n + 1) snprintf(buf, tam, "Pomodoro");
    else if (i == n + 2 && mantenerActivo()) textoMantener(buf, tam);
    else if (i == n + 2) snprintf(buf, tam, "Mantener sesión");
    else if (i == n + 3) snprintf(buf, tam, "Azar");
    else if (i == n + 4) snprintf(buf, tam, "Ajustes");
    else snprintf(buf, tam, "Bloquear");
  }

  void elegir(int i) override;
  void atras() override {}  // es la raíz
};
static MenuPrincipal menu;

void MenuPrincipal::elegir(int i) {
  int n = almacenNumCategorias();
  if (i < n) return servicios.abrir(i);
  if (i == n + 1) return uiPush(&pantallaPomodoro);
  if (i == n + 2) return uiPush(&menuMantener);
  if (i == n + 3) return uiPush(&menuAzar);
  if (i == n + 4) return uiPush(&ajustes);
  if (i == n + 5) return bloquear();
  rueda.abrir("Nueva categoría", "", LARGO_NOMBRE, false, [](const char *t) {
    int c = almacenNuevaCategoria(t);
    uiVolverA(&menu);
    if (c < 0) return fallo("No caben más");
    guardar();
    menu.seleccionar(c);
    servicios.abrir(c);
  });
}

void ListaServicios::nuevoServicio() {
  rueda.abrir("Nombre del servicio", "", LARGO_NOMBRE, false, [this](const char *t) {
    almacenCopiar(nombreNuevo, sizeof(nombreNuevo), t);
    rueda.configurar("Usuario (opcional)", "", LARGO_CAMPO, true, [this](const char *u) {
      almacenCopiar(usuarioNuevo, sizeof(usuarioNuevo), u);
      elegirContrasena.abrir([this](const char *c) {
        int id = almacenNuevoServicio(cat, nombreNuevo, usuarioNuevo, c);
        memset(usuarioNuevo, 0, sizeof(usuarioNuevo));
        uiVolverA(this);
        if (id < 0) return fallo("No caben más");
        guardar();
        seleccionarServicio(id);
        acciones.abrir(id);
      });
    });
  });
}

void ListaServicios::elegir(int i) {
  if (i < num) return acciones.abrir(ids[i]);
  if (i == num) return nuevoServicio();
  if (i == num + 1) {
    return rueda.abrir("Renombrar", almacenCategoria(cat), LARGO_NOMBRE, false, [this](const char *t) {
      almacenRenombrarCategoria(cat, t);
      guardar();
      uiPop();
    });
  }
  char pregunta[48];
  static char preguntaGuardada[48];
  snprintf(pregunta, sizeof(pregunta), num ? "¿Borrar con %d servicios?" : "¿Borrar categoría?", num);
  strcpy(preguntaGuardada, pregunta);
  confirmar.abrir(preguntaGuardada, "Sí, borrar", [this]() {
    almacenBorrarCategoria(cat);
    guardar();
    uiVolverA(&menu);
    aviso("Categoría borrada");
  });
}

void Acciones::elegir(int i) {
  Servicio *s = almacenServicio(id);
  if (!s) return uiPop();
  switch (opciones[i]) {
    case TECLEAR_USUARIO:
      // Al terminar, la selección pasa sola a teclear la contraseña
      tecleo.abrir("Usuario", s->usuario, [this]() { seleccionarAccion(TECLEAR_CONTRASENA); });
      break;
    case TECLEAR_CONTRASENA:
      tecleo.abrir("Contraseña", s->contrasena, nullptr);
      break;
    case EJECUTAR_GUION: {
      int linea = guionComprobar(almacenGuion(id));
      if (linea) {
        char t[32];
        snprintf(t, sizeof(t), "Guion l%d: %s", linea, guionError());
        return fallo(t);
      }
      int sid = id;
      tecleo.abrir("Guion", [sid]() {
        Servicio *g = almacenServicio(sid);
        return g ? guionEjecutar(almacenGuion(sid), g->usuario, g->contrasena) : 0;
      }, nullptr);
      break;
    }
    case MOSTRAR:
      mostrar.abrir(id);
      break;
    case EDITAR:
      editar.abrir(id);
      break;
    case BORRAR:
      confirmar.abrir("¿Borrar servicio?", "Sí, borrar", [this]() {
        almacenBorrarServicio(id);
        guardar();
        uiVolverA(&servicios);
        aviso("Servicio borrado");
      });
      break;
  }
}

// ---- PIN: lo recoge y se lo pasa a quien lo pidió ----

class PantallaPin : public Pantalla {
  uint8_t cifras[LARGO_PIN];
  int pos = 0;
  const char *nombre = "";
  AlAceptarTexto alAceptar;

  void reiniciar() {
    criptoBorrar(cifras, sizeof(cifras));
    pos = 0;
  }

  void terminar() {
    char pin[LARGO_PIN + 1];
    for (int i = 0; i < LARGO_PIN; i++) pin[i] = '0' + cifras[i];
    pin[LARGO_PIN] = '\0';
    reiniciar();
    AlAceptarTexto cb = alAceptar;  // puede reconfigurar esta pantalla
    cb(pin);
    criptoBorrar(pin, sizeof(pin));
  }

 public:
  void configurar(const char *titulo, AlAceptarTexto cb) {
    nombre = titulo;
    alAceptar = cb;
    reiniciar();
    sucio = true;
  }

  void abrir(const char *titulo, AlAceptarTexto cb) {
    configurar(titulo, cb);
    uiPush(this);
  }

  void tecla(Tecla t) override {
    switch (t) {
      case ARRIBA: cifras[pos] = (cifras[pos] + 1) % 10; break;
      case ABAJO: cifras[pos] = (cifras[pos] + 9) % 10; break;
      case DERECHA:
      case CENTRO:
        if (pos < LARGO_PIN - 1) pos++;
        else terminar();
        break;
      case IZQUIERDA:
        if (pos > 0) pos--;
        else uiPop();
        break;
      default:
        break;
    }
  }

  void dibujar() override {
    pantallaCabecera(nombre, "centro: sig.");
    oled.setFont(u8g2_font_7x13B_tf);
    const int PASO = 18, ANCHO = 14;
    const int inicio = (128 - ((LARGO_PIN - 1) * PASO + ANCHO)) / 2;
    for (int i = 0; i < LARGO_PIN; i++) {
      int x = inicio + i * PASO;
      char c[2] = {i < pos ? '*' : i == pos ? (char)('0' + cifras[i]) : '-', '\0'};
      if (i == pos) {
        oled.drawBox(x, 11, ANCHO, 19);
        oled.setDrawColor(0);
      } else {
        oled.drawFrame(x, 11, ANCHO, 19);
      }
      oled.drawStr(x + 4, 25, c);
      oled.setDrawColor(1);
    }
  }
};
static PantallaPin pantallaPin;

// ---- Bloqueo ----

static void pedirPin();
static void pedirRecuperacion();

class PantallaBloqueo : public Pantalla {
  int ultimoSegundo = -1;
  uint32_t ultimaHuella = 0;

  // La línea de abajo alterna cada 3 s entre la ayuda y el estado del
  // pomodoro y de «mantener sesión», si están en marcha
  enum Linea : uint8_t { AYUDA, LINEA_POMODORO, LINEA_MANTENER };
  Linea lineaActual() {
    Linea lineas[3];
    int n = 0;
    lineas[n++] = AYUDA;
    if (pomodoroEstado() != POMO_INACTIVO) lineas[n++] = LINEA_POMODORO;
    if (mantenerActivo()) lineas[n++] = LINEA_MANTENER;
    return lineas[(millis() / 3000) % n];
  }

  int segundosEspera() {
    int32_t queda = (int32_t)(esperaPinHastaMs - millis());
    return queda > 0 ? (queda + 999) / 1000 : 0;
  }

 public:
  void tecla(Tecla t) override {
    // Ni el pomodoro, ni «mantener sesión», ni el azar necesitan la bóveda
    if (t == ARRIBA) return uiPush(&pantallaPomodoro);
    if (t == ABAJO) return uiPush(&menuMantener);
    if (t == IZQUIERDA) return uiPush(&menuAzar);
    if (segundosEspera()) return;
    if (t == CENTRO || t == DERECHA) pedirPin();
    else if (t == CENTRO_LARGO) pedirRecuperacion();
  }

  void tick() override {
    int s = segundosEspera();
    if (s != ultimoSegundo) {
      ultimoSegundo = s;
      sucio = true;
    }
    uint32_t h = huellaPomodoro() * 4 + lineaActual() + mantenerRestanteMs() / 60000 * 64 + mantenerActivo() * 2;
    if (h != ultimaHuella) {
      ultimaHuella = h;
      sucio = true;
    }
  }

  void dibujar() override {
    oled.setFont(FUENTE_GRANDE);
    pantallaCentrado(15, "Ardutype");
    oled.setFont(FUENTE_PEQUENA);
    char t[32];
    int s = segundosEspera();
    if (s) snprintf(t, sizeof(t), "Espera %d s", s);
    else if (lineaActual() == LINEA_POMODORO) textoPomodoro(t, sizeof(t));
    else if (lineaActual() == LINEA_MANTENER) textoMantener(t, sizeof(t));
    else snprintf(t, sizeof(t), "Centro: PIN. Largo: clave");
    pantallaCentrado(29, t);
  }
};
static PantallaBloqueo pantallaBloqueo;

// ---- Primer arranque ----

class Bienvenida : public Pantalla {
 public:
  void tecla(Tecla t) override;
  void dibujar() override {
    pantallaCabecera("Ardutype nuevo");
    char t[24];
    snprintf(t, sizeof(t), "Crea un PIN de %u", LARGO_PIN);
    pantallaFila(0, t, false);
    pantallaFila(1, "cifras. Centro: ir", false);
  }
};
static Bienvenida bienvenida;

// Sin partición de ficheros no hay dónde guardar: hay que elegirla al subir
class SinMemoria : public Pantalla {
 public:
  void tecla(Tecla) override {}
  void dibujar() override {
    // El Flash Size ya se comprueba al compilar: o está sin formatear (placa
    // nueva) o el sistema de ficheros está dañado. No se formatea solo.
    pantallaCabecera("Memoria no legible");
    pantallaFila(0, "Sin formato o dañada", false);
    pantallaFila(1, "formatear_memoria", false);
  }
};
static SinMemoria sinMemoria;

// La clave de recuperación se enseña una sola vez, al crear la bóveda
static char textoRecuperacion[TAM_TEXTO_RECUPERACION];
static void entrarAlMenu();

// Dos palabras por página, numeradas; solo se confirma desde la última
class VerRecuperacion : public Pantalla {
  static const int PAGINAS = (PALABRAS_RECUPERACION + 1) / 2;
  int pagina = 0;

  // Copia la palabra n (desde 0) en destino
  static void palabra(int n, char *destino, size_t tam) {
    const char *p = textoRecuperacion;
    for (int i = 0; i < n && p; i++) {
      p = strchr(p, ' ');
      if (p) p++;
    }
    size_t largo = p ? strcspn(p, " ") : 0;
    if (largo >= tam) largo = tam - 1;
    if (p) memcpy(destino, p, largo);
    destino[largo] = '\0';
  }

 public:
  bool apagable() override { return false; }  // ni se apaga ni se bloquea sola

  void abrir() {
    pagina = 0;
    uiRaiz(this);
  }

  void tecla(Tecla t) override {
    if ((t == DERECHA || t == ABAJO) && pagina < PAGINAS - 1) pagina++;
    else if ((t == IZQUIERDA || t == ARRIBA) && pagina > 0) pagina--;
    else if (t == CENTRO_LARGO && pagina == PAGINAS - 1)
      confirmar.abrir("¿La has apuntado?", "Sí, en papel", []() {
        criptoBorrar(textoRecuperacion, sizeof(textoRecuperacion));
        entrarAlMenu();
      });
    sucio = true;
  }

  void dibujar() override {
    char cab[16], pal[12], linea[24];
    snprintf(cab, sizeof(cab), "%d/%d %s", pagina + 1, PAGINAS, pagina == PAGINAS - 1 ? "largo: ok" : "der: más");
    pantallaCabecera("Clave recup.", cab);
    for (int fila = 0; fila < 2; fila++) {
      int n = pagina * 2 + fila;
      if (n >= PALABRAS_RECUPERACION) break;
      palabra(n, pal, sizeof(pal));
      snprintf(linea, sizeof(linea), "%d. %s", n + 1, pal);
      pantallaFila(fila, linea, false);
    }
  }
};
static VerRecuperacion verRecuperacion;

static void bloquear() {
  if (bovedaEstado() == BOVEDA_ABIERTA) Serial.println("Bloqueado");
  adminCancelar();
  bovedaCerrar();
  switch (bovedaEstado()) {
    case BOVEDA_SIN_FS: uiRaiz(&sinMemoria); break;
    case BOVEDA_NUEVA: uiRaiz(&bienvenida); break;
    default: uiRaiz(&pantallaBloqueo); break;
  }
}

static void entrarAlMenu() {
  fallosPin = 0;
  Serial.println("Desbloqueado");
  ledOk();
  if (aceptarTrasPin) {  // el PIN era para aceptar una pregunta del PC
    aceptarTrasPin = false;
    notiResponder(true);
  }
  uiRaiz(&menu);
}

// PIN o clave de recuperación incorrectos
static void registrarFallo(const char *texto = "Incorrecto") {
  Serial.println("Clave incorrecta");
  if (++fallosPin >= INTENTOS_PIN) {
    fallosPin = 0;
    esperaPinHastaMs = millis() + ESPERA_PIN_MS;
    uiRaiz(&pantallaBloqueo);
    fallo("Demasiados fallos");
  } else {
    fallo(texto);
  }
}

// Con el chip, el PIN incorrecto dice cuántos intentos le quedan
static void registrarFalloPin() {
  char texto[24];
  uint32_t quedan;
  if (bovedaIntentosChip(quedan)) snprintf(texto, sizeof(texto), "Incorrecto. Quedan %lu", (unsigned long)quedan);
  else snprintf(texto, sizeof(texto), "Incorrecto");
  registrarFallo(texto);
}

static void abrirDanada() {
  uiRaiz(&pantallaBloqueo);
  fallo("Almacén dañado");
}

static void pedirPin() {
  pantallaPin.abrir("PIN", [](const char *pin) {
    pintarAhora("PIN", "Comprobando...");
    switch (bovedaAbrirConPin(pin)) {
      case APERTURA_OK:
        aplicarAjustes();
        entrarAlMenu();
        break;
      case APERTURA_CLAVE_MAL:
        registrarFalloPin();
        break;
      case APERTURA_DANADA:
        abrirDanada();
        break;
      case APERTURA_SIN_CHIP:
        uiRaiz(&pantallaBloqueo);
        fallo("Falta el chip");
        break;
      case APERTURA_SIN_INTENTOS:
        // Solo queda la clave de recuperación (centro largo), que los repone
        uiRaiz(&pantallaBloqueo);
        fallo("PIN agotado: clave");
        break;
    }
  });
}

// PIN nuevo pedido dos veces; con los dos iguales se llama a alTenerPinNuevo
static char pinNuevo[LARGO_PIN + 1];
static AlAceptarTexto alTenerPinNuevo;

static void pinNuevoPrimero() {
  pantallaPin.configurar("PIN nuevo", [](const char *pin) {
    memcpy(pinNuevo, pin, sizeof(pinNuevo));
    pantallaPin.configurar("Repite el PIN", [](const char *repetido) {
      if (!criptoIguales(repetido, pinNuevo, LARGO_PIN)) {
        criptoBorrar(pinNuevo, sizeof(pinNuevo));
        fallo("No coinciden");
        pinNuevoPrimero();
        return;
      }
      AlAceptarTexto cb = alTenerPinNuevo;
      cb(pinNuevo);
      criptoBorrar(pinNuevo, sizeof(pinNuevo));
    });
  });
}

static void pedirPinNuevo(AlAceptarTexto cb) {
  alTenerPinNuevo = cb;
  uiPush(&pantallaPin);
  pinNuevoPrimero();
}

static void guardarPinNuevo(const char *pin) {
  pintarAhora("PIN nuevo", "Guardando...");
  bool ok = bovedaCambiarPin(pin);
  uiRaiz(&menu);
  if (!ok) return fallo("Error al guardar");
  ledOk();
  aviso("PIN cambiado");
}

void Bienvenida::tecla(Tecla t) {
  if (t != CENTRO && t != DERECHA) return;
  pedirPinNuevo([](const char *pin) {
    pintarAhora("Preparando", "Calibrando cifrado...");
    if (!bovedaCrear(pin, textoRecuperacion)) {
      uiRaiz(&bienvenida);
      return fallo("Error al crear");
    }
    Serial.println("Bóveda creada");
    verRecuperacion.abrir();
  });
}

// Con la clave de recuperación se entra y se obliga a poner un PIN nuevo
static void abrirConRecuperacion(const char *clave);

static void pedirRecuperacion() {
  if (bovedaRecuperacionConPalabras())
    ruedaPalabras.abrir("Recuperación", PALABRAS_RECUPERACION, abrirConRecuperacion);
  else  // bóvedas anteriores: clave base32
    rueda.abrir("Clave de recuperación", "", 40, false, abrirConRecuperacion, CLAVE_RECUPERACION);
}

static void abrirConRecuperacion(const char *clave) {
  pintarAhora("Recuperación", "Comprobando...");
  switch (bovedaAbrirConRecuperacion(clave)) {
    case APERTURA_OK:
      aplicarAjustes();
      entrarAlMenu();
      pedirPinNuevo(guardarPinNuevo);
      break;
    case APERTURA_CLAVE_MAL:
      registrarFallo();
      break;
    default:  // la recuperación no usa el chip
      abrirDanada();
      break;
  }
}

void MenuAjustes::elegir(int i) {
  Ajustes &a = aj();
  switch (i) {
    case 0: a.distribucion = (a.distribucion + 1) % NUM_DISTRIBUCIONES; break;
    case 1: a.retardo = (a.retardo + 1) % NUM(RETARDOS_MS); break;
    case 2: a.bloqueo = (a.bloqueo + 1) % NUM(BLOQUEOS_MIN); break;
    case 3: a.brillo = (a.brillo + 1) % NUM(CONTRASTES); break;
    case 4: a.led = !a.led; break;
    case 5:
      pantallaPin.abrir("PIN actual", [](const char *pin) {
        pintarAhora("PIN actual", "Comprobando...");
        bool correcto = bovedaComprobarPin(pin);
        uiPop();
        if (!correcto) return fallo("PIN incorrecto");
        pedirPinNuevo(guardarPinNuevo);
      });
      return;
    case 6:
      confirmar.abrir("¿Borrar TODO?", "Sí, borrar todo", []() {
        bovedaBorrarTodo();
        Serial.println("Bóveda borrada");
        bloquear();
        aviso("Todo borrado");
      });
      return;
  }
  aplicarAjustes();
  guardar();
  sucio = true;
}

// ---- PC conectado (admin.cpp) ----

class PantallaPc : public Pantalla {
  char texto[32] = "";
  const char *pregunta = nullptr;
  const char *textoSi = "";
  void (*respuesta)(bool) = nullptr;
  int sel = 0;

  void responder(bool si) {
    void (*cb)(bool) = respuesta;
    pregunta = nullptr;
    cb(si);
  }

 public:
  void ponerTexto(const char *t) {
    almacenCopiar(texto, sizeof(texto), t);
    sucio = true;
  }

  void preguntar(const char *p, const char *si, void (*cb)(bool)) {
    pregunta = p;
    textoSi = si;
    respuesta = cb;
    sel = 0;  // "No" primero
    sucio = true;
  }

  void entrar() override {
    pregunta = nullptr;
    ponerTexto("Esperando órdenes");
  }

  bool apagable() override { return false; }  // la sesión caduca sola (SESION_PC_MS)

  void tecla(Tecla t) override {
    if (pregunta) {
      if (t == ARRIBA || t == ABAJO) sel ^= 1;
      else if (t == CENTRO || t == DERECHA) responder(sel == 1);
      else if (t == IZQUIERDA) responder(false);
      return;
    }
    if (t == IZQUIERDA) {
      adminCancelar();
      uiRaiz(&menu);
      aviso("PC desconectado");
    } else if (t == CENTRO_LARGO) {
      bloquear();
    }
  }

  void dibujar() override {
    if (pregunta) {
      pantallaCabecera(pregunta);
      pantallaFila(0, "No", sel == 0);
      pantallaFila(1, textoSi, sel == 1);
      return;
    }
    pantallaCabecera("PC conectado");
    pantallaFila(0, texto, false);
    pantallaFila(1, "izq: salir", false);
  }
};
static PantallaPc pantallaPc;

bool uiPcAbrir() {
  if (numPila && pila[0] == &pantallaPc) return true;
  if (!numPila || pila[0] != &menu) return false;
  for (int i = 0; i < numPila; i++)
    if (pila[i] == &pantallaPin || pila[i] == &tecleo) return false;
  uiRaiz(&pantallaPc);
  ultimaTeclaMs = millis();
  if (!pantallaEncendida()) {
    pantallaEncender(true);
    ledReposo(false);
  }
  return true;
}

void uiPcTexto(const char *texto) {
  pantallaPc.ponerTexto(texto);
}

void uiPcPreguntar(const char *pregunta, const char *si, void (*respuesta)(bool)) {
  pantallaPc.preguntar(pregunta, si, respuesta);
  ultimaTeclaMs = millis();
  avisoActivo = false;
  if (!pantallaEncendida()) {
    pantallaEncender(true);
    ledReposo(false);
  }
  ledPulsacion();
}

void uiPcCerrar() {
  if (numPila && pila[0] == &pantallaPc) uiRaiz(&menu);
}

// ---- Arranque: el nombre se teclea solo ----

class Arranque : public Pantalla {
  uint32_t inicioMs = 0;

 public:
  void entrar() override { inicioMs = millis(); }
  bool apagable() override { return false; }
  void tecla(Tecla) override { bloquear(); }

  void tick() override {
    uint32_t pasado = millis() - inicioMs;
    if (pasado >= 2200) return bloquear();  // o la bienvenida si no hay bóveda
    sucio = true;
  }

  void dibujar() override {
    static const char NOMBRE[] = "Ardutype";
    uint32_t pasado = millis() - inicioMs;
    int letras = (int)min((uint32_t)strlen(NOMBRE), pasado / 110);
    char parte[sizeof(NOMBRE)];
    memcpy(parte, NOMBRE, letras);
    parte[letras] = '\0';

    oled.setFont(FUENTE_GRANDE);
    int x = (128 - oled.getStrWidth(NOMBRE)) / 2;
    oled.drawStr(x, 17, parte);
    // Cursor de texto parpadeando detrás de la última letra
    if ((pasado / 180) % 2 == 0) oled.drawBox(x + oled.getStrWidth(parte) + 1, 5, 2, 13);
    if (pasado > 1100) {
      oled.setFont(FUENTE_PEQUENA);
      pantallaCentrado(30, "tus claves, tecleadas");
    }
  }
};
static Arranque arranque;

// ---- Notificaciones del PC (notificacion.h) ----

static bool enPila(Pantalla *p) {
  for (int i = 0; i < numPila; i++)
    if (pila[i] == p) return true;
  return false;
}

static void quitarDePila(Pantalla *p) {
  for (int i = 1; i < numPila; i++) {  // nunca la raíz
    if (pila[i] != p) continue;
    memmove(&pila[i], &pila[i + 1], (numPila - i - 1) * sizeof(pila[0]));
    numPila--;
    if (i == numPila) cima()->volver();
    sucio = true;
    return;
  }
}

//
// Se ponen encima de lo que haya, salvo que se esté tecleando (la cuenta atrás
// seguiría a ciegas al volver) o en pantallas que no deben cortarse.
// Texto: arriba/abajo lo desplazan. Aviso: cualquier otra tecla lo quita.
// Pregunta: izquierda deniega; centro o derecha llevan a «No / Sí» (No
// primero). Aceptar con el Ardutype bloqueado pide antes el PIN.

class PantallaNoti : public Pantalla {
  int fila = 0;  // primera fila del texto a la vista
  bool opciones = false;
  int sel = 0;
  uint32_t mostradaMs = 0;
  int ultimoSegundo = -1;

  int numFilas() {
    int n = utf8Contar(notiTexto());
    return max(1, (n + CARACTERES_FILA - 1) / CARACTERES_FILA);
  }

  void aceptar() {
    if (bovedaEstado() == BOVEDA_ABIERTA) return notiResponder(true);
    if ((int32_t)(esperaPinHastaMs - millis()) > 0) return fallo("Espera para el PIN");
    // Si se estaba metiendo el PIN cuando llegó, esa pantalla sobra: una sola en la pila
    if (enPila(&pantallaPin)) quitarDePila(&pantallaPin);
    aceptarTrasPin = true;
    pedirPin();
  }

 public:
  void entrar() override {
    fila = 0;
    opciones = false;
    sel = 0;
    mostradaMs = millis();
  }

  void volver() override {  // tras cancelar el PIN: la pregunta sigue sin contestar
    aceptarTrasPin = false;
    opciones = false;
    sel = 0;
  }

  void tecla(Tecla t) override {
    if (millis() - mostradaMs < NOTI_IGNORAR_TECLAS_MS) return;
    bool pregunta = notiTipo() == NOTI_PREGUNTA;
    if (opciones) {
      if (t == ARRIBA || t == ABAJO) sel ^= 1;
      else if (t == IZQUIERDA) opciones = false;
      else if (t == CENTRO || t == DERECHA) sel ? aceptar() : notiResponder(false);
      return;
    }
    switch (t) {
      case ARRIBA:
        if (fila > 0) fila--;
        break;
      case ABAJO:
        if (fila + 2 < numFilas()) fila++;
        break;
      case IZQUIERDA:
        pregunta ? notiResponder(false) : notiDescartar();
        break;
      case CENTRO:
      case DERECHA:
        if (pregunta) opciones = true;
        else notiDescartar();
        break;
      default:  // el centro largo no bloquea aquí
        break;
    }
  }

  void tick() override {
    int s = (notiRestanteMs() + 999) / 1000;
    if (s != ultimoSegundo) {
      ultimoSegundo = s;
      sucio = true;
    }
  }

  void dibujar() override {
    char derecha[40] = "";
    bool pregunta = notiTipo() == NOTI_PREGUNTA;
    if (opciones) {
      snprintf(derecha, sizeof(derecha), "%ds", ultimoSegundo);
      pantallaCabecera("¿Aceptar?", derecha);
      pantallaFila(0, "No", sel == 0);
      pantallaFila(1, bovedaEstado() == BOVEDA_ABIERTA ? "Sí" : "Sí (pide PIN)", sel == 1);
      return;
    }
    int n = numFilas();
    char pagina[24] = "";
    if (n > 2) snprintf(pagina, sizeof(pagina), "%c%d/%d", fila + 2 < n ? 'v' : ' ', fila + 1, n - 1);
    if (pregunta) snprintf(derecha, sizeof(derecha), "%s %ds", pagina, ultimoSegundo);
    else snprintf(derecha, sizeof(derecha), "%s", pagina);
    pantallaCabecera(notiOrigen(), derecha);
    char tramo[CARACTERES_FILA * 3 + 1];
    for (int f = 0; f < 2 && fila + f < n; f++) {
      utf8Tramo(notiTexto(), (fila + f) * CARACTERES_FILA, CARACTERES_FILA, tramo, sizeof(tramo));
      pantallaFila(f, tramo, false);
    }
  }
};
static PantallaNoti pantallaNoti;
static uint32_t notiAtendida = 0;  // número de la última notificación que ha visto la interfaz

static bool puedeMostrarNoti() {
  EstadoBoveda e = bovedaEstado();
  if (e != BOVEDA_CERRADA && e != BOVEDA_ABIERTA) return false;
  if (pila[0] == &arranque || pila[0] == &pantallaPc || pila[0] == &verRecuperacion) return false;
  return cima() != &tecleo;
}

static void despertarPorNoti() {
  if (atenuada) {
    atenuada = false;
    pantallaContraste(CONTRASTES[aj().brillo]);
  }
  if (!pantallaEncendida()) {
    pantallaEncender(true);
    ledReposo(false);
  }
  avisoActivo = false;
  notiDespertoMs = millis();
  sucio = true;
}

static void sincronizarNoti() {
  if (notiTipo() == NOTI_NINGUNA) {
    aceptarTrasPin = false;
    if (enPila(&pantallaNoti)) quitarDePila(&pantallaNoti);
    return;
  }
  if (notiNumero() != notiAtendida) {  // ha llegado una nueva
    notiAtendida = notiNumero();
    if (enPila(&pantallaNoti)) quitarDePila(&pantallaNoti);
    despertarPorNoti();
  }
  if (aceptarTrasPin && !enPila(&pantallaPin)) aceptarTrasPin = false;  // PIN cancelado o fallido
  if (!aceptarTrasPin && !enPila(&pantallaNoti) && puedeMostrarNoti()) uiPush(&pantallaNoti);
}

// ---- Bucle ----

void uiIniciar() {
  prepararGrupos();
  aplicarAjustes();
  cargarAzar();
  validarAzar();
  ultimaTeclaMs = millis();
  uiRaiz(&arranque);
}

void uiTick() {
  uint32_t ahora = millis();
  Tecla t = joystickLeer();

  if (t != NINGUNA) {
    ultimaTeclaMs = ahora;
    ledPulsacion();
    if (atenuada) {
      atenuada = false;
      pantallaContraste(CONTRASTES[aj().brillo]);
      if (pantallaEncendida()) t = NINGUNA;  // la tecla solo devuelve el brillo
    }
    if (!pantallaEncendida()) {
      pantallaEncender(true);  // la tecla solo despierta
      ledReposo(false);
      sucio = true;
      t = NINGUNA;
    } else if (avisoActivo) {
      avisoActivo = false;  // la tecla solo quita el aviso
      sucio = true;
      t = NINGUNA;
    }
  }
  if (t != NINGUNA) {
    cima()->tecla(t);
    sucio = true;
  }
  sincronizarNoti();
  if (!enPila(&resultado)) resultado.soltar();  // p. ej. tras el bloqueo automático
  cima()->tick();

  if (avisoActivo && (int32_t)(ahora - avisoHastaMs) >= 0) {
    avisoActivo = false;
    sucio = true;
  }

  if (bovedaEstado() == BOVEDA_ABIERTA && cima()->apagable() &&
      ahora - ultimaTeclaMs >= BLOQUEOS_MIN[aj().bloqueo] * 60000UL) {
    // Mirando el pomodoro: se cierra la bóveda, pero la cuenta atrás sigue a la vista
    Pantalla *sigue = cima()->trasBloqueo();
    bloquear();
    if (sigue) {
      uiPush(sigue);
    } else {
      pantallaEncender(false);
      ledReposo(true);
    }
  }
  // Una notificación enciende el OLED un rato, pero no retrasa el bloqueo
  uint32_t actividadMs = (int32_t)(notiDespertoMs - ultimaTeclaMs) > 0 ? notiDespertoMs : ultimaTeclaMs;
  if (pantallaEncendida() && !atenuada && cima()->apagable() &&
      ahora - actividadMs >= APAGAR_PANTALLA_MS) {
    if (cima()->atenuable()) {
      atenuada = true;
      pantallaContraste(CONTRASTES[0]);
    } else {
      pantallaEncender(false);
      ledReposo(true);  // arcoíris mientras está en reposo
    }
  }

  if (sucio && pantallaEncendida()) {
    sucio = false;
    oled.clearBuffer();
    if (avisoActivo) dibujarAviso();
    else cima()->dibujar();
    oled.sendBuffer();
  }
  if (pantallaRevisarBus()) sucio = true;
}
