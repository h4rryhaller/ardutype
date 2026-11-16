#!/usr/bin/env python3
"""Notificaciones en el Ardutype: avisos y preguntas de aceptar o denegar.

El Ardutype pita, el LED parpadea en violeta y la pantalla enseña el texto
(arriba/abajo lo desplazan). Funciona también con el Ardutype bloqueado, pero
aceptar una pregunta pide entonces el PIN.

Uso:
  notificar.py aviso    [-o ORIGEN] [--mudo] TEXTO...
  notificar.py quitar   [-o ORIGEN]          (quita el aviso de ese origen, si sigue)
  notificar.py pregunta [-o ORIGEN] [-t SEGUNDOS] [-e SEGUNDOS] [--sin-terminal] TEXTO...
  (TEXTO «-»: se lee de la entrada estándar)

Enlazado como «avisar» o «preguntar», el modo sale del nombre:
  preguntar "¿Borro los registros?" && rm -r registros/

Las preguntas se hacen primero en la terminal (si hay una y el programa está en
primer plano). Si no se contesta en unos segundos (-e, por defecto 10), se
pregunta también en el Ardutype y vale la primera respuesta que llegue, de
cualquiera de los dos. Sin terminal (cron, hooks, en segundo plano) se pregunta
directamente en el Ardutype. -t es el plazo total, contado desde el principio.

Códigos de salida:
  0  aviso entregado / pregunta aceptada
  1  pregunta denegada
  2  sin respuesta a tiempo
  3  error: no hay Ardutype, firmware sin notificaciones, puerto ocupado...
     (en una pregunta con terminal, un error del Ardutype no corta: se sigue
     esperando en la terminal)

Solo usa la biblioteca estándar (sin pyserial), para poder llamarlo desde
cualquier sitio. Bloquea el puerto (flock) mientras lo usa: dos preguntas a la
vez esperan su turno, y la sincronización con KeePassXC también lo respeta.
"""

import argparse
import fcntl
import glob
import os
import select
import sys
import termios
import time
import tty

VID_ADAFRUIT = "239a"
LARGO_ORIGEN = 24   # bytes UTF-8 (notificacion.h, sin el 0 final)
LARGO_TEXTO = 480
MAX_SEGUNDOS = 3600

SI, NO, SIN_RESPUESTA, ERROR = 0, 1, 2, 3


class Fallo(Exception):
    pass


class Ocupado(Fallo):
    """Otro programa usa el Ardutype: puede quedar libre más tarde."""


def buscar_puerto():
    puertos = []
    for tty_dir in sorted(glob.glob("/sys/class/tty/ttyACM*")):
        # .../ttyACMn/device es la interfaz USB; su padre, el dispositivo
        dispositivo = os.path.realpath(os.path.join(tty_dir, "device", ".."))
        try:
            with open(os.path.join(dispositivo, "idVendor")) as f:
                if f.read().strip() == VID_ADAFRUIT:
                    puertos.append("/dev/" + os.path.basename(tty_dir))
        except OSError:
            pass
    if not puertos:
        raise Fallo("no encuentro el Ardutype: ¿está enchufado?")
    if len(puertos) > 1:
        raise Fallo(f"hay varias placas de Adafruit ({', '.join(puertos)}): elige una con --puerto")
    return puertos[0]


def sin_controles(texto):
    return " ".join("".join(" " if ord(c) < 0x20 or c == "\x7f" else c for c in texto).split())


def limpiar(texto, maximo):
    """Lo que la pantalla puede dibujar (Latin-1), sin controles, recortado en bytes UTF-8."""
    texto = "".join(c if ord(c) <= 0xFF else "?" for c in sin_controles(texto))
    datos = texto.encode("utf-8")
    if len(datos) > maximo:
        datos = datos[:maximo - 3].decode("utf-8", errors="ignore").encode("utf-8") + b"..."
    return datos


class Conexion:
    def __init__(self, puerto, espera_turno):
        limite = time.monotonic() + espera_turno
        try:
            self.fd = os.open(puerto, os.O_RDWR | os.O_NOCTTY)
        except OSError as e:
            raise Fallo(f"no puedo abrir {puerto}: {e.strerror}")
        while True:  # el turno: otra pregunta o la sincronización
            try:
                fcntl.flock(self.fd, fcntl.LOCK_EX | fcntl.LOCK_NB)
                break
            except BlockingIOError:
                if time.monotonic() >= limite:
                    os.close(self.fd)
                    raise Ocupado("el puerto del Ardutype está ocupado")
                time.sleep(0.3)
        tty.setraw(self.fd)
        termios.tcflush(self.fd, termios.TCIFLUSH)
        self.resto = b""

    def enviar(self, *palabras):
        os.write(self.fd, (" ".join(palabras) + "\n").encode("ascii"))

    def _siguiente(self):
        """Respuesta ya recibida (línea que empieza por '@') como lista de palabras, o None."""
        while b"\n" in self.resto:
            linea, self.resto = self.resto.split(b"\n", 1)
            linea = linea.decode("utf-8", errors="replace").strip()
            if linea.startswith("@"):
                return linea[1:].split(" ")
        return None

    def recibir(self):
        """Lee lo que haya llegado (llamar cuando select dice que hay datos)."""
        datos = os.read(self.fd, 4096)
        if not datos:
            raise Fallo("el Ardutype se ha desconectado")
        self.resto += datos

    def leer(self, espera):
        """Siguiente respuesta; None si vence."""
        limite = time.monotonic() + espera
        while True:
            r = self._siguiente()
            if r is not None:
                return r
            queda = limite - time.monotonic()
            if queda <= 0:
                return None
            listos, _, _ = select.select([self.fd], [], [], queda)
            if listos:
                self.recibir()

    def orden(self, *palabras):
        self.enviar(*palabras)
        r = self.leer(3)
        if r is None:
            raise Fallo("el Ardutype no responde")
        return r

    def cerrar(self):
        os.close(self.fd)


class Terminal:
    """La terminal de quien ejecuta el programa, si la hay y está en primer plano."""

    @staticmethod
    def abrir():
        try:
            fd = os.open("/dev/tty", os.O_RDWR)
        except OSError:
            return None  # sin terminal: cron, hooks de Claude Code, servicios...
        try:
            if os.tcgetpgrp(fd) != os.getpgrp():
                os.close(fd)  # en segundo plano: leer lo pararía (SIGTTIN)
                return None
        except OSError:
            os.close(fd)
            return None
        return Terminal(fd)

    def __init__(self, fd):
        self.fd = fd
        self.resto = b""

    def escribir(self, texto):
        try:
            os.write(self.fd, texto.encode("utf-8", errors="replace"))
        except OSError:
            pass

    def linea(self):
        """Lee lo que haya. Devuelve una línea completa, None si aún no la hay
        y CERRADA si la terminal se ha cerrado."""
        datos = os.read(self.fd, 1024)
        if not datos:
            return CERRADA
        self.resto += datos
        if b"\n" not in self.resto:
            return None
        linea, self.resto = self.resto.split(b"\n", 1)
        return linea.decode("utf-8", errors="replace").strip().lower()

    def descartar(self):
        """Tira lo que se haya tecleado a medias."""
        try:
            termios.tcflush(self.fd, termios.TCIFLUSH)
        except OSError:
            pass
        self.resto = b""

    def cerrar(self):
        os.close(self.fd)


CERRADA = object()

RESPUESTAS = {"s": SI, "si": SI, "sí": SI, "y": SI, "yes": SI, "n": NO, "no": NO}

MOTIVOS = {
    "orden": "el firmware del Ardutype no tiene notificaciones (súbelo de nuevo)",
    "ocupado": "el Ardutype está ocupado (otra pregunta o sincronizando)",
    "estado": "el Ardutype no está listo (sin bóveda creada)",
}


def comprobar_ok(r):
    if r[0] == "ERR":
        motivo = r[1] if len(r) > 1 else "?"
        clase = Ocupado if motivo == "ocupado" else Fallo
        raise clase(MOTIVOS.get(motivo, motivo))
    if r[0] != "OK":
        raise Fallo(f"respuesta inesperada: {' '.join(r)}")


def quitar(con, origen):
    comprobar_ok(con.orden("QUITAR", origen.hex()))
    return SI


def avisar(con, origen, texto, mudo=False):
    comprobar_ok(con.orden("AVISO", *(["MUDO"] if mudo else []), origen.hex(), texto.hex()))
    return SI


class PreguntaArdutype:
    """Una pregunta en curso en el Ardutype."""

    def __init__(self, puerto, origen, texto, segundos, espera_turno):
        self.con = Conexion(puerto, espera_turno)
        self.ident = str(int.from_bytes(os.urandom(4), "big"))
        try:
            comprobar_ok(self.con.orden("PREGUNTA", self.ident, str(segundos), origen.hex(), texto.hex()))
        except BaseException:
            self.con.cerrar()
            raise
        self.pendiente = True

    def respuesta(self):
        """SI/NO si ha llegado la respuesta a esta pregunta; None si aún no."""
        while True:
            r = self.con._siguiente()
            if r is None:
                return None
            if r[0] == "RESPUESTA" and len(r) == 3 and r[1] == self.ident:
                self.pendiente = False
                return {"SI": SI, "NO": NO}.get(r[2], SIN_RESPUESTA)

    def cerrar(self):
        try:
            if self.pendiente:
                self.con.enviar("RETIRAR", self.ident)
        except OSError:
            pass
        self.con.cerrar()


def preguntar_solo_ardutype(puerto, origen, texto, segundos):
    p = PreguntaArdutype(puerto, origen, texto, segundos, espera_turno=segundos)
    limite = time.monotonic() + segundos + 5
    try:
        while True:
            r = p.respuesta()
            if r is not None:
                return r
            queda = limite - time.monotonic()
            if queda <= 0:
                return SIN_RESPUESTA
            listos, _, _ = select.select([p.con.fd], [], [], queda)
            if listos:
                p.con.recibir()
    finally:
        p.cerrar()


def preguntar_con_terminal(term, puerto_fijo, origen_txt, texto_txt, origen, texto, segundos, espera, silencio):
    """Primero en la terminal; pasados `espera` segundos, también en el Ardutype."""
    inicio = time.monotonic()
    fin = inicio + segundos
    proximo_intento = inicio + espera   # cuándo probar el Ardutype (None: ya no)
    pregunta = None
    aviso_ocupado = False

    def nota(msg):
        if not silencio:
            term.escribir(f"\n({msg})\n¿Aceptar? [s/n]: ")

    term.escribir(f"[{origen_txt}] {texto_txt}\n¿Aceptar? [s/n]"
                  + (f" (en {espera} s también en el Ardutype)" if espera < segundos else "")
                  + ": ")
    try:
        while True:
            ahora = time.monotonic()
            if ahora >= fin:
                term.escribir("\nSin respuesta a tiempo.\n")
                return SIN_RESPUESTA

            if pregunta is None and proximo_intento is not None and ahora >= proximo_intento:
                restan = max(1, int(fin - ahora))
                try:
                    pregunta = PreguntaArdutype(puerto_fijo or buscar_puerto(), origen, texto,
                                                restan, espera_turno=0)
                    nota("preguntando también en el Ardutype")
                except Ocupado as e:
                    proximo_intento = ahora + 1   # se reintenta hasta que quede libre
                    if not aviso_ocupado:
                        aviso_ocupado = True
                        nota(f"{e}: lo vuelvo a intentar")
                except Fallo as e:
                    proximo_intento = None
                    nota(f"Ardutype: {e}; solo en la terminal")

            esperas = [fin]
            if pregunta is None and proximo_intento is not None:
                esperas.append(proximo_intento)
            fds = [term.fd] + ([pregunta.con.fd] if pregunta else [])
            listos, _, _ = select.select(fds, [], [], max(0.0, min(esperas) - time.monotonic()))

            if pregunta and pregunta.con.fd in listos:
                try:
                    pregunta.con.recibir()
                    r = pregunta.respuesta()
                except Fallo as e:
                    pregunta.pendiente = False
                    pregunta.cerrar()
                    pregunta, proximo_intento = None, None
                    nota(f"Ardutype: {e}; solo en la terminal")
                    r = None
                if r is not None:
                    term.descartar()
                    term.escribir({SI: "\nAceptado en el Ardutype.\n",
                                   NO: "\nDenegado en el Ardutype.\n"}.get(r, "\nSin respuesta.\n"))
                    return r

            if term.fd in listos:
                linea = term.linea()
                if linea is CERRADA:       # terminal cerrada: queda el Ardutype
                    if pregunta is None and proximo_intento is None:
                        return SIN_RESPUESTA
                    return preguntar_restante(pregunta, puerto_fijo, origen, texto, fin)
                if linea is None:
                    continue               # aún no hay línea completa
                if linea in RESPUESTAS:
                    return RESPUESTAS[linea]
                term.escribir("Contesta s o n: ")
    finally:
        if pregunta:
            pregunta.cerrar()


def preguntar_restante(pregunta, puerto_fijo, origen, texto, fin):
    """La terminal se cerró: se sigue esperando solo en el Ardutype."""
    restan = max(1, int(fin - time.monotonic()))
    if pregunta is None:
        return preguntar_solo_ardutype(puerto_fijo or buscar_puerto(), origen, texto, restan)
    while True:
        r = pregunta.respuesta()
        if r is not None:
            return r
        queda = fin - time.monotonic()
        if queda <= 0:
            return SIN_RESPUESTA
        listos, _, _ = select.select([pregunta.con.fd], [], [], queda)
        if listos:
            pregunta.con.recibir()


def main():
    nombre = os.path.basename(sys.argv[0])
    modo_por_nombre = {"avisar": "aviso", "preguntar": "pregunta"}.get(nombre)

    p = argparse.ArgumentParser(description="Avisos y preguntas en el Ardutype.")
    if not modo_por_nombre:
        p.add_argument("modo", choices=["aviso", "pregunta", "quitar"])
    p.add_argument("-o", "--origen", default=os.environ.get("ARDUTYPE_ORIGEN", "PC"),
                   help="quién avisa o pregunta (cabecera de la pantalla)")
    p.add_argument("-t", "--tiempo", type=int, default=120,
                   help=f"segundos para contestar una pregunta (1-{MAX_SEGUNDOS}, por defecto 120)")
    p.add_argument("-e", "--espera", type=int, default=int(os.environ.get("ARDUTYPE_ESPERA", "10")),
                   help="segundos en la terminal antes de preguntar también en el Ardutype "
                        "(0: a la vez; por defecto 10)")
    p.add_argument("--mudo", action="store_true", help="aviso sin pitido (solo LED y pantalla)")
    p.add_argument("--sin-terminal", action="store_true",
                   help="preguntar solo en el Ardutype, aunque haya terminal")
    p.add_argument("-p", "--puerto", default=os.environ.get("ARDUTYPE_PUERTO"))
    p.add_argument("-q", "--silencio", action="store_true", help="sin mensajes de error")
    p.add_argument("texto", nargs="*")
    a = p.parse_args()
    modo = modo_por_nombre or a.modo

    texto_original = sys.stdin.read() if a.texto == ["-"] else " ".join(a.texto)
    origen, texto = limpiar(a.origen, LARGO_ORIGEN), limpiar(texto_original, LARGO_TEXTO)
    if modo == "quitar":
        texto = b"-"  # no lleva texto
    if not texto or not origen:
        p.error("el texto y el origen no pueden estar vacíos")
    if not 1 <= a.tiempo <= MAX_SEGUNDOS:
        p.error(f"el tiempo va de 1 a {MAX_SEGUNDOS} segundos")
    if a.espera < 0:
        p.error("la espera no puede ser negativa")

    term = None
    try:
        if modo == "pregunta" and not a.sin_terminal:
            term = Terminal.abrir()
        if term:
            return preguntar_con_terminal(term, a.puerto, sin_controles(a.origen),
                                          sin_controles(texto_original), origen, texto,
                                          a.tiempo, a.espera, a.silencio)
        puerto = a.puerto or buscar_puerto()
        if modo in ("aviso", "quitar"):
            con = Conexion(puerto, espera_turno=10)
            try:
                if modo == "quitar":
                    return quitar(con, origen)
                return avisar(con, origen, texto, a.mudo)
            finally:
                con.cerrar()
        return preguntar_solo_ardutype(puerto, origen, texto, a.tiempo)
    except Fallo as e:
        if not a.silencio:
            print(f"{nombre}: {e}", file=sys.stderr)
        return ERROR
    except KeyboardInterrupt:
        if term:
            term.escribir("\n")
        return SIN_RESPUESTA
    finally:
        if term:
            term.cerrar()


if __name__ == "__main__":
    sys.exit(main())
