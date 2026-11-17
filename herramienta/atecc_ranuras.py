#!/usr/bin/env python3
"""Lee la configuración del ATECC608B del Ardutype y dice qué ranuras sirven
para una clave P-256 nueva (agente SSH). Solo lectura: no cambia nada.

Uso:
  atecc_ranuras.py            resumen de las 16 ranuras y de los bloqueos
  atecc_ranuras.py --publicas además, pide la clave pública de las ranuras P-256
                              (GenKey modo 0: la calcula de la privada, no escribe)
  atecc_ranuras.py --crudo    vuelca también los 128 bytes en hex

Campos según la hoja de datos del ATECC608B (zona de configuración):
  SlotConfig (bytes 20-51), KeyConfig (96-127), LockValue (86), LockConfig (87),
  SlotLocked (88-89).
"""

import argparse
import os
import sys

sys.path.insert(0, os.path.dirname(os.path.realpath(__file__)))
from notificar import Conexion, Fallo, buscar_puerto  # noqa: E402

TIPOS = {4: "P-256", 6: "AES", 7: "SHA/datos"}
NUESTRAS = {3: "ADMIN", 4: "INTENTO", 6: "LIMITE"}  # atecc.h


def palabra(cfg, i):
    return cfg[i] | cfg[i + 1] << 8


def describir(cfg, ranura):
    sc = palabra(cfg, 20 + 2 * ranura)
    kc = palabra(cfg, 96 + 2 * ranura)
    slot_bloq = palabra(cfg, 88)
    d = {
        "sc": sc, "kc": kc,
        "privada": bool(kc & 1),
        "pubinfo": bool(kc >> 1 & 1),
        "tipo": kc >> 2 & 7,
        "bloqueable": bool(kc >> 5 & 1),
        "req_random": bool(kc >> 6 & 1),
        "req_auth": bool(kc >> 7 & 1),
        "auth_key": kc >> 8 & 15,
        "persistent": bool(kc >> 12 & 1),
        "read_key": sc & 15,
        "no_mac": bool(sc >> 4 & 1),
        "limited": bool(sc >> 5 & 1),
        "enc_read": bool(sc >> 6 & 1),
        "secreta": bool(sc >> 7 & 1),
        "write_key": sc >> 8 & 15,
        "write_cfg": sc >> 12 & 15,
        "slot_bloqueada": not (slot_bloq >> ranura & 1),
    }
    if d["privada"] and d["tipo"] == 4:
        rk, wc = d["read_key"], d["write_cfg"]
        d["firma_externa"] = bool(rk & 1)
        d["firma_interna"] = bool(rk >> 1 & 1)
        d["ecdh"] = bool(rk >> 2 & 1)
        d["genkey"] = bool(wc >> 1 & 1)     # bit 13: GenKey permitido
        d["privwrite"] = bool(wc >> 2 & 1)  # bit 14: PrivWrite permitido
    return d


def apta(d):
    """Sirve para el agente SSH: P-256 privada, firma de mensajes externos,
    se puede generar con GenKey y la ranura no está bloqueada."""
    motivos = []
    if not (d["privada"] and d["tipo"] == 4):
        return False, ["no es clave privada P-256"]
    if not d["firma_externa"]:
        motivos.append("sin firma de mensajes externos")
    if not d["genkey"]:
        motivos.append("GenKey no permitido")
    if d["slot_bloqueada"]:
        motivos.append("ranura bloqueada")
    if d["req_auth"]:
        motivos.append(f"pide autorización con la ranura {d['auth_key']}")
    return not motivos, motivos


def main():
    p = argparse.ArgumentParser(description="Ranuras del ATECC608B del Ardutype (solo lectura).")
    p.add_argument("--publicas", action="store_true", help="pedir la pública de las ranuras P-256")
    p.add_argument("--crudo", action="store_true", help="volcar los 128 bytes")
    p.add_argument("--puerto", help="puerto serie (por defecto se busca)")
    a = p.parse_args()

    try:
        con = Conexion(a.puerto or buscar_puerto(), espera_turno=10)
        try:
            r = con.orden("HOLA")
            if r[0] != "ARDUTYPE":
                raise Fallo("no parece un Ardutype")
            r = con.orden("ATECC")
            if r[0] != "ATECC":
                raise Fallo("respuesta: " + " ".join(r) + " (¿firmware actualizado?)")
            cfg = bytes.fromhex(r[1])
            publicas = {}
            if a.publicas:
                for ranura in range(16):
                    d = describir(cfg, ranura)
                    if d["privada"] and d["tipo"] == 4:
                        r = con.orden(f"ATECCPUB {ranura}")
                        publicas[ranura] = r[1] if r[0] == "ATECCPUB" else " ".join(r)
            con.enviar("ADIOS")
        finally:
            con.cerrar()
    except Fallo as e:
        sys.exit(f"atecc: {e}")

    if a.crudo:
        for i in range(0, 128, 16):
            print(f"{i:3d}: {cfg[i:i + 16].hex(' ')}")
        print()

    serie = (cfg[0:4] + cfg[8:13]).hex()
    print(f"Serie {serie}  revisión {cfg[4:8].hex()}  I2C 0x{cfg[16] >> 1:02x}")
    print(f"Configuración: {'bloqueada' if cfg[87] != 0x55 else 'SIN bloquear'}   "
          f"Datos/OTP: {'bloqueados' if cfg[86] != 0x55 else 'SIN bloquear'}   "
          f"CountMatch 0x{cfg[18]:02x}  ChipMode 0x{cfg[19]:02x}")
    print()
    print("Ran SlotCfg KeyCfg Tipo       Uso")
    candidatas = []
    for ranura in range(16):
        d = describir(cfg, ranura)
        tipo = TIPOS.get(d["tipo"], f"tipo {d['tipo']}")
        if d["privada"]:
            tipo += " priv"
        extra = []
        if ranura in NUESTRAS:
            extra.append(f"NUESTRA: {NUESTRAS[ranura]}")
        if d["privada"] and d["tipo"] == 4:
            ok, motivos = apta(d)
            extra.append("firma ext" if d["firma_externa"] else "")
            extra.append("firma int" if d["firma_interna"] else "")
            extra.append("ECDH" if d["ecdh"] else "")
            extra.append("GenKey" if d["genkey"] else "")
            extra.append("PrivWrite" if d["privwrite"] else "")
            if ok:
                candidatas.append(ranura)
                extra.append("→ APTA")
            else:
                extra.append("→ no: " + ", ".join(motivos))
        else:
            if d["secreta"]:
                extra.append("secreta")
            if d["limited"]:
                extra.append("LimitedUse")
        if d["slot_bloqueada"]:
            extra.append("ranura bloqueada")
        if d["req_auth"] and not (d["privada"] and d["tipo"] == 4):
            extra.append(f"ReqAuth({d['auth_key']})")
        print(f"{ranura:3d} 0x{d['sc']:04x}  0x{d['kc']:04x} {tipo:10s} {' '.join(x for x in extra if x)}")
        if ranura in publicas:
            print(f"      pública: {publicas[ranura]}")
    print()
    if candidatas:
        print("Ranuras aptas para el agente SSH: " + ", ".join(map(str, candidatas)))
    else:
        print("Ninguna ranura apta: haría falta otro ATECC configurado a propósito.")


if __name__ == "__main__":
    main()
