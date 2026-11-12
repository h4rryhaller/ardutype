#ifndef TECLADO_H
#define TECLADO_H

#include <Arduino.h>

// Teclado USB con distribución configurable. Teclea texto UTF-8, incluidos
// los caracteres españoles que la librería Keyboard del core no cubre
// (ñ, ç, €, ¿, ¡, vocales acentuadas con teclas muertas...).
//
// La distribución es la que tiene configurada el ORDENADOR, no la del teclado
// físico: el dispositivo manda posiciones de tecla y el sistema las traduce.
// ES Windows y ES Linux solo se diferencian en la ~ (AltGr+4 es tecla muerta
// en Windows y directa en Linux).
enum Distribucion : uint8_t {
  DIST_ES_LINUX,
  DIST_ES_WINDOWS,
  DIST_US,
  NUM_DISTRIBUCIONES
};

void tecladoIniciar();
void tecladoDistribucion(Distribucion d);
Distribucion tecladoDistribucionActual();
const char *tecladoNombre(Distribucion d);

// Pausa tras pulsar y tras soltar cada tecla. Más alto si el ordenador pierde letras.
void tecladoRetardo(uint16_t ms);

// Bloq Mayús: el ordenador avisa del estado de su LED. Con la compensación
// activa, si está encendido se apaga antes de teclear y se vuelve a encender al
// terminar, para que el texto salga exactamente como está guardado.
void tecladoCompensarMayusculas(bool activa);
bool tecladoMayusculas();     // estado del LED de Bloq Mayús
bool tecladoLedConocido();    // false hasta que el ordenador manda el estado

// Teclea texto UTF-8. Los caracteres que la distribución no puede escribir se
// saltan; devuelve cuántos fueron (0 = todo tecleado).
size_t tecladoEscribir(const char *utf8);
uint32_t tecladoUltimoNoSoportado();  // código Unicode del último saltado

// Una tecla especial de Keyboard.h (KEY_TAB, KEY_RETURN, KEY_ESC...)
void tecladoTecla(uint8_t tecla);

// Modificadores para tecladoCombinacion (se pueden sumar con |)
enum {
  TMOD_CTRL = 1,
  TMOD_ALT = 2,
  TMOD_ALTGR = 4,
  TMOD_MAYS = 8,
  TMOD_SUPER = 16,
};

// Pulsa los modificadores de mods y, si no es 0, la tecla (código de Keyboard.h),
// y los suelta todos. Para los guiones (guion.h): «ctrl+alt+t», «super»...
void tecladoCombinacion(uint8_t mods, uint8_t tecla);

#endif
