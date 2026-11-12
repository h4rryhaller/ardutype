#ifndef GUION_H
#define GUION_H

#include <Arduino.h>

// Guiones: pequeñas secuencias que el Ardutype teclea y ejecuta, para
// automatizar accesos o pasos repetidos. Se escriben y editan en KeePassXC
// (atributo «ardutype-guion») y se guardan como un campo del servicio; en el
// dispositivo solo se ejecutan, con la misma cuenta atrás que el tecleo.
//
// Una orden por línea; líneas en blanco y «# comentario» se ignoran:
//   ESCRIBIR <texto>        teclea el texto; admite $usuario y $contrasena
//   TECLA <t>[+<t>...]       una pulsación: intro, tab, esc, f5, ctrl+alt+t...
//   PAUSA <ms>               espera (como mucho GUION_PAUSA_MAX ms)
//
// Teclas: intro, tab, esc, espacio, supr, retro, insert, arriba, abajo, izq,
// der, inicio, fin, repag, avpag, menu, f1..f12, y una letra o cifra suelta.
// Modificadores: ctrl, alt, altgr, mays, super.

const uint32_t GUION_PAUSA_MAX = 10000;

// Comprueba la sintaxis. Devuelve 0 si está bien, o el número de línea (desde 1)
// del primer error; guionError() da una descripción corta de ese error.
int guionComprobar(const char *guion);
const char *guionError();

// Ejecuta el guion (se supone ya comprobado). Devuelve cuántos caracteres de
// texto no se pudieron teclear con la distribución actual (0 = todo bien).
size_t guionEjecutar(const char *guion, const char *usuario, const char *contrasena);

#endif
