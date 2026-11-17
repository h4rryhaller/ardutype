#!/usr/bin/env python3
"""Cifrar, descifrar, firmar y verificar archivos con el Ardutype.

  ardutype-cifrado crear              crea la clave de cifrado en el ATECC (ranura 1)
                                      y escribe la identidad y los destinatarios
  ardutype-cifrado destinatario       muestra el destinatario age1ardutype1…
  ardutype-cifrado cifrar ARCHIVO…    → ARCHIVO.age, para los de destinatarios.txt
  ardutype-cifrado descifrar F.age…   → F (pide confirmarlo en el Ardutype)
  ardutype-cifrado firmar ARCHIVO…    → ARCHIVO.sig (firma SSH, clave SSH del Ardutype)
  ardutype-cifrado verificar ARCHIVO… comprueba ARCHIVO.sig

Cifrar usa age con el plugin age-plugin-ardutype; firmar, ssh-keygen -Y con el
agente SSH del Ardutype (servicio ardutype-ssh). Archivos en ~/.config/ardutype:
  age-identidad.txt   identidad del plugin (no es secreta: solo señala la clave del chip)
  destinatarios.txt   a quién se cifra: el Ardutype y, MUY recomendable, una clave
                      de reserva (age-keygen) guardada fuera, p. ej. en KeePassXC
  ssh.pub, allowed_signers   los de la firma (los crea ardutype-ssh)

OJO: la privada del Ardutype no tiene copia. Sin un destinatario de reserva, lo
cifrado se pierde para siempre si el chip se rompe o se sustituye la clave.
"""

import argparse
import os
import subprocess
import sys

sys.path.insert(0, os.path.dirname(os.path.realpath(__file__)))
from agente_ssh import Fallo, comprobar, con_ardutype, estado  # noqa: E402
from age_plugin_ardutype import comprimir, destinatario, identidad  # noqa: E402

DIR = os.path.expanduser("~/.config/ardutype")
IDENTIDAD = os.path.join(DIR, "age-identidad.txt")
DESTINATARIOS = os.path.join(DIR, "destinatarios.txt")
SSH_PUB = os.path.join(DIR, "ssh.pub")
FIRMANTES = os.path.join(DIR, "allowed_signers")
ESPACIO_FIRMA = "file"


def escribir_ficheros(p33):
    os.makedirs(DIR, exist_ok=True)
    with open(IDENTIDAD, "w") as f:
        f.write("# Identidad del plugin age-plugin-ardutype: señala la clave de cifrado\n"
                "# del Ardutype (ranura 1 del ATECC). No es secreta.\n"
                f"# destinatario: {destinatario(p33)}\n{identidad(p33)}\n")
    otros = []
    if os.path.exists(DESTINATARIOS):
        with open(DESTINATARIOS) as f:
            otros = [x for x in f.read().splitlines() if not x.startswith("age1ardutype1")
                     and not x.startswith("# Ardutype")]
    with open(DESTINATARIOS, "w") as f:
        f.write(f"# Ardutype (ranura 1 del ATECC)\n{destinatario(p33)}\n")
        if otros:
            f.write("\n".join(otros) + "\n")
        else:
            f.write("# Reserva: crea una con «age-keygen», pega aquí su línea «age1…» y guarda\n"
                    "# la privada (AGE-SECRET-KEY-…) en KeePassXC, no en el disco.\n")


def leer_destinatarios():
    try:
        with open(DESTINATARIOS) as f:
            return [x.strip() for x in f if x.strip() and not x.startswith("#")]
    except OSError:
        return []


def crear(_):
    def f(con):
        if estado(con) != "ABIERTA":
            raise Fallo("desbloquea el Ardutype y repite")
        print("Confírmalo en el Ardutype...", file=sys.stderr)
        con.enviar("CIFGENERAR")
        return comprobar(con.leer(60), "CIFPUB")
    p33 = comprimir(con_ardutype(f))
    escribir_ficheros(p33)
    print(destinatario(p33))
    print(f"(identidad en {IDENTIDAD}; destinatarios en {DESTINATARIOS})", file=sys.stderr)


def mostrar_destinatario(_):
    p33 = comprimir(con_ardutype(lambda con: (estado(con), comprobar(con.orden("CIFPUB"), "CIFPUB"))[1]))
    if not os.path.exists(IDENTIDAD):
        escribir_ficheros(p33)
    print(destinatario(p33))


def ejecutar(orden, **kw):
    r = subprocess.run(orden, **kw)
    if r.returncode:
        raise Fallo(f"{orden[0]} ha fallado ({r.returncode})")


def cifrar(a):
    ds = leer_destinatarios()
    if not ds:
        raise Fallo(f"no hay destinatarios en {DESTINATARIOS} («crear» primero)")
    if not any(not d.startswith("age1ardutype1") for d in ds):
        print("Aviso: sin destinatario de reserva; si el Ardutype falla, esto no se podrá descifrar.",
              file=sys.stderr)
    for archivo in a.archivos:
        salida = archivo + ".age"
        if os.path.exists(salida):
            raise Fallo(f"ya existe {salida}")
        ejecutar(["age", "-R", DESTINATARIOS, "-o", salida, archivo])
        print(f"{archivo} → {salida}")


def descifrar(a):
    for archivo in a.archivos:
        if not archivo.endswith(".age"):
            raise Fallo(f"{archivo} no acaba en .age")
        salida = archivo[:-4]
        if os.path.exists(salida):
            raise Fallo(f"ya existe {salida}")
        try:
            ejecutar(["age", "-d", "-i", IDENTIDAD, "-o", salida, archivo])
        except Fallo:
            if os.path.exists(salida) and not os.path.getsize(salida):
                os.unlink(salida)
            raise
        print(f"{archivo} → {salida}")


def firmar(a):
    for archivo in a.archivos:
        if os.path.exists(archivo + ".sig"):
            raise Fallo(f"ya existe {archivo}.sig")
        ejecutar(["ssh-keygen-ardutype", "-q", "-Y", "sign", "-n", ESPACIO_FIRMA, "-f", SSH_PUB, archivo])
        print(f"{archivo} → {archivo}.sig")


def verificar(a):
    malos = 0
    for archivo in a.archivos:
        firma = archivo + ".sig"
        with open(firma, "rb") as f:
            r = subprocess.run(["ssh-keygen", "-Y", "find-principals", "-f", FIRMANTES, "-s", firma],
                               stdin=f, capture_output=True, text=True)
        quien = r.stdout.split()[0] if r.returncode == 0 and r.stdout.split() else None
        if not quien:
            print(f"{archivo}: firma de una clave desconocida o ilegible")
            malos += 1
            continue
        with open(archivo, "rb") as f:
            r = subprocess.run(["ssh-keygen", "-Y", "verify", "-f", FIRMANTES, "-I", quien,
                                "-n", ESPACIO_FIRMA, "-s", firma], stdin=f, capture_output=True, text=True)
        if r.returncode == 0:
            print(f"{archivo}: firma buena de {quien}")
        else:
            print(f"{archivo}: FIRMA INCORRECTA")
            malos += 1
    if malos:
        sys.exit(1)


def main():
    p = argparse.ArgumentParser(description="Cifrar y firmar archivos con el Ardutype.")
    sub = p.add_subparsers(dest="orden", required=True)
    sub.add_parser("crear").set_defaults(f=crear)
    sub.add_parser("destinatario").set_defaults(f=mostrar_destinatario)
    for nombre, f in [("cifrar", cifrar), ("descifrar", descifrar), ("firmar", firmar), ("verificar", verificar)]:
        s = sub.add_parser(nombre)
        s.add_argument("archivos", nargs="+")
        s.set_defaults(f=f)
    a = p.parse_args()
    try:
        a.f(a)
    except Fallo as e:
        sys.exit(f"ardutype-cifrado: {e}")


if __name__ == "__main__":
    main()
