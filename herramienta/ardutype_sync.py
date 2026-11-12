#!/usr/bin/env python3
"""Sincroniza el Ardutype con una base de datos de KeePassXC.

KeePassXC es el original; el Ardutype lleva una copia de las entradas que
tengan la etiqueta «ardutype». En cada sincronización:

  1. Si en el Ardutype se han creado, cambiado o borrado servicios, se recogen
     (hay que confirmarlo en el dispositivo) y se pasan a KeePassXC. Si una
     entrada ha cambiado en los dos lados, se pregunta cuál vale.
  2. Se envía al Ardutype la lista completa de entradas con la etiqueta (hay
     que confirmar en el dispositivo el resumen: nuevos, cambiados, borrados).

Uso:
  python ardutype_sync.py Contraseñas.kdbx [--puerto /dev/ttyACM0] [--clave-fichero f.keyx]
                          [--etiqueta ardutype] [--simular]

Requisitos: pip install pykeepass pyserial
El Ardutype tiene que estar desbloqueado. Mejor con KeePassXC cerrado, o al
menos sin cambios por guardar: la herramienta escribe el .kdbx (antes deja una
copia en <base>.kdbx.antes-ardutype). No admite bases protegidas con YubiKey.
"""

import argparse
import getpass
import hashlib
import shutil
import sys
import time
import uuid

import serial
import serial.tools.list_ports
from pykeepass import PyKeePass

VID_ADAFRUIT = 0x239A
PROPIEDAD_HUELLA = "ardutype-sincronizado"  # huella del estado de la última sincronización
PROPIEDAD_GUION = "ardutype-guion"          # guion opcional del servicio (ver guion.h)
LARGO_NOMBRE = 31  # bytes UTF-8 (almacen.h, sin el 0 final)
LARGO_CAMPO = 95
LARGO_GUION = 1023
MAX_SERVICIOS = 64
MAX_CATEGORIAS = 16
MAX_GUIONES = 8
ESPERA_CONFIRMACION_S = 120

# Teclas y modificadores válidos de un guion (deben cuadrar con guion.cpp)
TECLAS_GUION = {"intro", "tab", "esc", "espacio", "supr", "retro", "insert", "arriba", "abajo",
                "izq", "der", "inicio", "fin", "repag", "avpag", "menu"} | {f"f{i}" for i in range(1, 13)}
MODIFICADORES_GUION = {"ctrl", "alt", "altgr", "mays", "super"}


class ErrorArdutype(Exception):
    pass


MOTIVOS = {
    "bloqueado": "el Ardutype está bloqueado: desbloquéalo y vuelve a empezar",
    "ocupado": "el Ardutype está a medias de algo (¿clave de recuperación o PIN nuevo?)",
    "cancelado": "cancelado en el dispositivo",
    "pendientes": "el Ardutype tiene cambios sin recoger",
    "lleno": "no caben más servicios (100 como mucho)",
    "categorias": "no caben más categorías (16 como mucho)",
    "guardar": "el Ardutype no ha podido guardar",
}


def a_hex(texto):
    return texto.encode("utf-8").hex() if texto else "-"


def de_hex(campo):
    return "" if campo == "-" else bytes.fromhex(campo).decode("utf-8")


def recortar(texto, maximo):
    """Recorta a maximo bytes UTF-8 sin partir un carácter."""
    datos = texto.encode("utf-8")
    if len(datos) <= maximo:
        return texto
    return datos[:maximo].decode("utf-8", errors="ignore")


def huella(categoria, nombre, usuario, contrasena, guion):
    datos = "\0".join([categoria, nombre, usuario, contrasena, guion])
    return hashlib.sha256(datos.encode("utf-8")).hexdigest()


def validar_guion(texto):
    """Comprueba la sintaxis como lo hace el dispositivo. Devuelve None si está
    bien, o un mensaje con la línea del primer error."""
    for n, linea in enumerate(texto.split("\n"), 1):
        linea = linea.strip()
        if not linea or linea.startswith("#"):
            continue
        orden, _, arg = linea.partition(" ")
        orden = orden.lower()
        arg = arg.strip()
        if orden == "escribir":
            if not arg:
                return f"línea {n}: falta texto"
        elif orden == "tecla":
            tokens = arg.split("+") if arg else []
            principales = 0
            for t in tokens:
                t = t.lower()
                if t in MODIFICADORES_GUION:
                    continue
                if t in TECLAS_GUION or (len(t) == 1 and 0x20 <= ord(t) < 0x7F):
                    principales += 1
                else:
                    return f"línea {n}: tecla «{t}» no válida"
            if principales > 1 or not tokens:
                return f"línea {n}: tecla no válida"
        elif orden == "pausa":
            if not arg.isdigit():
                return f"línea {n}: pausa no válida"
        else:
            return f"línea {n}: orden «{orden}» no válida"
    return None


# ---- Conexión con el Ardutype ----

class Ardutype:
    def __init__(self, puerto):
        self.serie = serial.Serial(puerto, 115200, timeout=0.5)
        time.sleep(0.2)
        self.serie.reset_input_buffer()

    def enviar(self, *palabras):
        self.serie.write((" ".join(palabras) + "\n").encode("ascii"))

    def leer(self, espera=5.0):
        """Siguiente respuesta (línea que empieza por '@'), como lista de palabras."""
        limite = time.monotonic() + espera
        while time.monotonic() < limite:
            linea = self.serie.readline().decode("utf-8", errors="replace").strip()
            if linea.startswith("@"):
                palabras = linea[1:].split(" ")
                if palabras[0] == "ERR":
                    motivo = palabras[1] if len(palabras) > 1 else "?"
                    raise ErrorArdutype(MOTIVOS.get(motivo, motivo))
                return palabras
        raise ErrorArdutype("el Ardutype no responde")

    def orden(self, *palabras, espera=5.0):
        self.enviar(*palabras)
        return self.leer(espera)

    def cerrar(self):
        try:
            self.enviar("ADIOS")
            self.leer(2)
        except (ErrorArdutype, serial.SerialException):
            pass
        self.serie.close()


def buscar_puerto():
    puertos = [p.device for p in serial.tools.list_ports.comports() if p.vid == VID_ADAFRUIT]
    if len(puertos) == 1:
        return puertos[0]
    if not puertos:
        sys.exit("No encuentro el Ardutype: ¿está enchufado? Puedes indicarlo con --puerto.")
    sys.exit(f"Hay varias placas de Adafruit ({', '.join(puertos)}): elige una con --puerto.")


# ---- KeePassXC ----

class Base:
    def __init__(self, ruta, contrasena, clave_fichero, etiqueta):
        self.ruta = ruta
        self.kp = PyKeePass(ruta, password=contrasena, keyfile=clave_fichero)
        self.etiqueta = etiqueta
        self.copia_hecha = False
        self.papelera = self.kp.recyclebin_group

    def en_papelera(self, entrada):
        grupo = entrada.group
        while grupo is not None:
            if self.papelera is not None and grupo.uuid == self.papelera.uuid:
                return True
            grupo = grupo.parentgroup
        return False

    def etiquetadas(self):
        return [e for e in self.kp.entries if self.etiqueta in e.tags and not self.en_papelera(e)]

    def por_uuid(self, valor):
        entrada = self.kp.find_entries(uuid=valor, first=True)
        return entrada if entrada is not None and not self.en_papelera(entrada) else None

    def grupo(self, nombre):
        for g in self.kp.find_groups(name=nombre):
            if not (self.papelera is not None and g.uuid == self.papelera.uuid):
                return g
        return self.kp.add_group(self.kp.root_group, nombre)

    def guardar(self):
        if not self.copia_hecha:
            shutil.copy2(self.ruta, self.ruta + ".antes-ardutype")
            self.copia_hecha = True
        self.kp.save()


def guion_de(entrada):
    return entrada.get_custom_property(PROPIEDAD_GUION) or ""


def campos(entrada):
    """Lo que el Ardutype guarda de una entrada, sin recortar (5 valores)."""
    return (entrada.group.name or "", entrada.title or "", entrada.username or "",
            entrada.password or "", guion_de(entrada))


def recortados(entrada):
    """Los campos tal como quedarían en el Ardutype (nombre y categoría recortados)."""
    categoria, nombre, usuario, contrasena, guion = campos(entrada)
    return (recortar(categoria, LARGO_NOMBRE) or "General", recortar(nombre, LARGO_NOMBRE) or "(sin título)",
            usuario, contrasena, guion)


# ---- 1. Del Ardutype a KeePassXC ----

def recoger_cambios(ardutype):
    """Pide los cambios del dispositivo: (servicios, borrados)."""
    pendientes = int(ardutype.orden("PENDIENTES")[1])
    if not pendientes:
        return [], []
    print(f"El Ardutype tiene {pendientes} cambio{'' if pendientes == 1 else 's'}. "
          "Confirma el envío en el dispositivo...")
    ardutype.enviar("RECIBIR")
    servicios, borrados = [], []
    espera = ESPERA_CONFIRMACION_S
    while True:
        r = ardutype.leer(espera)
        espera = 5
        if r[0] == "FIN":
            return servicios, borrados
        if r[0] == "SERV":
            servicios.append({
                "id": r[1],
                "uuid": None if r[2] == "-" else uuid.UUID(hex=r[2]),
                "categoria": de_hex(r[3]), "nombre": de_hex(r[4]),
                "usuario": de_hex(r[5]), "contrasena": de_hex(r[6]), "guion": de_hex(r[7]),
            })
        elif r[0] == "BORRADO":
            borrados.append({"uuid": uuid.UUID(hex=r[1]), "nombre": de_hex(r[2])})


def diferencias(entrada, s):
    categoria, nombre, usuario, contrasena, guion = recortados(entrada)
    cambios = []
    if s["categoria"] != categoria:
        cambios.append(f"categoría ({categoria} / {s['categoria']})")
    if s["nombre"] != nombre:
        cambios.append(f"nombre ({nombre} / {s['nombre']})")
    if s["usuario"] != usuario:
        cambios.append(f"usuario ({usuario} / {s['usuario']})")
    if s["contrasena"] != contrasena:
        cambios.append("contraseña")
    if s["guion"] != guion:
        cambios.append("guion")
    return cambios


def aplicar_en_entrada(base, entrada, s):
    """Pasa a la entrada lo que viene del dispositivo, sin pisar lo que solo
    se había recortado al enviarlo."""
    categoria, nombre, usuario, contrasena, guion = campos(entrada)
    entrada.save_history()
    if s["nombre"] != recortar(nombre, LARGO_NOMBRE):
        entrada.title = s["nombre"]
    entrada.username = s["usuario"]
    entrada.password = s["contrasena"]
    if s["guion"] != guion:
        if s["guion"]:
            entrada.set_custom_property(PROPIEDAD_GUION, s["guion"])
        elif PROPIEDAD_GUION in entrada.custom_properties:
            entrada.delete_custom_property(PROPIEDAD_GUION)
    if s["categoria"] != (recortar(categoria, LARGO_NOMBRE) or "General"):
        base.kp.move_entry(entrada, base.grupo(s["categoria"]))
    entrada.touch(modify=True)


def preguntar(texto, opciones):
    while True:
        r = input(texto).strip().lower()
        if r in opciones:
            return r


def pasar_a_keepass(base, servicios, borrados, simular):
    """Aplica en KeePassXC los cambios del dispositivo. Devuelve los enlaces
    (id del dispositivo, uuid) de los servicios creados allí."""
    enlaces = []
    for s in servicios:
        entrada = base.por_uuid(s["uuid"]) if s["uuid"] else None
        if s["uuid"] and entrada is None:
            print(f"  ! {s['nombre']}: su entrada ya no está en KeePassXC; se crea de nuevo")
        if entrada is None:
            # Un servicio creado en el dispositivo. Si una ejecución anterior lo
            # creó en KeePassXC pero no llegó a enlazarlo, se reutiliza.
            for e in base.etiquetadas():
                if recortados(e) == (s["categoria"], s["nombre"], s["usuario"], s["contrasena"], s["guion"]):
                    entrada = e
                    break
            if entrada is None:
                print(f"  + {s['categoria']} / {s['nombre']}: nuevo en KeePassXC")
                if not simular:
                    entrada = base.kp.add_entry(base.grupo(s["categoria"]), s["nombre"], s["usuario"],
                                                s["contrasena"], tags=[base.etiqueta], force_creation=True)
                    if s["guion"]:
                        entrada.set_custom_property(PROPIEDAD_GUION, s["guion"])
            if entrada is not None:
                enlaces.append((s["id"], entrada.uuid))
            continue

        cambios = diferencias(entrada, s)
        if not cambios:
            continue
        guardada = entrada.get_custom_property(PROPIEDAD_HUELLA)
        cambio_en_keepass = guardada is not None and guardada != huella(*recortados(entrada))
        texto = f"{s['nombre']}: {', '.join(cambios)} (KeePassXC / Ardutype)"
        if cambio_en_keepass:
            r = preguntar(f"  ? {texto}\n    Ha cambiado en los dos lados. ¿Cuál vale? [a]rdutype / [k]eepassxc: ",
                          ("a", "k"))
            if r == "k":
                print("    Se queda lo de KeePassXC (se enviará al Ardutype)")
                continue
        print(f"  ~ {texto}")
        if not simular:
            aplicar_en_entrada(base, entrada, s)

    for b in borrados:
        entrada = base.por_uuid(b["uuid"])
        if entrada is None or base.etiqueta not in entrada.tags:
            continue
        print(f"  - {b['nombre']}: se le quita la etiqueta «{base.etiqueta}» (la entrada sigue en KeePassXC)")
        if not simular:
            entrada.tags = [t for t in entrada.tags if t != base.etiqueta]
            entrada.touch(modify=True)
    return enlaces


# ---- 2. De KeePassXC al Ardutype ----

def preparar_envio(base):
    """Entradas que se envían, como (entrada, campos recortados)."""
    envio, categorias = [], set()
    for entrada in base.etiquetadas():
        categoria, nombre, usuario, contrasena, guion = campos(entrada)
        c = recortados(entrada)
        if len(usuario.encode("utf-8")) > LARGO_CAMPO or len(contrasena.encode("utf-8")) > LARGO_CAMPO:
            print(f"  ! {nombre}: el usuario o la contraseña pasan de {LARGO_CAMPO} bytes; no se envía")
            continue
        if c[1] != nombre or c[0] != categoria:
            print(f"  · {categoria} / {nombre}: el nombre se recorta a {c[0]} / {c[1]}")
        if guion:
            error = validar_guion(guion)
            if len(guion.encode("utf-8")) > LARGO_GUION:
                error = f"pasa de {LARGO_GUION} bytes"
            if error:
                print(f"  ! {nombre}: guion no válido ({error}); se envía sin guion")
                c = c[:4] + ("",)
        envio.append((entrada, c))
        categorias.add(c[0])
    if len(envio) > MAX_SERVICIOS:
        sys.exit(f"Hay {len(envio)} entradas con la etiqueta; el Ardutype admite {MAX_SERVICIOS}.")
    if len(categorias) > MAX_CATEGORIAS:
        sys.exit(f"Las entradas están en {len(categorias)} grupos; el Ardutype admite {MAX_CATEGORIAS} categorías.")
    con_guion = sum(1 for _, c in envio if c[4])
    if con_guion > MAX_GUIONES:
        sys.exit(f"Hay {con_guion} entradas con guion; el Ardutype admite {MAX_GUIONES} guiones a la vez.")
    return envio


def enviar_al_ardutype(ardutype, envio):
    ardutype.orden("ENVIAR")
    for entrada, (categoria, nombre, usuario, contrasena, guion) in envio:
        ardutype.orden("SERV", entrada.uuid.hex, a_hex(categoria), a_hex(nombre),
                       a_hex(usuario), a_hex(contrasena), a_hex(guion))
    r = ardutype.orden("FIN")
    nuevos, cambiados, borrados = (int(x) for x in r[1:4])
    if nuevos or cambiados or borrados:
        print(f"Al Ardutype: {nuevos} nuevos, {cambiados} cambiados, {borrados} borrados. "
              "Confirma en el dispositivo...")
        ardutype.leer(ESPERA_CONFIRMACION_S)
        print("Aplicado en el Ardutype.")
    else:
        ardutype.leer()
        print("El Ardutype ya estaba al día.")


def apuntar_huellas(envio):
    """Recuerda en cada entrada lo enviado, para notar después si cambia en KeePassXC."""
    cambiadas = 0
    for entrada, c in envio:
        h = huella(*c)
        if entrada.get_custom_property(PROPIEDAD_HUELLA) != h:
            entrada.set_custom_property(PROPIEDAD_HUELLA, h, protect=True)
            cambiadas += 1
    return cambiadas


# ---- Programa ----

def main():
    p = argparse.ArgumentParser(description="Sincroniza el Ardutype con KeePassXC.")
    p.add_argument("base", help="base de datos de KeePassXC (.kdbx)")
    p.add_argument("--puerto", help="puerto serie del Ardutype (por defecto, se busca)")
    p.add_argument("--clave-fichero", help="fichero de clave de la base de datos, si lo usa")
    p.add_argument("--etiqueta", default="ardutype", help="etiqueta de las entradas que lleva el Ardutype")
    p.add_argument("--simular", action="store_true",
                   help="enseña los dos sentidos sin escribir nada (sí pide los cambios al Ardutype)")
    args = p.parse_args()

    try:
        base = Base(args.base, getpass.getpass("Contraseña de KeePassXC: "), args.clave_fichero, args.etiqueta)
    except Exception as e:  # credenciales o fichero
        sys.exit(f"No puedo abrir la base de datos: {e}")

    ardutype = Ardutype(args.puerto or buscar_puerto())
    try:
        r = ardutype.orden("HOLA")
        if r[0] != "ARDUTYPE":
            sys.exit("Ese puerto no es un Ardutype.")
        if r[2] != "ABIERTA":
            sys.exit("Desbloquea el Ardutype y vuelve a ejecutar la herramienta.")

        # Dirección 1: del Ardutype a KeePassXC
        print("== Del Ardutype a KeePassXC ==")
        servicios, borrados = recoger_cambios(ardutype)
        enlaces = []
        if servicios or borrados:
            enlaces = pasar_a_keepass(base, servicios, borrados, args.simular)
        else:
            print("  El Ardutype no tiene cambios.")

        # Dirección 2: de KeePassXC al Ardutype (solo se previsualiza)
        print(f"== De KeePassXC al Ardutype (etiqueta «{args.etiqueta}») ==")
        envio = preparar_envio(base)
        for _, (categoria, nombre, _, _, _) in envio:
            print(f"  · {categoria} / {nombre}")
        print(f"  {len(envio)} entradas para el Ardutype")

        if args.simular:
            print("\n(simulación: no se ha escrito nada en ningún lado)")
            return

        # Escritura real: primero KeePassXC, luego el Ardutype
        if servicios or borrados:
            base.guardar()
            for id_, valor in enlaces:
                ardutype.orden("ENLAZAR", id_, valor.hex)
            ardutype.orden("CONFIRMADO")
            print("Guardados en KeePassXC.")
        enviar_al_ardutype(ardutype, envio)
        if apuntar_huellas(envio):
            base.guardar()
    except ErrorArdutype as e:
        sys.exit(f"Ardutype: {e}")
    except KeyboardInterrupt:
        sys.exit("\nInterrumpido.")
    finally:
        ardutype.cerrar()


if __name__ == "__main__":
    main()
