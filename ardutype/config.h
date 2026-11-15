#ifndef CONFIG_H
#define CONFIG_H

#include <Arduino.h>

// Flash Size: la bóveda vive en el sistema de ficheros, y su sitio en la flash
// depende de este ajuste. Con otro, el firmware no la encontraría. Se exige al
// compilar, también desde el IDE: «8MB (Sketch: 7MB, FS: 1MB)», o subir.sh.
#if !defined(FS_START) || FS_START != 275771392 || FS_END != 276819968
#error "Flash Size equivocado: elige «8MB (Sketch: 7MB, FS: 1MB)» en Herramientas, o usa subir.sh"
#endif

// ---- Pines (KB2040) ----
// Dos buses I2C. Compartiendo uno, el tráfico del OLED despertaba al ATECC608
// y los envíos a la pantalla se quedaban colgados (2026-11-12).
// I2C0 en TX/RX, solo el ATECC608 (pull-ups de 4,7 kΩ a 3V3, a 100 kHz):
const uint8_t PIN_SDA = 0;  // TX
const uint8_t PIN_SCL = 1;  // RX
// I2C1 en A0/A1, el OLED (su módulo lleva las pull-ups):
#define BUS_OLED Wire1
const uint8_t PIN_SDA_OLED = 26;  // A0
const uint8_t PIN_SCL_OLED = 27;  // A1

// Joystick: común a GND, INPUT_PULLUP. Mapeado con el polímetro.
const uint8_t PIN_ARRIBA = 2;
const uint8_t PIN_DERECHA = 3;
const uint8_t PIN_IZQUIERDA = 4;
const uint8_t PIN_ABAJO = 5;
const uint8_t PIN_CENTRO = 6;

// Zumbador pasivo (tone) en D8, a GND a través de una resistencia (Ardutype_diseno.md, 3)
const uint8_t PIN_ZUMBADOR = 8;

// ---- Desbloqueo ----
// Con el ATECC608 puesto, el chip limita los intentos del PIN (boveda.h).
// Con 6 cifras y como mucho 41 intentos, acertar es un 0,004 %. Sin el chip,
// cualquier PIN se rompe enseguida copiando la flash.
const uint8_t LARGO_PIN = 6;
const uint32_t INTENTOS_CHIP = 10;      // mínimo que deja el chip tras entrar (quedan de 10 a 41)
const uint32_t OBJETIVO_KDF_MS = 1000;  // lo que tarda en comprobar el PIN
const uint8_t DISTRIBUCION_INICIAL = 0; // ES Linux
const uint8_t INTENTOS_PIN = 3;         // fallos seguidos antes de la espera
const uint32_t ESPERA_PIN_MS = 30000;

// ---- Joystick ----
const uint32_t ANTIRREBOTE_MS = 25;
const uint32_t PULSACION_LARGA_MS = 600;     // centro mantenido
const uint32_t REPETICION_INICIO_MS = 400;   // arriba/abajo/izquierda mantenidos
const uint32_t REPETICION_MS = 110;
const uint32_t REPETICION_RAPIDA_TRAS_MS = 1500;
const uint32_t REPETICION_RAPIDA_MS = 40;

// ---- Interfaz ----
const uint32_t CUENTA_ATRAS_MS = 3000;       // antes de teclear
const uint32_t MOSTRAR_MS = 15000;           // contraseña visible en el OLED
const uint32_t APAGAR_PANTALLA_MS = 60000;   // sin tocar nada se apaga el OLED
const uint32_t AVISO_MS = 1500;
const uint8_t LARGO_GENERADA = 16;           // contraseñas generadas

// ---- Pomodoro ----
const uint32_t POMODORO_TRABAJO_MS = 25 * 60000UL;
const uint32_t POMODORO_DESCANSO_MS = 5 * 60000UL;
const uint32_t POMODORO_LARGO_MS = 15 * 60000UL;
const uint8_t POMODORO_TRABAJOS_POR_LARGO = 4;   // tras 4 trabajos se sugiere el descanso largo
const uint32_t POMODORO_REPETIR_AVISO_MS = 60000; // al terminar, pitido cada minuto
const uint16_t PITIDO_HZ = 2000;
const uint16_t PITIDO_MS = 250;

// ---- Mantener sesión ----
// Un movimiento de ratón de 1 px (y vuelta) cada 30 s: el PC no se bloquea
// por inactividad. Los sistemas permiten como mínimo 1 min de espera.
const uint32_t MANTENER_INTERVALO_MS = 30000;

// ---- Administración desde el PC ----
const uint32_t SESION_PC_MS = 60000;         // sin órdenes, se cierra la pantalla del PC

#endif
