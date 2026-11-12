#ifndef CONFIG_H
#define CONFIG_H

#include <Arduino.h>

// Lo único de config.h del firmware que usa atecc.cpp
#if defined(ARDUINO_ARCH_RP2040)
const uint8_t PIN_SDA = 0;  // KB2040: TX
const uint8_t PIN_SCL = 1;  // KB2040: RX
#else
const uint8_t PIN_SDA = 4;  // ESP8266: GPIO4 = D2 en NodeMCU y Wemos D1 mini
const uint8_t PIN_SCL = 5;  // GPIO5 = D1
#endif

#endif
