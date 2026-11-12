#ifndef UI_H
#define UI_H

// Pantallas y navegación. Cada pantalla recibe las teclas del joystick y se
// dibuja en el OLED; se apilan al entrar en un menú y se desapilan al volver.
//
// En los menús: arriba/abajo mueven, derecha o centro eligen, izquierda
// vuelve y el centro mantenido bloquea el Ardutype.
void uiIniciar();  // tras pantallaIniciar(), tecladoIniciar() y almacenIniciar()
void uiTick();

// Pantalla «PC conectado», para admin.cpp. Abrirla deja solo esa pantalla en la
// pila; devuelve false si el usuario está a medias de algo que no se puede
// cortar (la clave de recuperación o un PIN nuevo obligatorio).
bool uiPcAbrir();
void uiPcTexto(const char *texto);
void uiPcPreguntar(const char *pregunta, const char *si, void (*respuesta)(bool));
void uiPcCerrar();  // vuelve al menú

#endif
