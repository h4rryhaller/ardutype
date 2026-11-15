#ifndef MANTENER_H
#define MANTENER_H

#include <Arduino.h>

// Modo «mantener sesión»: mientras está activo, mueve el ratón 1 px y lo
// devuelve cada MANTENER_INTERVALO_MS para que el PC no bloquee la sesión ni
// apague la pantalla. Independiente de la bóveda: sigue con el Ardutype
// bloqueado. Con duración o sin límite.
//
// Nunca actúa con el PC suspendido: el movimiento podría despertarlo.
void mantenerIniciar();  // arranca el ratón USB (el Ardutype pasa a ser teclado + ratón)
void mantenerTick();

void mantenerActivar(uint32_t duracionMs);  // 0 = sin límite
void mantenerDesactivar();
bool mantenerActivo();
bool mantenerSinLimite();
uint32_t mantenerRestanteMs();

#endif
