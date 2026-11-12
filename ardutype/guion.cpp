#include <Keyboard.h>

#include "teclado.h"
#include "guion.h"

static char descripcionError[32];

struct Nombre {
  const char *nombre;
  uint8_t valor;
};

static const Nombre TECLAS[] = {
  {"intro", KEY_RETURN}, {"tab", KEY_TAB}, {"esc", KEY_ESC}, {"espacio", ' '},
  {"supr", KEY_DELETE}, {"retro", KEY_BACKSPACE}, {"insert", KEY_INSERT},
  {"arriba", KEY_UP_ARROW}, {"abajo", KEY_DOWN_ARROW}, {"izq", KEY_LEFT_ARROW}, {"der", KEY_RIGHT_ARROW},
  {"inicio", KEY_HOME}, {"fin", KEY_END}, {"repag", KEY_PAGE_UP}, {"avpag", KEY_PAGE_DOWN}, {"menu", KEY_MENU},
  {"f1", KEY_F1}, {"f2", KEY_F2}, {"f3", KEY_F3}, {"f4", KEY_F4}, {"f5", KEY_F5}, {"f6", KEY_F6},
  {"f7", KEY_F7}, {"f8", KEY_F8}, {"f9", KEY_F9}, {"f10", KEY_F10}, {"f11", KEY_F11}, {"f12", KEY_F12},
};
static const Nombre MODIFICADORES[] = {
  {"ctrl", TMOD_CTRL}, {"alt", TMOD_ALT}, {"altgr", TMOD_ALTGR}, {"mays", TMOD_MAYS}, {"super", TMOD_SUPER},
};

// Compara sin distinguir mayúsculas una porción [ini, fin) con un nombre
static bool igual(const char *ini, const char *fin, const char *nombre) {
  for (; ini < fin && *nombre; ini++, nombre++)
    if (tolower((unsigned char)*ini) != *nombre) return false;
  return ini == fin && !*nombre;
}

static int buscar(const Nombre *tabla, int n, const char *ini, const char *fin) {
  for (int i = 0; i < n; i++)
    if (igual(ini, fin, tabla[i].nombre)) return tabla[i].valor;
  return -1;
}
#define NUM(a) (int)(sizeof(a) / sizeof(a[0]))

// Un token «ctrl+alt+t» -> modificadores y tecla (0 si solo hay modificadores)
static bool analizarTecla(const char *ini, const char *fin, uint8_t &mods, uint8_t &tecla) {
  mods = 0;
  tecla = 0;
  bool hayTecla = false;
  if (fin > ini && fin[-1] == '+') return false;  // «ctrl+» sin tecla
  while (ini < fin) {
    const char *sep = ini;
    while (sep < fin && *sep != '+') sep++;
    if (sep == ini) return false;  // vacío (p. ej. «ctrl+»)
    int m = buscar(MODIFICADORES, NUM(MODIFICADORES), ini, sep);
    if (m >= 0) {
      mods |= m;
    } else if (hayTecla) {
      return false;  // dos teclas principales
    } else {
      int t = buscar(TECLAS, NUM(TECLAS), ini, sep);
      if (t >= 0) tecla = t;
      else if (sep - ini == 1 && (unsigned char)*ini >= 0x20 && (unsigned char)*ini < 0x7F) tecla = *ini;
      else return false;
      hayTecla = true;
    }
    ini = sep < fin ? sep + 1 : sep;
  }
  return mods || hayTecla;
}

// Teclea el texto de una línea ESCRIBIR sustituyendo $usuario y $contrasena
static size_t escribir(const char *texto, const char *usuario, const char *contrasena) {
  char buf[256];
  size_t n = 0;
  for (const char *p = texto; *p && n < sizeof(buf) - 1;) {
    const char *valor = nullptr;
    if (!strncmp(p, "$usuario", 8)) {
      valor = usuario;
      p += 8;
    } else if (!strncmp(p, "$contrasena", 11)) {
      valor = contrasena;
      p += 11;
    }
    if (valor) {
      while (*valor && n < sizeof(buf) - 1) buf[n++] = *valor++;
    } else {
      buf[n++] = *p++;
    }
  }
  buf[n] = '\0';
  return tecladoEscribir(buf);
}

// Recorre el guion llamando a la acción adecuada. Si ejecutar es false, solo
// comprueba la sintaxis. Devuelve 0 si todo bien; si no, el número de línea del
// error (con ejecutar=false) o 0 (con ejecutar=true, que no comprueba).
static int recorrer(const char *guion, bool ejecutar, const char *usuario, const char *contrasena,
                    size_t *saltados) {
  int numLinea = 0;
  const char *p = guion;
  while (*p) {
    numLinea++;
    const char *fin = p;
    while (*fin && *fin != '\n') fin++;

    const char *ini = p;
    while (ini < fin && (*ini == ' ' || *ini == '\t' || *ini == '\r')) ini++;
    const char *corte = fin;
    while (corte > ini && (corte[-1] == ' ' || corte[-1] == '\t' || corte[-1] == '\r')) corte--;

    if (ini < corte && *ini != '#') {
      const char *esp = ini;
      while (esp < corte && *esp != ' ') esp++;
      const char *arg = esp;
      while (arg < corte && *arg == ' ') arg++;

      if (igual(ini, esp, "escribir")) {
        if (arg >= corte) {
          snprintf(descripcionError, sizeof descripcionError, "falta texto");
          return numLinea;
        }
        if (ejecutar) {
          char linea[256];
          size_t n = min((size_t)(corte - arg), sizeof(linea) - 1);
          memcpy(linea, arg, n);
          linea[n] = '\0';
          *saltados += escribir(linea, usuario, contrasena);
        }
      } else if (igual(ini, esp, "tecla")) {
        uint8_t mods, tecla;
        if (!analizarTecla(arg, corte, mods, tecla)) {
          snprintf(descripcionError, sizeof descripcionError, "tecla no valida");
          return numLinea;
        }
        if (ejecutar) tecladoCombinacion(mods, tecla);
      } else if (igual(ini, esp, "pausa")) {
        char *tras;
        long ms = strtol(arg, &tras, 10);
        if (tras == arg || ms < 0) {
          snprintf(descripcionError, sizeof descripcionError, "pausa no valida");
          return numLinea;
        }
        if (ejecutar) delay(min((uint32_t)ms, GUION_PAUSA_MAX));
      } else {
        snprintf(descripcionError, sizeof descripcionError, "orden no valida");
        return numLinea;
      }
    }
    p = *fin ? fin + 1 : fin;
  }
  return 0;
}

int guionComprobar(const char *guion) {
  descripcionError[0] = '\0';
  size_t saltados = 0;
  return recorrer(guion, false, "", "", &saltados);
}

const char *guionError() {
  return descripcionError;
}

size_t guionEjecutar(const char *guion, const char *usuario, const char *contrasena) {
  size_t saltados = 0;
  recorrer(guion, true, usuario, contrasena, &saltados);
  return saltados;
}
