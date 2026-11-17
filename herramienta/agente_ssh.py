#!/usr/bin/env python3
"""Agente SSH con la clave en el ATECC608B del Ardutype.

La clave privada (ECDSA P-256) se crea dentro del chip y nunca sale de él. Este
programa habla el protocolo de ssh-agent por un socket UNIX: cuando ssh o git
piden una firma, calcula el SHA-256 de lo que hay que firmar, se lo pasa al
Ardutype, este pregunta en su pantalla y el chip firma.

Uso:
  agente_ssh.py crear      crea la clave en el Ardutype (confirmándolo en él) y
                           guarda la pública en ~/.config/ardutype/ssh.pub
  agente_ssh.py publica    lee la pública del Ardutype, la guarda y la muestra
  agente_ssh.py agente     sirve el socket ($XDG_RUNTIME_DIR/ardutype-ssh.sock)

Para usarlo: SSH_AUTH_SOCK=<socket> ssh ..., o en ~/.ssh/config
  IdentityAgent ${XDG_RUNTIME_DIR}/ardutype-ssh.sock
Para firmar commits, git con gpg.format=ssh y gpg.ssh.program=ssh-keygen-ardutype.

Con el Ardutype bloqueado, el agente le pone un aviso («Desbloquea para
firmar») y espera hasta ESPERA_DESBLOQUEO segundos a que se abra la bóveda.
"""

import argparse
import base64
import hashlib
import os
import socket
import struct
import sys
import threading
import time

sys.path.insert(0, os.path.dirname(os.path.realpath(__file__)))
from notificar import Conexion, Fallo, avisar, buscar_puerto, quitar  # noqa: E402

RUNTIME = os.environ.get("XDG_RUNTIME_DIR") or f"/tmp/ardutype-{os.getuid()}"
SOCKET = os.path.join(RUNTIME, "ardutype-ssh.sock")
PUBLICA = os.path.expanduser("~/.config/ardutype/ssh.pub")
COMENTARIO = "ardutype"
TIPO = b"ecdsa-sha2-nistp256"
CURVA = b"nistp256"
ORIGEN = b"SSH"
ESPERA_DESBLOQUEO = 60
ESPERA_CONFIRMACION = 60
LARGO_MOTIVO = 19   # cabe en la cabecera del OLED con «¿» y «?»

# Mensajes del protocolo de ssh-agent (draft-miller-ssh-agent)
FAILURE = 5
SUCCESS = 6
REQUEST_IDENTITIES = 11
IDENTITIES_ANSWER = 12
SIGN_REQUEST = 13
SIGN_RESPONSE = 14

MOTIVOS = {
    "bloqueado": "el Ardutype está bloqueado",
    "ocupado": "el Ardutype está ocupado",
    "cancelado": "rechazado en el Ardutype",
    "sinclave": "el Ardutype no tiene clave SSH (agente_ssh.py crear)",
    "atecc": "error del chip ATECC",
    "orden": "el firmware no conoce la orden: ¿está actualizado?",
}


def log(*a):
    print(time.strftime("%H:%M:%S"), *a, file=sys.stderr, flush=True)


# ---- Codificación SSH ----

def cadena(b):
    return struct.pack(">I", len(b)) + b


def mpint(n_bytes):
    n_bytes = n_bytes.lstrip(b"\0")
    if n_bytes and n_bytes[0] & 0x80:
        n_bytes = b"\0" + n_bytes
    return cadena(n_bytes)


class Lector:
    def __init__(self, datos):
        self.d = datos
        self.i = 0

    def u32(self):
        v = struct.unpack_from(">I", self.d, self.i)[0]
        self.i += 4
        return v

    def byte(self):
        v = self.d[self.i]
        self.i += 1
        return v

    def cadena(self):
        n = self.u32()
        v = self.d[self.i:self.i + n]
        if len(v) != n:
            raise ValueError("cadena cortada")
        self.i += n
        return v


def blob_publica(pub64):
    return cadena(TIPO) + cadena(CURVA) + cadena(b"\x04" + pub64)


def linea_publica(pub64):
    return f"{TIPO.decode()} {base64.b64encode(blob_publica(pub64)).decode()} {COMENTARIO}"


def guardar_publica(pub64):
    os.makedirs(os.path.dirname(PUBLICA), exist_ok=True)
    with open(PUBLICA, "w") as f:
        f.write(linea_publica(pub64) + "\n")


def leer_publica():
    """Blob de la pública guardada, o None."""
    try:
        with open(PUBLICA) as f:
            tipo, b64 = f.read().split()[:2]
    except (OSError, ValueError):
        return None
    return base64.b64decode(b64) if tipo == TIPO.decode() else None


# ---- Ardutype ----

def comprobar(r, esperado):
    if r is None:
        raise Fallo("el Ardutype no contesta (¿se ha confirmado a tiempo?)")
    if r[0] == "ERR":
        motivo = r[1] if len(r) > 1 else "?"
        raise Fallo(MOTIVOS.get(motivo, motivo))
    if r[0] != esperado or len(r) < 2:
        raise Fallo("respuesta inesperada: " + " ".join(r))
    return bytes.fromhex(r[1])


def estado(con):
    r = con.orden("HOLA")
    if r[0] != "ARDUTYPE":
        raise Fallo("no parece un Ardutype")
    return r[2] if len(r) > 2 else "?"


def esperar_desbloqueo(con):
    if estado(con) == "ABIERTA":
        return
    log("Ardutype bloqueado: aviso y espero")
    avisar(con, ORIGEN, "Desbloquea para firmar".encode())
    try:
        limite = time.monotonic() + ESPERA_DESBLOQUEO
        while time.monotonic() < limite:
            time.sleep(1.5)
            if estado(con) == "ABIERTA":
                return
        raise Fallo(MOTIVOS["bloqueado"])
    finally:
        try:
            quitar(con, ORIGEN)
        except Fallo:
            pass


def con_ardutype(funcion):
    con = Conexion(buscar_puerto(), espera_turno=15)
    try:
        return funcion(con)
    finally:
        try:
            con.enviar("ADIOS")
        except OSError:
            pass
        con.cerrar()


def pedir_publica(con):
    estado(con)
    return comprobar(con.orden("SSHPUB"), "SSHPUB")


def crear(con):
    if estado(con) != "ABIERTA":
        raise Fallo("desbloquea el Ardutype y repite")
    print("Confírmalo en el Ardutype...", file=sys.stderr)
    con.enviar("SSHGENERAR")
    return comprobar(con.leer(ESPERA_CONFIRMACION), "SSHPUB")


def firmar_en_ardutype(resumen, motivo):
    def f(con):
        esperar_desbloqueo(con)
        con.enviar("SSHFIRMAR", resumen.hex(), motivo.encode().hex())
        firma = comprobar(con.leer(ESPERA_CONFIRMACION), "FIRMA")
        if len(firma) != 64:
            raise Fallo("firma de largo raro")
        return firma
    return con_ardutype(f)


# ---- Qué se firma (solo para la pregunta: el Ardutype no lo puede comprobar) ----

def motivo_de(datos):
    try:
        if datos.startswith(b"SSHSIG"):
            r = Lector(datos[6:])
            ns = r.cadena().decode(errors="replace")
            texto = {"git": "Firmar commit", "file": "Firmar archivo"}.get(ns, f"Firmar {ns}")
        else:
            # Autenticación: session_id, 50, usuario, servicio, "publickey", ...
            r = Lector(datos)
            r.cadena()
            if r.byte() != 50:
                raise ValueError
            usuario = r.cadena().decode(errors="replace")
            texto = f"Login SSH {usuario}"
    except (ValueError, IndexError, struct.error):
        texto = "Firmar"
    return texto[:LARGO_MOTIVO]


# ---- Servidor ----

cerrojo = threading.Lock()  # una firma cada vez


def responder(sock, tipo, cuerpo=b""):
    m = bytes([tipo]) + cuerpo
    sock.sendall(struct.pack(">I", len(m)) + m)


def atender_firma(cuerpo, mi_blob):
    r = Lector(cuerpo)
    blob = r.cadena()
    datos = r.cadena()
    if blob != mi_blob:
        return None
    motivo = motivo_de(datos)
    log(f"petición: {motivo}")
    with cerrojo:
        try:
            firma = firmar_en_ardutype(hashlib.sha256(datos).digest(), motivo)
        except Fallo as e:
            log(f"no firmado: {e}")
            return None
    log("firmado")
    return cadena(TIPO) + cadena(mpint(firma[:32]) + mpint(firma[32:]))


def atender(sock):
    with sock:
        while True:
            cab = sock.recv(4, socket.MSG_WAITALL)
            if len(cab) < 4:
                return
            n = struct.unpack(">I", cab)[0]
            if n == 0 or n > 256 * 1024:
                return
            m = sock.recv(n, socket.MSG_WAITALL)
            if len(m) < n:
                return
            tipo, cuerpo = m[0], m[1:]
            mi_blob = leer_publica()
            if tipo == REQUEST_IDENTITIES:
                if mi_blob:
                    responder(sock, IDENTITIES_ANSWER,
                              struct.pack(">I", 1) + cadena(mi_blob) + cadena(COMENTARIO.encode()))
                else:
                    responder(sock, IDENTITIES_ANSWER, struct.pack(">I", 0))
            elif tipo == SIGN_REQUEST and mi_blob:
                try:
                    firma = atender_firma(cuerpo, mi_blob)
                except (ValueError, struct.error):
                    firma = None
                if firma:
                    responder(sock, SIGN_RESPONSE, cadena(firma))
                else:
                    responder(sock, FAILURE)
            else:
                responder(sock, FAILURE)  # añadir/quitar claves, extensiones...


def agente():
    if not leer_publica():
        log(f"aviso: no hay {PUBLICA}; usa «crear» o «publica»")
    os.makedirs(RUNTIME, mode=0o700, exist_ok=True)
    try:
        os.unlink(SOCKET)
    except FileNotFoundError:
        pass
    srv = socket.socket(socket.AF_UNIX, socket.SOCK_STREAM)
    viejo = os.umask(0o177)
    srv.bind(SOCKET)
    os.umask(viejo)
    srv.listen(8)
    log(f"escuchando en {SOCKET}")
    try:
        while True:
            c, _ = srv.accept()
            threading.Thread(target=atender, args=(c,), daemon=True).start()
    except KeyboardInterrupt:
        pass
    finally:
        srv.close()
        os.unlink(SOCKET)


def main():
    p = argparse.ArgumentParser(description="Agente SSH con la clave en el Ardutype.")
    p.add_argument("orden", choices=["crear", "publica", "agente"])
    a = p.parse_args()
    try:
        if a.orden == "agente":
            agente()
            return
        pub = con_ardutype(crear if a.orden == "crear" else pedir_publica)
        guardar_publica(pub)
        print(linea_publica(pub))
        print(f"(guardada en {PUBLICA})", file=sys.stderr)
    except Fallo as e:
        sys.exit(f"agente_ssh: {e}")


if __name__ == "__main__":
    main()
