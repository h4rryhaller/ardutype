# Guiones del Ardutype

Un **guion** es una pequeña secuencia que el Ardutype teclea y ejecuta por ti:
automatizar un acceso, abrir una terminal y lanzar un comando, rellenar un
formulario… Es un campo opcional de un servicio.

- Se **escriben y editan solo en KeePassXC**, en un atributo de la entrada.
- En el Ardutype **solo se ejecutan**: en las acciones del servicio aparece
  «Ejecutar guion», con la misma cuenta atrás de 3 segundos que el tecleo.
- Se sincronizan como todo lo demás con `ardutype_sync.py`.

## Dónde se escribe en KeePassXC

En la entrada del servicio: pestaña **Avanzado → Atributos personalizados →
Añadir**.

- **Nombre del atributo:** `ardutype-guion`
- **Valor:** el guion, una orden por línea (ver abajo).

El usuario y la contraseña del guion salen de esa misma entrada (campos
**Usuario** y **Contraseña**), así que no se repiten dentro del guion.

## Sintaxis

Una orden por línea. Las líneas en blanco y las que empiezan por `#` se ignoran.

| Orden | Qué hace |
|-------|----------|
| `ESCRIBIR <texto>` | Teclea el texto tal cual. Admite `$usuario` y `$contrasena`, que se sustituyen por los del servicio. |
| `TECLA <t>[+<t>…]` | Una pulsación, con modificadores opcionales: `TECLA intro`, `TECLA ctrl+alt+t`, `TECLA super`. |
| `PAUSA <ms>` | Espera esos milisegundos (como mucho 10000). |

### Teclas

`intro` `tab` `esc` `espacio` `supr` `retro` `insert`
`arriba` `abajo` `izq` `der` `inicio` `fin` `repag` `avpag` `menu`
`f1` … `f12`, y **una letra o cifra suelta** (`a`, `5`, `.`…).

### Modificadores

`ctrl` `alt` `altgr` `mays` (mayúsculas/shift) `super` (tecla Windows/Super).

Se combinan con `+` y la tecla va al final: `ctrl+c`, `ctrl+alt+supr`,
`mays+tab`. Un modificador suelto también vale: `TECLA super` abre el menú de
aplicaciones en la mayoría de escritorios.

## Equivalencia con DigiSpark / DigiKeyboard

Si vienes de sketches de DigiSpark, así se traducen sus constantes.

Modificadores (`MOD_*`, se suman con `+`):

| DigiKeyboard | Ardutype |
|---|---|
| `MOD_CONTROL_LEFT` / `_RIGHT` | `ctrl` |
| `MOD_SHIFT_LEFT` / `_RIGHT` | `mays` |
| `MOD_ALT_LEFT` | `alt` |
| `MOD_ALT_RIGHT` (AltGr) | `altgr` |
| `MOD_GUI_LEFT` / `_RIGHT` | `super` |

Teclas (`KEY_*`):

| DigiKeyboard | Ardutype |
|---|---|
| `KEY_ENTER` / `KEY_RETURN` | `intro` |
| `KEY_TAB` | `tab` |
| `KEY_ESC` | `esc` |
| `KEY_SPACE` | `espacio` |
| `KEY_BACKSPACE` | `retro` |
| `KEY_DELETE` | `supr` |
| `KEY_INSERT` | `insert` |
| `KEY_ARROW_UP/DOWN/LEFT/RIGHT` | `arriba` / `abajo` / `izq` / `der` |
| `KEY_HOME` / `KEY_END` | `inicio` / `fin` |
| `KEY_PAGE_UP` / `KEY_PAGE_DOWN` | `repag` / `avpag` |
| `KEY_F1` … `KEY_F12` | `f1` … `f12` |
| `KEY_A` … `KEY_Z` | una letra: `a` … `z` |
| `KEY_1` … `KEY_0` | una cifra: `1` … `0` |
| (sin equivalente habitual) | `menu` (tecla de menú contextual) |

Ejemplo: `sendKeyStroke(KEY_ENTER, MOD_CONTROL_LEFT + MOD_SHIFT_LEFT)` →
`TECLA ctrl+mays+intro`.

Dos diferencias de fondo:

- **Modo:** DigiKeyboard junta tecla y modificadores en una llamada; aquí van en
  una línea `TECLA`, con la tecla principal al final (`ctrl+mays+intro`). Los
  modificadores van en cualquier orden.
- **Texto:** para escribir texto, DigiKeyboard usa `print(...)`; aquí, `ESCRIBIR
  ...`. Y donde DigiSpark asume teclado US, el Ardutype respeta la distribución
  que tengas configurada (ES Linux, ES Windows, US).

## Límites y avisos

- **Longitud:** 1023 bytes como mucho (las tildes y la ñ ocupan 2). La
  herramienta avisa si te pasas y envía el servicio **sin** el guion.
- **Cuántos:** hasta 8 servicios pueden tener guion a la vez. Si marcas más, la
  herramienta avisa antes de enviar nada.
- **Teclea a ciegas:** no comprueba qué ventana está activa. Empieza por algo
  inofensivo y usa `PAUSA` para dar tiempo a que carguen las cosas.
- **No oculta rastro:** lo que escriba en una terminal queda en el historial
  del shell salvo que el propio guion lo borre. El Ardutype teclea a la vista.
- **Lo lanzas tú:** el Ardutype nunca ejecuta un guion solo; solo cuando lo
  eliges y pasa la cuenta atrás. Pon solo guiones que entiendas.
- Si el guion tiene un error de sintaxis, el dispositivo no lo ejecuta y avisa
  con el número de línea. La herramienta valida lo mismo antes de enviarlo.

## Ejemplo de prueba

El más sencillo para comprobar que funciona, sin tocar nada del sistema: abre
una terminal y escribe una línea.

Crea en KeePassXC una entrada de prueba:

- **Grupo:** `Pruebas`
- **Título:** `Prueba guion`
- **Usuario:** `mundo`
- **Contraseña:** (cualquier cosa, no se usa aquí)
- **Etiqueta:** `ardutype`
- **Atributo** `ardutype-guion`:

```
# Abre una terminal y saluda, para probar el guion
PAUSA 1500
TECLA ctrl+alt+t
PAUSA 1500
ESCRIBIR echo "Hola $usuario desde el Ardutype"
TECLA intro
```

Luego:

1. Sincroniza: `ardusync` (o `.venv/bin/python ardutype_sync.py <base>.kdbx`).
2. En el Ardutype, entra en `Pruebas → Prueba guion → Ejecutar guion`.
3. Pon el cursor donde no moleste (el escritorio) y espera la cuenta atrás.

Debería abrirse una terminal y aparecer `Hola mundo desde el Ardutype`.

### Un ejemplo más real

Acceso completo a un servicio (usuario, tabulador, contraseña, entrar):

```
ESCRIBIR $usuario
TECLA tab
ESCRIBIR $contrasena
TECLA intro
```
