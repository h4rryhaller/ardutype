#!/usr/bin/env python3
"""Notificaciones en el Ardutype: avisos y preguntas de aceptar o denegar.

El Ardutype pita, el LED parpadea en violeta y la pantalla enseña el texto
(arriba/abajo lo desplazan). Funciona también con el Ardutype bloqueado, pero
aceptar una pregunta pide entonces el PIN.

Uso:
  notificar.py aviso    [-o ORIGEN] TEXTO...
  notificar.py pregunta [-o ORIGEN] [-t SEGUNDOS] TEXTO...
  (TEXTO «-»: se lee de la entrada estándar)

Enlazado como «avisar» o «preguntar», el modo sale del nombre:
  preguntar "¿Borro los registros?" && rm -r registros/

Códigos de salida:
  0  aviso entregado / pregunta aceptada
  1  pregunta denegada
  2  sin respuesta a tiempo
  3  error: no hay Ardutype, firmware sin notificaciones, puerto ocupado...

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


def limpiar(texto, maximo):
    """Lo que la pantalla puede dibujar (Latin-1), sin controles, recortado en bytes UTF-8."""
    texto = "".join(" " if ord(c) < 0x20 or c == "\x7f" else c if ord(c) <= 0xFF else "?" for c in texto)
    texto = " ".join(texto.split())
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
                    raise Fallo("el puerto del Ardutype está ocupado")
                time.sleep(0.3)
        tty.setraw(self.fd)
        termios.tcflush(self.fd, termios.TCIFLUSH)
        self.resto = b""

    def enviar(self, *palabras):
        os.write(self.fd, (" ".join(palabras) + "\n").encode("ascii"))

    def leer(self, espera):
        """Siguiente respuesta (línea que empieza por '@') como lista de palabras; None si vence."""
        limite = time.monotonic() + espera
        while True:
            while b"\n" in self.resto:
                linea, self.resto = self.resto.split(b"\n", 1)
                linea = linea.decode("utf-8", errors="replace").strip()
                if linea.startswith("@"):
                    return linea[1:].split(" ")
            queda = limite - time.monotonic()
            if queda <= 0:
                return None
            listos, _, _ = select.select([self.fd], [], [], queda)
            if listos:
                datos = os.read(self.fd, 4096)
                if not datos:
                    raise Fallo("el Ardutype se ha desconectado")
                self.resto += datos

    def orden(self, *palabras):
        self.enviar(*palabras)
        r = self.leer(3)
        if r is None:
            raise Fallo("el Ardutype no responde")
        return r

    def cerrar(self):
        os.close(self.fd)


MOTIVOS = {
    "orden": "el firmware del Ardutype no tiene notificaciones (súbelo de nuevo)",
    "ocupado": "el Ardutype está ocupado (otra pregunta o sincronizando)",
    "estado": "el Ardutype no está listo (sin bóveda creada)",
}


def comprobar_ok(r):
    if r[0] == "ERR":
        motivo = r[1] if len(r) > 1 else "?"
        raise Fallo(MOTIVOS.get(motivo, motivo))
    if r[0] != "OK":
        raise Fallo(f"respuesta inesperada: {' '.join(r)}")


def avisar(con, origen, texto):
    comprobar_ok(con.orden("AVISO", origen.hex(), texto.hex()))
    return SI


def preguntar(con, origen, texto, segundos):
    ident = str(int.from_bytes(os.urandom(4), "big"))
    comprobar_ok(con.orden("PREGUNTA", ident, str(segundos), origen.hex(), texto.hex()))
    limite = time.monotonic() + segundos + 5
    try:
        while True:
            r = con.leer(max(0.1, limite - time.monotonic()))
            if r is None:
                con.enviar("RETIRAR", ident)
                return SIN_RESPUESTA
            if r[0] == "RESPUESTA" and len(r) == 3 and r[1] == ident:
                return {"SI": SI, "NO": NO}.get(r[2], SIN_RESPUESTA)
    except KeyboardInterrupt:
        con.enviar("RETIRAR", ident)
        raise


def main():
    nombre = os.path.basename(sys.argv[0])
    modo_por_nombre = {"avisar": "aviso", "preguntar": "pregunta"}.get(nombre)

    p = argparse.ArgumentParser(description="Avisos y preguntas en el Ardutype.")
    if not modo_por_nombre:
        p.add_argument("modo", choices=["aviso", "pregunta"])
    p.add_argument("-o", "--origen", default=os.environ.get("ARDUTYPE_ORIGEN", "PC"),
                   help="quién avisa o pregunta (cabecera de la pantalla)")
    p.add_argument("-t", "--tiempo", type=int, default=120,
                   help=f"segundos para contestar una pregunta (1-{MAX_SEGUNDOS}, por defecto 120)")
    p.add_argument("-p", "--puerto", default=os.environ.get("ARDUTYPE_PUERTO"))
    p.add_argument("-q", "--silencio", action="store_true", help="sin mensajes de error")
    p.add_argument("texto", nargs="+")
    a = p.parse_args()
    modo = modo_por_nombre or a.modo

    texto = sys.stdin.read() if a.texto == ["-"] else " ".join(a.texto)
    origen, texto = limpiar(a.origen, LARGO_ORIGEN), limpiar(texto, LARGO_TEXTO)
    if not texto or not origen:
        p.error("el texto y el origen no pueden estar vacíos")
    if not 1 <= a.tiempo <= MAX_SEGUNDOS:
        p.error(f"el tiempo va de 1 a {MAX_SEGUNDOS} segundos")

    try:
        puerto = a.puerto or buscar_puerto()
        con = Conexion(puerto, espera_turno=a.tiempo if modo == "pregunta" else 10)
        try:
            if modo == "aviso":
                return avisar(con, origen, texto)
            return preguntar(con, origen, texto, a.tiempo)
        finally:
            con.cerrar()
    except Fallo as e:
        if not a.silencio:
            print(f"{nombre}: {e}", file=sys.stderr)
        return ERROR
    except KeyboardInterrupt:
        return SIN_RESPUESTA


if __name__ == "__main__":
    sys.exit(main())
