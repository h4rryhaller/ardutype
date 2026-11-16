#!/usr/bin/env python3
"""Hook de Claude Code: peticiones de permiso y avisos en el Ardutype.

  claude_permiso.py permiso   (hook PermissionRequest)
      Pregunta en el Ardutype. Aceptar o denegar ahí contesta a Claude Code;
      sin respuesta a tiempo (o sin Ardutype) no dice nada y la petición sale
      en la terminal como siempre.
  claude_permiso.py aviso     (hook Notification)
      Aviso en el Ardutype con el mensaje de Claude Code.

Lee el JSON del hook por la entrada estándar. Usa notificar.py (mismo
directorio). Nunca falla hacia «aceptar»: cualquier error = sin respuesta.
"""

import json
import os
import subprocess
import sys

NOTIFICAR = os.path.join(os.path.dirname(os.path.realpath(__file__)), "notificar.py")
PLAZO_S = int(os.environ.get("ARDUTYPE_PLAZO_PERMISO", "90"))
ORIGEN = "Claude"


def resumen(tool, entrada):
    """Lo que se pide, en pocas palabras y con lo importante delante."""
    if not isinstance(entrada, dict):
        return tool
    if tool == "Bash":
        texto = entrada.get("command", "")
        desc = entrada.get("description")
        return f"Bash: {texto}" + (f" ({desc})" if desc else "")
    for campo in ("file_path", "notebook_path", "path", "url", "pattern", "query", "skill", "prompt"):
        if entrada.get(campo):
            return f"{tool}: {entrada[campo]}"
    return f"{tool}: {json.dumps(entrada, ensure_ascii=False)}"


def notificar(*args, texto):
    return subprocess.run([sys.executable, "-I", NOTIFICAR, *args, "-q", "-o", ORIGEN, "-"],
                          input=texto, text=True).returncode


def permiso(datos):
    texto = resumen(datos.get("tool_name", "?"), datos.get("tool_input"))
    r = notificar("pregunta", "-t", str(PLAZO_S), texto=texto)
    if r == 0:
        decision = {"behavior": "allow"}
    elif r == 1:
        decision = {"behavior": "deny", "message": "Denegado por el usuario en el Ardutype."}
    else:
        return  # sin respuesta: la petición sigue en la terminal
    print(json.dumps({"hookSpecificOutput": {"hookEventName": "PermissionRequest", "decision": decision}}))


def aviso(datos):
    texto = datos.get("message") or "Claude Code te necesita"
    notificar("aviso", texto=texto)


def main():
    try:
        datos = json.load(sys.stdin)
    except ValueError:
        return 0
    modo = sys.argv[1] if len(sys.argv) > 1 else ""
    if modo == "permiso":
        permiso(datos)
    elif modo == "aviso":
        aviso(datos)
    return 0


if __name__ == "__main__":
    sys.exit(main())
