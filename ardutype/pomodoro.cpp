#include "config.h"
#include "pomodoro.h"
#include "zumbador.h"
#include "led.h"

static const char *const NOMBRES[NUM_FASES] = {"Trabajo", "Descanso", "Descanso largo"};
static const uint32_t COLORES[NUM_FASES] = {0xFF4000, 0x00FF00, 0x0000FF};  // naranja, verde, azul

static EstadoPomodoro estado = POMO_INACTIVO;
static FasePomodoro fase = TRABAJO;
static uint32_t finMs = 0;          // corriendo: cuándo termina; terminado: cuándo terminó
static uint32_t restantePausaMs = 0;
static uint32_t ultimoAvisoMs = 0;
static uint8_t trabajosHechos = 0;
static bool faseContada = false;     // el trabajo en curso ya sumó en trabajosHechos

static void pintarLed() {
  switch (estado) {
    case POMO_CORRIENDO: ledPomodoro(COLORES[fase], LED_POMO_FIJO); break;
    case POMO_PAUSADO: ledPomodoro(COLORES[fase], LED_POMO_LENTO); break;
    case POMO_TERMINADO: ledPomodoro(COLORES[fase], LED_POMO_RAPIDO); break;
    default: ledPomodoro(0, LED_POMO_NADA); break;
  }
}

static void terminar(uint32_t ahora) {
  estado = POMO_TERMINADO;
  finMs = ahora;
  ultimoAvisoMs = ahora;
  if (fase == TRABAJO) trabajosHechos++;
  faseContada = true;
  zumbadorPitido();
  pintarLed();
  Serial.printf("Pomodoro: fin de %s\n", NOMBRES[fase]);
}

void pomodoroTick() {
  uint32_t ahora = millis();
  if (estado == POMO_CORRIENDO && (int32_t)(ahora - finMs) >= 0) {
    terminar(ahora);
  } else if (estado == POMO_TERMINADO && ahora - ultimoAvisoMs >= POMODORO_REPETIR_AVISO_MS) {
    ultimoAvisoMs += POMODORO_REPETIR_AVISO_MS;
    zumbadorPitido();
  }
}

void pomodoroEmpezar(FasePomodoro f) {
  fase = f;
  faseContada = false;
  estado = POMO_CORRIENDO;
  finMs = millis() + pomodoroDuracionMs(f);
  pintarLed();
}

void pomodoroPausar() {
  if (estado == POMO_CORRIENDO) {
    int32_t queda = (int32_t)(finMs - millis());
    restantePausaMs = queda > 0 ? queda : 0;
  } else if (estado == POMO_TERMINADO) {
    restantePausaMs = 0;
  } else {
    return;
  }
  estado = POMO_PAUSADO;
  pintarLed();
}

void pomodoroReanudar() {
  if (!pomodoroPuedeReanudar()) return;
  estado = POMO_CORRIENDO;
  finMs = millis() + restantePausaMs;
  pintarLed();
}

void pomodoroDetener() {
  estado = POMO_INACTIVO;
  trabajosHechos = 0;
  pintarLed();
}

EstadoPomodoro pomodoroEstado() { return estado; }
FasePomodoro pomodoroFase() { return fase; }
uint8_t pomodoroTrabajosHechos() { return trabajosHechos; }
bool pomodoroPuedeReanudar() { return estado == POMO_PAUSADO && restantePausaMs > 0; }

uint32_t pomodoroRestanteMs() {
  if (estado == POMO_PAUSADO) return restantePausaMs;
  if (estado != POMO_CORRIENDO) return 0;
  int32_t queda = (int32_t)(finMs - millis());
  return queda > 0 ? queda : 0;
}

uint32_t pomodoroPasadoFinMs() {
  return estado == POMO_TERMINADO ? millis() - finMs : 0;
}

FasePomodoro pomodoroSugerida() {
  if (fase != TRABAJO) return TRABAJO;
  // Tras el trabajo: descanso largo cada 4 (contando el que está en curso)
  uint8_t hechos = trabajosHechos + (faseContada ? 0 : 1);
  return hechos && hechos % POMODORO_TRABAJOS_POR_LARGO == 0 ? DESCANSO_LARGO : DESCANSO;
}

const char *pomodoroNombre(FasePomodoro f) { return NOMBRES[f]; }

uint32_t pomodoroDuracionMs(FasePomodoro f) {
  switch (f) {
    case TRABAJO: return POMODORO_TRABAJO_MS;
    case DESCANSO: return POMODORO_DESCANSO_MS;
    default: return POMODORO_LARGO_MS;
  }
}
