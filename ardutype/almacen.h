#ifndef ALMACEN_H
#define ALMACEN_H

#include <Arduino.h>

// Categorías y servicios, en claro en la RAM solo mientras el Ardutype está
// desbloqueado. La bóveda (boveda.h) los serializa, los cifra y los guarda en la
// flash; al bloquear se borran de la RAM con almacenVaciar().
//
// Sincronización con KeePassXC (admin.h): cada servicio que viene de allí
// lleva el UUID de su entrada. Los cambios hechos en el dispositivo se marcan,
// y los servicios enlazados que se borran aquí se recuerdan, para que la
// herramienta del PC los devuelva a KeePassXC.

const int MAX_CATEGORIAS = 16;
const int MAX_SERVICIOS = 64;
const int LARGO_NOMBRE = 32;  // bytes UTF-8, con el 0 final
const int LARGO_CAMPO = 96;
const int MAX_GUIONES = 8;     // guiones a la vez (se editan solo en KeePassXC)
const int LARGO_GUION = 1024;  // bytes del guion, con el 0 final (prefijo u16)
const int TAM_UUID = 16;
const int SIN_GUION = -1;

struct Servicio {
  bool usado;
  bool enlazado;  // tiene el UUID de una entrada de KeePassXC
  bool cambiado;  // creado o cambiado en el dispositivo desde la última sincronización
  uint8_t categoria;
  int8_t guion;   // ranura del almacén de guiones, o SIN_GUION
  uint8_t uuid[TAM_UUID];
  char nombre[LARGO_NOMBRE];
  char usuario[LARGO_CAMPO];  // "" = sin usuario
  char contrasena[LARGO_CAMPO];
};

// Servicio enlazado borrado en el dispositivo, pendiente de contárselo al PC
struct Borrado {
  uint8_t uuid[TAM_UUID];
  char nombre[LARGO_NOMBRE];
};

void almacenVaciar();  // borra todo de la RAM

// Formato binario que cifra la bóveda. Serializar devuelve 0 si no cabe;
// deserializar devuelve false (y deja el almacén vacío) si los datos no cuadran.
// Lee también el formato 1 (fase 4, sin UUID).
size_t almacenSerializar(uint8_t *buf, size_t tam);
bool almacenDeserializar(const uint8_t *buf, size_t largo);

// Tamaño máximo serializado (formato 3): categorías + servicios (con 1 byte de
// índice de guion) + almacén de guiones aparte + borrados
const size_t MAX_SERIALIZADO = 2 + MAX_CATEGORIAS * LARGO_NOMBRE + 2 +
                               MAX_SERVICIOS * (2 + TAM_UUID + LARGO_NOMBRE + 2 * LARGO_CAMPO + 1) +
                               1 + MAX_GUIONES * (1 + 2 + LARGO_GUION) + 1 +
                               MAX_SERVICIOS * (TAM_UUID + LARGO_NOMBRE);

int almacenNumCategorias();
const char *almacenCategoria(int c);
int almacenBuscarCategoria(const char *nombre);  // -1 si no está
int almacenNuevaCategoria(const char *nombre);   // -1 si no caben más
void almacenRenombrarCategoria(int c, const char *nombre);  // marca sus servicios
void almacenBorrarCategoria(int c);  // con todos sus servicios

// Servicios: un id estable por servicio (no cambia al borrar otros)
int almacenContarServicios(int c);
int almacenTotalServicios();
int almacenListarServicios(int c, int *ids, int max);  // ordenados por nombre
Servicio *almacenServicio(int id);
int almacenBuscarUuid(const uint8_t uuid[TAM_UUID]);  // -1 si no está
int almacenNuevoServicio(int c, const char *nombre, const char *usuario, const char *contrasena);  // -1 si no cabe
void almacenBorrarServicio(int id);  // si estaba enlazado, lo recuerda para el PC
void almacenOlvidarServicio(int id); // sin recordarlo (lo ha quitado el PC)

// Guion de un servicio (almacén aparte). Leer devuelve "" si no tiene.
// Poner un texto vacío lo quita; devuelve false si no quedan ranuras de guion.
const char *almacenGuion(int id);
bool almacenPonerGuion(int id, const char *texto);

// Pendientes de enviar al PC: servicios cambiados y borrados
int almacenPendientes();
int almacenNumBorrados();
const Borrado *almacenBorrado(int i);
void almacenOlvidarPendientes();  // el PC ya los tiene

// Copia texto en un campo sin partir un carácter UTF-8
void almacenCopiar(char *campo, size_t tam, const char *texto);

#endif
