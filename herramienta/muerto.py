#!/usr/bin/env python3
"""Guion del interruptor del hombre muerto del Ardutype.

Si nadie toca el joystick durante el plazo de la acción «Guion», el Ardutype
teclea este guion en el PC (misma sintaxis que los guiones de los servicios:
TECLA, PAUSA, ESCRIBIR; ver guiones.md). Va sin cifrar en el aparato, fuera
de la bóveda, para poder ejecutarse aunque esté bloqueado: nada de secretos,
y no admite $usuario ni $contrasena.

Uso:
  muerto.py cargar [ARCHIVO]   (por defecto ~/.config/ardutype/muerto.guion)
  muerto.py ver
  muerto.py borrar

Cargar y borrar piden el Ardutype desbloqueado y confirmarlo en su pantalla.
Las líneas en blanco y los comentarios (#) no se envían.
"""

import argparse
import os
import sys

sys.path.insert(0, os.path.dirname(os.path.realpath(__file__)))
from notificar import Conexion, Fallo, buscar_puerto  # noqa: E402

ARCHIVO = os.path.expanduser("~/.config/ardutype/muerto.guion")
LARGO_MAX = 511          # LARGO_GUION_MUERTO en muerto.h, sin el 0 final
ESPERA_CONFIRMACION = 120

MOTIVOS = {
    "bloqueado": "el Ardutype está bloqueado: desbloquéalo y repite",
    "ocupado": "el Ardutype está ocupado (otra sesión o a medias de algo)",
    "cancelado": "rechazado en el Ardutype",
    "variables": "el guion no puede usar $usuario ni $contrasena",
    "largo": f"el guion no cabe (como mucho {LARGO_MAX} bytes sin comentarios)",
    "guardar": "el Ardutype no ha podido guardarlo",
    "orden": "el firmware no conoce la orden MUERTO: ¿está actualizado?",
}


def compacto(texto):
    lineas = []
    for linea in texto.splitlines():
        linea = linea.strip()
        if linea and not linea.startswith("#"):
            lineas.append(linea)
    return "\n".join(lineas)


def comprobar(r):
    if r is None:
        raise Fallo("el Ardutype no contesta (¿se ha confirmado a tiempo?)")
    if r[0] == "ERR":
        motivo = r[1] if len(r) > 1 else "?"
        if motivo == "sintaxis":
            raise Fallo(f"error de sintaxis en la línea {r[2]} (sin contar comentarios): {' '.join(r[3:])}")
        raise Fallo(MOTIVOS.get(motivo, motivo))
    if r[0] != "OK":
        raise Fallo(f"respuesta inesperada: {' '.join(r)}")


def hola(con):
    r = con.orden("HOLA")
    if r[0] != "ARDUTYPE":
        raise Fallo("no parece un Ardutype")
    if len(r) > 2 and r[2] != "ABIERTA":
        raise Fallo(MOTIVOS["bloqueado"])


def enviar(con, guion):
    hola(con)
    print("Confírmalo en el Ardutype...", file=sys.stderr)
    con.enviar("MUERTO", guion.encode("utf-8").hex() or "-")
    comprobar(con.leer(ESPERA_CONFIRMACION))


def main():
    p = argparse.ArgumentParser(description="Guion del hombre muerto del Ardutype.")
    p.add_argument("orden", choices=["cargar", "ver", "borrar"])
    p.add_argument("archivo", nargs="?", default=ARCHIVO)
    p.add_argument("--puerto", help="puerto serie (por defecto se busca)")
    a = p.parse_args()

    guion = None
    if a.orden == "cargar":
        try:
            with open(a.archivo, encoding="utf-8") as f:
                guion = compacto(f.read())
        except OSError as e:
            sys.exit(f"muerto: no puedo leer {a.archivo}: {e.strerror}")
        if not guion:
            sys.exit("muerto: el guion está vacío (para quitarlo, «borrar»)")
        if len(guion.encode("utf-8")) > LARGO_MAX:
            sys.exit(f"muerto: {MOTIVOS['largo']}")

    try:
        con = Conexion(a.puerto or buscar_puerto(), espera_turno=10)
        try:
            if a.orden == "ver":
                hola(con)
                r = con.orden("MUERTO")
                if r[0] != "MUERTO":
                    comprobar(r)
                texto = bytes.fromhex(r[1]).decode("utf-8") if len(r) > 1 and r[1] != "-" else ""
                print(texto if texto else "(sin guion)")
            else:
                enviar(con, guion if a.orden == "cargar" else "")
                print("Guion guardado." if guion else "Guion borrado.")
            con.enviar("ADIOS")
        finally:
            con.cerrar()
    except Fallo as e:
        sys.exit(f"muerto: {e}")


if __name__ == "__main__":
    main()
