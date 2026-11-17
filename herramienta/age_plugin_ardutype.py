#!/usr/bin/env python3
"""Plugin de age con la clave de cifrado en el ATECC608B del Ardutype.

Se instala como «age-plugin-ardutype» en el PATH; age lo llama solo:
  age -r age1ardutype1...  -o f.age f      cifrar (no hace falta el Ardutype)
  age -d -i identidad.txt  -o f f.age      descifrar (pide confirmarlo en él)

Esquema (como el X25519 de age, con P-256 y la privada en el chip):
  P   = pública de la ranura de cifrado (SEC1 comprimida, 33 bytes)
  e,E = par efímero; S = ECDH(e, P) = coordenada X (32 bytes)
  k   = HKDF-SHA256(S, sal = E || P, info = "age-plugin-ardutype/v1")
  cuerpo = ChaCha20-Poly1305(k, nonce 0) de la clave del archivo
  estrofa: «ardutype <etiqueta> <E>», etiqueta = SHA-256(P)[:4]
Para descifrar, el Ardutype calcula S = ECDH(privada, E) dentro del chip.

Destinatario: Bech32 «age1ardutype1…» con P. Identidad: «AGE-PLUGIN-ARDUTYPE-1…»
con P también: no es secreta, solo dice qué Ardutype usar.
"""

import base64
import hashlib
import os
import sys

sys.path.insert(0, os.path.dirname(os.path.realpath(__file__)))

from cryptography.hazmat.primitives import hashes  # noqa: E402
from cryptography.hazmat.primitives.asymmetric import ec  # noqa: E402
from cryptography.hazmat.primitives.ciphers.aead import ChaCha20Poly1305  # noqa: E402
from cryptography.hazmat.primitives.kdf.hkdf import HKDF  # noqa: E402
from cryptography.hazmat.primitives.serialization import Encoding, PublicFormat  # noqa: E402

NOMBRE = "ardutype"
HRP_DESTINATARIO = "age1" + NOMBRE
HRP_IDENTIDAD = "age-plugin-" + NOMBRE + "-"
INFO = b"age-plugin-ardutype/v1"
TIPO_ESTROFA = NOMBRE

# ---- Bech32 (BIP 173, sin límite de largo, como age) ----

ALFABETO = "qpzry9x8gf2tvdw0s3jn54khce6mua7l"


def _polymod(valores):
    gen = [0x3B6A57B2, 0x26508E6D, 0x1EA119FA, 0x3D4233DD, 0x2A1462B3]
    chk = 1
    for v in valores:
        b = chk >> 25
        chk = (chk & 0x1FFFFFF) << 5 ^ v
        for i in range(5):
            chk ^= gen[i] if (b >> i) & 1 else 0
    return chk


def _expandir(hrp):
    return [ord(c) >> 5 for c in hrp] + [0] + [ord(c) & 31 for c in hrp]


def _convertir(datos, de, a, rellenar):
    acc = bits = 0
    salida = []
    maximo = (1 << a) - 1
    for v in datos:
        acc = acc << de | v
        bits += de
        while bits >= a:
            bits -= a
            salida.append(acc >> bits & maximo)
    if rellenar and bits:
        salida.append(acc << (a - bits) & maximo)
    elif not rellenar and (bits >= de or acc << (a - bits) & maximo):
        raise ValueError("relleno Bech32 incorrecto")
    return salida


def bech32_codificar(hrp, datos):
    d = _convertir(datos, 8, 5, True)
    chk = _polymod(_expandir(hrp) + d + [0] * 6) ^ 1
    d += [chk >> 5 * (5 - i) & 31 for i in range(6)]
    return hrp + "1" + "".join(ALFABETO[x] for x in d)


def bech32_decodificar(texto):
    if texto.lower() != texto and texto.upper() != texto:
        raise ValueError("Bech32 con mayúsculas y minúsculas")
    texto = texto.lower()
    pos = texto.rfind("1")
    hrp, resto = texto[:pos], texto[pos + 1:]
    if pos < 1 or len(resto) < 6:
        raise ValueError("Bech32 mal formado")
    d = [ALFABETO.index(c) for c in resto]
    if _polymod(_expandir(hrp) + d) != 1:
        raise ValueError("suma de control Bech32 incorrecta")
    return hrp, bytes(_convertir(d[:-6], 5, 8, False))


def destinatario(p33):
    return bech32_codificar(HRP_DESTINATARIO, p33)


def identidad(p33):
    return bech32_codificar(HRP_IDENTIDAD, p33).upper()


def publica_de(texto):
    """P (33 bytes) de un destinatario o una identidad del plugin."""
    hrp, datos = bech32_decodificar(texto)
    if hrp not in (HRP_DESTINATARIO, HRP_IDENTIDAD) or len(datos) != 33:
        raise ValueError("no es un destinatario ni una identidad de ardutype")
    ec.EllipticCurvePublicKey.from_encoded_point(ec.SECP256R1(), datos)  # comprueba el punto
    return datos


def comprimir(xy64):
    return ec.EllipticCurvePublicKey.from_encoded_point(ec.SECP256R1(), b"\x04" + xy64).public_bytes(
        Encoding.X962, PublicFormat.CompressedPoint)


def descomprimir(p33):
    return ec.EllipticCurvePublicKey.from_encoded_point(ec.SECP256R1(), p33).public_bytes(
        Encoding.X962, PublicFormat.UncompressedPoint)[1:]


# ---- Criptografía ----

def b64(b):
    return base64.b64encode(b).decode().rstrip("=")


def unb64(s):
    if "=" in s:
        raise ValueError("base64 con relleno")
    return base64.b64decode(s + "=" * (-len(s) % 4), validate=True)


def etiqueta(p33):
    return b64(hashlib.sha256(p33).digest()[:4])


def clave_envoltura(secreto, e33, p33):
    return HKDF(algorithm=hashes.SHA256(), length=32, salt=e33 + p33, info=INFO).derive(secreto)


def envolver(clave_archivo, p33):
    efimera = ec.generate_private_key(ec.SECP256R1())
    e33 = efimera.public_key().public_bytes(Encoding.X962, PublicFormat.CompressedPoint)
    receptor = ec.EllipticCurvePublicKey.from_encoded_point(ec.SECP256R1(), p33)
    secreto = efimera.exchange(ec.ECDH(), receptor)
    k = clave_envoltura(secreto, e33, p33)
    cuerpo = ChaCha20Poly1305(k).encrypt(b"\0" * 12, clave_archivo, None)
    return [TIPO_ESTROFA, etiqueta(p33), b64(e33)], cuerpo


def desenvolver(secreto, e33, p33, cuerpo):
    k = clave_envoltura(secreto, e33, p33)
    return ChaCha20Poly1305(k).decrypt(b"\0" * 12, cuerpo, None)


# ---- Protocolo de plugins de age ----

class Canal:
    def __init__(self):
        self.ent = sys.stdin.buffer
        self.sal = sys.stdout.buffer

    def leer(self):
        cab = self.ent.readline()
        if not cab:
            raise EOFError
        cab = cab.decode().rstrip("\n")
        if not cab.startswith("-> "):
            raise ValueError(f"estrofa mal formada: {cab!r}")
        args = cab[3:].split(" ")
        cuerpo = ""
        while True:
            linea = self.ent.readline().decode().rstrip("\n")
            cuerpo += linea
            if len(linea) < 64:
                break
        return args, unb64(cuerpo)

    def escribir(self, args, cuerpo=b""):
        t = b64(cuerpo)
        lineas = [t[i:i + 64] for i in range(0, len(t), 64)]
        if not lineas or len(lineas[-1]) == 64:
            lineas.append("")
        self.sal.write(("-> " + " ".join(args) + "\n" + "\n".join(lineas) + "\n").encode())
        self.sal.flush()

    def orden(self, args, cuerpo=b""):
        """Fase 2: envía y espera la respuesta de age."""
        self.escribir(args, cuerpo)
        r, c = self.leer()
        return r, c

    def mensaje(self, texto):
        self.orden(["msg"], texto.encode())


def fase1(canal):
    estrofas = []
    while True:
        args, cuerpo = canal.leer()
        if args[0] == "done":
            return estrofas
        estrofas.append((args, cuerpo))


def recipient_v1():
    canal = Canal()
    destinos, claves = [], []
    errores = []
    for args, cuerpo in fase1(canal):
        if args[0] in ("add-recipient", "add-identity"):
            try:
                destinos.append(publica_de(args[1]))
            except (ValueError, IndexError) as e:
                errores.append((["error", "recipient" if args[0] == "add-recipient" else "identity",
                                 str(len(destinos) + len(errores))], str(e)))
        elif args[0] == "wrap-file-key":
            claves.append(cuerpo)
    for args, texto in errores:
        canal.orden(args, texto.encode())
    if not errores:
        for i, clave in enumerate(claves):
            for p33 in destinos:
                estrofa, cuerpo = envolver(clave, p33)
                canal.orden(["recipient-stanza", str(i)] + estrofa, cuerpo)
    canal.escribir(["done"])


def identity_v1():
    from agente_ssh import Fallo, comprobar, con_ardutype, esperar_desbloqueo

    canal = Canal()
    mias, estrofas = [], []
    for args, cuerpo in fase1(canal):
        if args[0] == "add-identity":
            mias.append(publica_de(args[1]))
        elif args[0] == "recipient-stanza" and len(args) >= 3 and args[2] == TIPO_ESTROFA:
            estrofas.append((int(args[1]), args[3:], cuerpo))

    hechos = set()
    for indice, args, cuerpo in estrofas:
        if indice in hechos or len(args) != 2:
            continue
        p33 = next((p for p in mias if etiqueta(p) == args[0]), None)
        if p33 is None:
            continue
        try:
            e33 = unb64(args[1])
            e64 = descomprimir(e33)
        except ValueError:
            canal.orden(["error", "stanza", str(indice)], b"estrofa ardutype mal formada")
            continue
        canal.mensaje("Confirma el descifrado en el Ardutype")

        def ecdh(con):
            esperar_desbloqueo(con, "Desbloquea para descifrar")
            if comprimir(comprobar(con.orden("CIFPUB"), "CIFPUB")) != p33:
                raise Fallo("este Ardutype no tiene la clave de esta identidad")
            con.enviar("ECDH", e64.hex(), "Descifrar archivo".encode().hex())
            return comprobar(con.leer(60), "ECDH")

        try:
            secreto = con_ardutype(ecdh)
            clave = desenvolver(secreto, e33, p33, cuerpo)
        except Fallo as e:
            canal.orden(["error", "internal"], f"Ardutype: {e}".encode())
            break
        except Exception:  # InvalidTag: la estrofa no era para esta clave
            continue
        canal.orden(["file-key", str(indice)], clave)
        hechos.add(indice)
    canal.escribir(["done"])


def main():
    modo = next((a.split("=", 1)[1] for a in sys.argv[1:] if a.startswith("--age-plugin=")), None)
    if modo == "recipient-v1":
        recipient_v1()
    elif modo == "identity-v1":
        identity_v1()
    else:
        sys.exit("age-plugin-ardutype: lo usa age; para crear la clave, «ardutype-cifrado crear»")


if __name__ == "__main__":
    main()
