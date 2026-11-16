#ifndef NOTIFICACION_H
#define NOTIFICACION_H

#include <Arduino.h>

// Notificaciones del PC (órdenes AVISO, PREGUNTA y RETIRAR de admin.h; script
// herramienta/notificar.py). Hay como mucho una a la vez:
//   - aviso: un texto; se quita al verlo. Una pregunta lo sustituye.
//   - pregunta: aceptar o denegar, con plazo. La respuesta sale por el serie
//     como «@RESPUESTA <id> SI|NO|TIEMPO». Si el PC cierra el puerto, se retira.
// Pitido y LED violeta al llegar; las preguntas repiten el pitido hasta
// contestarlas. Funcionan con el Ardutype bloqueado, pero aceptar una
// pregunta exige el PIN (ui.cpp).
enum TipoNotificacion : uint8_t { NOTI_NINGUNA, NOTI_AVISO, NOTI_PREGUNTA };

const size_t LARGO_ORIGEN_NOTI = 25;  // bytes UTF-8 con el 0 final
const size_t LARGO_TEXTO_NOTI = 481;

bool notiAvisar(const char *origen, const char *texto);  // false si hay una pregunta
bool notiPreguntar(uint32_t id, uint32_t segundos, const char *origen, const char *texto);
bool notiRetirar(uint32_t id);  // la retira el PC (sin respuesta)

void notiResponder(bool si);  // desde la interfaz
void notiDescartar();         // aviso visto
void notiTick();              // plazo, puerto cerrado, pitido de recuerdo

TipoNotificacion notiTipo();
uint32_t notiNumero();        // cambia con cada notificación nueva
const char *notiOrigen();
const char *notiTexto();
uint32_t notiRestanteMs();    // de la pregunta

#endif
