#ifndef MUERTO_H
#define MUERTO_H

#include <Arduino.h>

// Interruptor del hombre muerto: si nadie toca el joystick durante el plazo de
// una acción, el Ardutype la lanza en el PC. Solo lo reinicia quien tenga el
// aparato (a diferencia del bloqueo por inactividad del PC, que reinicia
// cualquiera que toque el PC, y también «mantener sesión»).
//
// Acciones, cada una activable y con su plazo:
//   - Bloquear el PC: Super+L (Windows de serie; en Linux, asignarlo en el PC).
//   - Suspender el PC: la tecla «Sleep» de control de consumo (logind la
//     atiende aunque la pantalla esté bloqueada; Windows también).
//   - Guion: uno propio (sintaxis de guion.h, sin $usuario ni $contrasena),
//     cargado desde el PC (admin.h, orden MUERTO). Se omite si el bloqueo ya
//     ha saltado en esta ausencia: se teclearía en la pantalla de bloqueo.
// Un minuto antes de cada una (la mitad del plazo si es de 1 min): aviso (pitido, LED y pantalla); cualquier
// pulsación lo reinicia todo. Funciona también con el Ardutype bloqueado.
// Sin USB o con el PC suspendido no hace nada (la acción se da por hecha).
//
// Configuración en /muerto.bin y guion en /muerto.txt, fuera de la bóveda.

enum AccionMuerto : uint8_t { MUERTO_BLOQUEAR, MUERTO_SUSPENDER, MUERTO_GUION, NUM_ACCIONES_MUERTO };

const size_t LARGO_GUION_MUERTO = 512;  // con el 0 final (cabe en una línea de admin.cpp)
const uint16_t MUERTO_PLAZO_MAX_MIN = 480;

void muertoIniciar();  // tras montar LittleFS (bovedaIniciar)
void muertoTick();
void muertoSenal();    // alguien ha tocado el joystick

bool muertoActivo();
void muertoActivar(bool activo);
const char *muertoNombre(AccionMuerto a);
bool muertoAccionActiva(AccionMuerto a);
void muertoAccionActivar(AccionMuerto a, bool activa);
uint16_t muertoPlazoMin(AccionMuerto a);
void muertoPlazo(AccionMuerto a, uint16_t minutos);
// El guion está activo pero su plazo es mayor que el del bloqueo: nunca irá
bool muertoGuionTrasBloqueo();

// Segundos hasta la próxima acción si está en el minuto de aviso; -1 si no
int muertoAvisoSegundos(AccionMuerto &a);
// Cada vez que se lanza (o se omite) una acción: contador y texto para la pantalla
uint32_t muertoEventos();
const char *muertoUltimoEvento();

const char *muertoGuion();
int muertoLineasGuion();
// Comprueba y guarda el guion ("" lo borra). Si falla, devuelve el motivo.
const char *muertoGuardarGuion(const char *guion);

// Lanza la acción ya (para probarla). Devuelve false sin USB o con el PC suspendido.
bool muertoEjecutar(AccionMuerto a);

#endif
