#ifndef CONFIG_H
#define CONFIG_H

#include <Arduino.h>

// ---- Pines (KB2040) ----
// I2C0 en TX/RX: GPIO12/13 solo salen por el STEMMA QT
const uint8_t PIN_SDA = 0;  // TX
const uint8_t PIN_SCL = 1;  // RX

// Joystick: común a GND, INPUT_PULLUP. Mapeado con el polímetro.
const uint8_t PIN_ARRIBA = 2;
const uint8_t PIN_DERECHA = 3;
const uint8_t PIN_IZQUIERDA = 4;
const uint8_t PIN_ABAJO = 5;
const uint8_t PIN_CENTRO = 6;

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

// ---- Administración desde el PC ----
const uint32_t SESION_PC_MS = 60000;         // sin órdenes, se cierra la pantalla del PC

#endif
