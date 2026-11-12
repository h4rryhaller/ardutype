#include "cripto.h"
#include "almacen.h"

static char categorias[MAX_CATEGORIAS][LARGO_NOMBRE];
static int numCategorias = 0;
static Servicio servicios[MAX_SERVICIOS];
static Borrado borrados[MAX_SERVICIOS];
static int numBorrados = 0;

// Almacén de guiones aparte: cada servicio con guion apunta a una ranura. Una
// ranura está libre si ningún servicio la usa (no hace falta marcarla).
static char guiones[MAX_GUIONES][LARGO_GUION];

static bool ranuraUsada(int r) {
  for (const Servicio &s : servicios)
    if (s.usado && s.guion == r) return true;
  return false;
}

static int ranuraLibre() {
  for (int r = 0; r < MAX_GUIONES; r++)
    if (!ranuraUsada(r)) return r;
  return SIN_GUION;
}

const char *almacenGuion(int id) {
  Servicio *s = almacenServicio(id);
  return s && s->guion != SIN_GUION ? guiones[s->guion] : "";
}

bool almacenPonerGuion(int id, const char *texto) {
  Servicio *s = almacenServicio(id);
  if (!s) return false;
  if (!texto || !*texto) {  // quitar el guion
    if (s->guion != SIN_GUION) {
      int r = s->guion;
      s->guion = SIN_GUION;
      if (!ranuraUsada(r)) criptoBorrar(guiones[r], LARGO_GUION);
    }
    return true;
  }
  if (s->guion == SIN_GUION) {
    int r = ranuraLibre();
    if (r == SIN_GUION) return false;
    s->guion = r;
  }
  almacenCopiar(guiones[s->guion], LARGO_GUION, texto);
  return true;
}

void almacenCopiar(char *campo, size_t tam, const char *texto) {
  size_t n = strlen(texto);
  if (n >= tam) {
    n = tam - 1;
    while (n > 0 && ((uint8_t)texto[n] & 0xC0) == 0x80) n--;  // no partir un carácter
  }
  memcpy(campo, texto, n);
  campo[n] = '\0';
}

void almacenVaciar() {
  criptoBorrar(servicios, sizeof(servicios));
  criptoBorrar(categorias, sizeof(categorias));
  criptoBorrar(borrados, sizeof(borrados));
  criptoBorrar(guiones, sizeof(guiones));
  for (Servicio &s : servicios) s.guion = SIN_GUION;
  numCategorias = 0;
  numBorrados = 0;
}

// ---- Formato serializado ----
// Versión 3:
//   u8 0xA3
//   u8 categorías, y por cada una: u8 largo + nombre
//   u16 servicios (little endian), y por cada uno:
//     u8 categoría, u8 marcas (bit 0 enlazado, bit 1 cambiado, bit 2 con guion),
//     16 bytes de UUID si está enlazado,
//     u8 largo + nombre, u8 largo + usuario, u8 largo + contraseña,
//     u8 índice de guion (en la lista de abajo) si el bit 2 está puesto
//   u8 guiones, y por cada uno: u16 largo + texto (en el orden de los índices)
//   u8 borrados, y por cada uno: 16 bytes de UUID, u8 largo + nombre
// Versión 2 (0xA2): como la 3 pero el guion iba en línea en cada servicio
//   (u8 largo + texto) y no había lista de guiones. Se lee y se reparte en ranuras.
// Versión 1: sin marca inicial, sin marcas ni UUID, sin guion y sin borrados.

static const uint8_t MARCA_V2 = 0xA2, MARCA_V3 = 0xA3;
static const uint8_t ENLAZADO = 1, CAMBIADO = 2, CON_GUION = 4;

static bool ponerTexto(uint8_t *buf, size_t tam, size_t &pos, const char *texto) {
  size_t n = strlen(texto);
  if (n > 255 || pos + 1 + n > tam) return false;
  buf[pos++] = n;
  memcpy(buf + pos, texto, n);
  pos += n;
  return true;
}

static bool ponerTexto16(uint8_t *buf, size_t tam, size_t &pos, const char *texto) {
  size_t n = strlen(texto);
  if (pos + 2 + n > tam) return false;
  buf[pos++] = n & 0xFF;
  buf[pos++] = n >> 8;
  memcpy(buf + pos, texto, n);
  pos += n;
  return true;
}

static bool ponerBytes(uint8_t *buf, size_t tam, size_t &pos, const void *datos, size_t n) {
  if (pos + n > tam) return false;
  memcpy(buf + pos, datos, n);
  pos += n;
  return true;
}

size_t almacenSerializar(uint8_t *buf, size_t tam) {
  size_t pos = 0;
  if (tam < 4) return 0;
  buf[pos++] = MARCA_V3;
  buf[pos++] = numCategorias;
  for (int c = 0; c < numCategorias; c++)
    if (!ponerTexto(buf, tam, pos, categorias[c])) return 0;

  // Lista de guiones en el orden en que aparecen los servicios; cada servicio
  // con guion guarda su índice en esa lista.
  int ranuraDeLista[MAX_GUIONES];
  int nGuiones = 0;

  uint16_t total = almacenTotalServicios();
  if (pos + 2 > tam) return 0;
  buf[pos++] = total & 0xFF;
  buf[pos++] = total >> 8;
  for (const Servicio &s : servicios) {
    if (!s.usado) continue;
    bool conGuion = s.guion != SIN_GUION;
    uint8_t cab[2] = {s.categoria, (uint8_t)((s.enlazado ? ENLAZADO : 0) | (s.cambiado ? CAMBIADO : 0) |
                                             (conGuion ? CON_GUION : 0))};
    if (!ponerBytes(buf, tam, pos, cab, 2)) return 0;
    if (s.enlazado && !ponerBytes(buf, tam, pos, s.uuid, TAM_UUID)) return 0;
    if (!ponerTexto(buf, tam, pos, s.nombre) || !ponerTexto(buf, tam, pos, s.usuario) ||
        !ponerTexto(buf, tam, pos, s.contrasena))
      return 0;
    if (conGuion) {
      if (nGuiones >= MAX_GUIONES || pos + 1 > tam) return 0;
      ranuraDeLista[nGuiones] = s.guion;
      buf[pos++] = nGuiones++;
    }
  }

  if (pos + 1 > tam) return 0;
  buf[pos++] = nGuiones;
  for (int i = 0; i < nGuiones; i++)
    if (!ponerTexto16(buf, tam, pos, guiones[ranuraDeLista[i]])) return 0;

  if (pos + 1 > tam) return 0;
  buf[pos++] = numBorrados;
  for (int i = 0; i < numBorrados; i++)
    if (!ponerBytes(buf, tam, pos, borrados[i].uuid, TAM_UUID) || !ponerTexto(buf, tam, pos, borrados[i].nombre))
      return 0;
  return pos;
}

static bool leerTexto(const uint8_t *buf, size_t largo, size_t &pos, char *dst, size_t tam) {
  if (pos >= largo) return false;
  size_t n = buf[pos++];
  if (n >= tam || pos + n > largo) return false;
  memcpy(dst, buf + pos, n);
  dst[n] = '\0';
  pos += n;
  return true;
}

static bool leerTexto16(const uint8_t *buf, size_t largo, size_t &pos, char *dst, size_t tam) {
  if (pos + 2 > largo) return false;
  size_t n = buf[pos] | (buf[pos + 1] << 8);
  pos += 2;
  if (n >= tam || pos + n > largo) return false;
  memcpy(dst, buf + pos, n);
  dst[n] = '\0';
  pos += n;
  return true;
}

static bool leerBytes(const uint8_t *buf, size_t largo, size_t &pos, void *dst, size_t n) {
  if (pos + n > largo) return false;
  memcpy(dst, buf + pos, n);
  pos += n;
  return true;
}

static bool deserializar(const uint8_t *buf, size_t largo) {
  size_t pos = 0;
  if (largo < 1) return false;
  bool v3 = buf[0] == MARCA_V3;
  bool v2 = buf[0] == MARCA_V2;
  if (v2 || v3) pos++;
  if (pos >= largo) return false;
  int nc = buf[pos++];
  if (nc > MAX_CATEGORIAS) return false;
  for (int c = 0; c < nc; c++) {
    if (!leerTexto(buf, largo, pos, categorias[c], LARGO_NOMBRE)) return false;
    numCategorias++;
  }
  if (pos + 2 > largo) return false;
  int ns = buf[pos] | (buf[pos + 1] << 8);
  pos += 2;
  if (ns > MAX_SERVICIOS) return false;
  int ranurasV2 = 0;  // ranuras ya usadas al repartir guiones en línea (v2)
  for (int i = 0; i < ns; i++) {
    Servicio &s = servicios[i];
    uint8_t marcas = CAMBIADO;  // lo creado en la fase 4 es nuevo para KeePassXC
    if (pos >= largo) return false;
    s.categoria = buf[pos++];
    if ((v2 || v3) && !leerBytes(buf, largo, pos, &marcas, 1)) return false;
    s.enlazado = marcas & ENLAZADO;
    s.cambiado = marcas & CAMBIADO;
    if (s.enlazado && !leerBytes(buf, largo, pos, s.uuid, TAM_UUID)) return false;
    if (s.categoria >= numCategorias || !leerTexto(buf, largo, pos, s.nombre, sizeof(s.nombre)) ||
        !leerTexto(buf, largo, pos, s.usuario, sizeof(s.usuario)) ||
        !leerTexto(buf, largo, pos, s.contrasena, sizeof(s.contrasena)))
      return false;
    if (marcas & CON_GUION) {
      if (v3) {  // índice en la lista que viene después
        if (pos >= largo) return false;
        s.guion = buf[pos++];
      } else {  // v2: guion en línea (u8 de largo) -> a una ranura
        if (ranurasV2 >= MAX_GUIONES || !leerTexto(buf, largo, pos, guiones[ranurasV2], LARGO_GUION))
          return false;
        s.guion = ranurasV2++;
      }
    }
    s.usado = true;
  }
  if (v3) {  // lista de guiones: la ranura i es el índice i
    if (pos >= largo) return false;
    int ng = buf[pos++];
    if (ng > MAX_GUIONES) return false;
    for (int i = 0; i < ng; i++)
      if (!leerTexto16(buf, largo, pos, guiones[i], LARGO_GUION)) return false;
    for (Servicio &s : servicios)  // comprobar los índices
      if (s.usado && s.guion != SIN_GUION && s.guion >= ng) return false;
  }
  if (v2 || v3) {
    if (pos >= largo) return false;
    int nb = buf[pos++];
    if (nb > MAX_SERVICIOS) return false;
    for (int i = 0; i < nb; i++) {
      if (!leerBytes(buf, largo, pos, borrados[i].uuid, TAM_UUID) ||
          !leerTexto(buf, largo, pos, borrados[i].nombre, LARGO_NOMBRE))
        return false;
      numBorrados++;
    }
  }
  return pos == largo;
}

bool almacenDeserializar(const uint8_t *buf, size_t largo) {
  almacenVaciar();
  if (deserializar(buf, largo)) return true;
  almacenVaciar();
  return false;
}

int almacenNumCategorias() {
  return numCategorias;
}

const char *almacenCategoria(int c) {
  return c >= 0 && c < numCategorias ? categorias[c] : "";
}

int almacenBuscarCategoria(const char *nombre) {
  for (int c = 0; c < numCategorias; c++)
    if (!strcmp(categorias[c], nombre)) return c;
  return -1;
}

int almacenNuevaCategoria(const char *nombre) {
  if (numCategorias >= MAX_CATEGORIAS) return -1;
  almacenCopiar(categorias[numCategorias], LARGO_NOMBRE, nombre);
  return numCategorias++;
}

void almacenRenombrarCategoria(int c, const char *nombre) {
  if (c < 0 || c >= numCategorias) return;
  almacenCopiar(categorias[c], LARGO_NOMBRE, nombre);
  for (Servicio &s : servicios)  // en KeePassXC cambian de grupo
    if (s.usado && s.categoria == c) s.cambiado = true;
}

void almacenBorrarCategoria(int c) {
  if (c < 0 || c >= numCategorias) return;
  for (Servicio &s : servicios) {
    if (!s.usado) continue;
    if (s.categoria == c) almacenBorrarServicio(&s - servicios);
    else if (s.categoria > c) s.categoria--;
  }
  for (int i = c; i < numCategorias - 1; i++) memcpy(categorias[i], categorias[i + 1], LARGO_NOMBRE);
  numCategorias--;
  memset(categorias[numCategorias], 0, LARGO_NOMBRE);
}

int almacenContarServicios(int c) {
  int n = 0;
  for (const Servicio &s : servicios) n += s.usado && s.categoria == c;
  return n;
}

int almacenTotalServicios() {
  int n = 0;
  for (const Servicio &s : servicios) n += s.usado;
  return n;
}

int almacenListarServicios(int c, int *ids, int max) {
  int n = 0;
  for (int id = 0; id < MAX_SERVICIOS && n < max; id++) {
    if (!servicios[id].usado || servicios[id].categoria != c) continue;
    // Inserción ordenada por nombre (sin distinguir mayúsculas)
    int j = n++;
    while (j > 0 && strcasecmp(servicios[ids[j - 1]].nombre, servicios[id].nombre) > 0) {
      ids[j] = ids[j - 1];
      j--;
    }
    ids[j] = id;
  }
  return n;
}

Servicio *almacenServicio(int id) {
  if (id < 0 || id >= MAX_SERVICIOS || !servicios[id].usado) return nullptr;
  return &servicios[id];
}

int almacenBuscarUuid(const uint8_t uuid[TAM_UUID]) {
  for (int id = 0; id < MAX_SERVICIOS; id++)
    if (servicios[id].usado && servicios[id].enlazado && !memcmp(servicios[id].uuid, uuid, TAM_UUID)) return id;
  return -1;
}

int almacenNuevoServicio(int c, const char *nombre, const char *usuario, const char *contrasena) {
  if (c < 0 || c >= numCategorias) return -1;
  for (int id = 0; id < MAX_SERVICIOS; id++) {
    Servicio &s = servicios[id];
    if (s.usado) continue;
    s.usado = true;
    s.enlazado = false;
    s.cambiado = true;
    s.categoria = c;
    s.guion = SIN_GUION;
    almacenCopiar(s.nombre, sizeof(s.nombre), nombre);
    almacenCopiar(s.usuario, sizeof(s.usuario), usuario);
    almacenCopiar(s.contrasena, sizeof(s.contrasena), contrasena);
    return id;
  }
  return -1;
}

void almacenOlvidarServicio(int id) {
  if (id < 0 || id >= MAX_SERVICIOS) return;
  Servicio &s = servicios[id];
  int r = s.usado ? s.guion : SIN_GUION;
  criptoBorrar(&s, sizeof(Servicio));
  if (r != SIN_GUION && !ranuraUsada(r)) criptoBorrar(guiones[r], LARGO_GUION);  // ya nadie la usa
}

void almacenBorrarServicio(int id) {
  Servicio *s = almacenServicio(id);
  if (!s) return;
  if (s->enlazado) {
    if (numBorrados == MAX_SERVICIOS) {  // no debería pasar: se pierde el más antiguo
      memmove(borrados, borrados + 1, sizeof(Borrado) * (MAX_SERVICIOS - 1));
      numBorrados--;
    }
    memcpy(borrados[numBorrados].uuid, s->uuid, TAM_UUID);
    memcpy(borrados[numBorrados].nombre, s->nombre, LARGO_NOMBRE);
    numBorrados++;
  }
  almacenOlvidarServicio(id);
}

int almacenPendientes() {
  int n = numBorrados;
  for (const Servicio &s : servicios) n += s.usado && s.cambiado;
  return n;
}

int almacenNumBorrados() {
  return numBorrados;
}

const Borrado *almacenBorrado(int i) {
  return i >= 0 && i < numBorrados ? &borrados[i] : nullptr;
}

void almacenOlvidarPendientes() {
  for (Servicio &s : servicios) s.cambiado = false;
  criptoBorrar(borrados, sizeof(borrados));
  numBorrados = 0;
}
