#include <Mouse.h>
#include "tusb.h"
#include "config.h"
#include "mantener.h"

static bool activo = false;
static bool sinLimite = false;
static uint32_t finMs = 0;
static uint32_t ultimoMs = 0;

void mantenerIniciar() {
  Mouse.begin();
}

void mantenerTick() {
  if (!activo) return;
  uint32_t ahora = millis();
  if (!sinLimite && (int32_t)(ahora - finMs) >= 0) {
    activo = false;
    Serial.println("Mantener sesión: terminado");
    return;
  }
  if (ahora - ultimoMs < MANTENER_INTERVALO_MS) return;
  ultimoMs = ahora;
  if (!tud_mounted() || tud_suspended()) return;
  Mouse.move(1, 0);
  Mouse.move(-1, 0);
}

void mantenerActivar(uint32_t duracionMs) {
  activo = true;
  sinLimite = duracionMs == 0;
  finMs = millis() + duracionMs;
  ultimoMs = millis() - MANTENER_INTERVALO_MS;  // el primero, ya
  Serial.println("Mantener sesión: activado");
}

void mantenerDesactivar() {
  activo = false;
  Serial.println("Mantener sesión: desactivado");
}

bool mantenerActivo() { return activo; }
bool mantenerSinLimite() { return activo && sinLimite; }

uint32_t mantenerRestanteMs() {
  if (!activo || sinLimite) return 0;
  int32_t queda = (int32_t)(finMs - millis());
  return queda > 0 ? queda : 0;
}
