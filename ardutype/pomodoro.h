#ifndef POMODORO_H
#define POMODORO_H

#include <Arduino.h>

// Temporizador pomodoro, independiente de la interfaz: sigue contando, pitando
// y marcando el LED aunque se cambie de pantalla o se bloquee el Ardutype.
//
//   Trabajo (25 min, naranja), descanso (5 min, verde), descanso largo
//   (15 min, azul). Pausa: indefinida, el LED parpadea despacio con el color
//   de la fase pausada.
//   Al terminar una fase: un pitido y el LED parpadea deprisa con su color; el
//   pitido se repite cada minuto hasta que se pausa o se empieza otra fase.
enum FasePomodoro : uint8_t { TRABAJO, DESCANSO, DESCANSO_LARGO, NUM_FASES };
enum EstadoPomodoro : uint8_t { POMO_INACTIVO, POMO_CORRIENDO, POMO_PAUSADO, POMO_TERMINADO };

void pomodoroTick();

void pomodoroEmpezar(FasePomodoro fase);
void pomodoroPausar();    // corriendo o terminado: pausa (y silencia)
void pomodoroReanudar();  // pausado con tiempo pendiente
void pomodoroDetener();   // vuelve a inactivo y apaga el LED del pomodoro

EstadoPomodoro pomodoroEstado();
FasePomodoro pomodoroFase();
uint32_t pomodoroRestanteMs();   // corriendo o pausado
uint32_t pomodoroPasadoFinMs();  // terminado: tiempo desde el final
bool pomodoroPuedeReanudar();
FasePomodoro pomodoroSugerida(); // la que toca después de la actual
uint8_t pomodoroTrabajosHechos();

const char *pomodoroNombre(FasePomodoro fase);
uint32_t pomodoroDuracionMs(FasePomodoro fase);

#endif
