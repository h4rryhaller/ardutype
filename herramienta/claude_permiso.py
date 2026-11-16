#!/usr/bin/env python3
"""Hook de Claude Code: peticiones de permiso y avisos en el Ardutype.

  claude_permiso.py permiso   (hook PermissionRequest)
      Pregunta en el Ardutype. Aceptar o denegar ahí contesta a Claude Code;
      sin respuesta a tiempo (o sin Ardutype) no dice nada y la petición sale
      en la terminal como siempre.
  claude_permiso.py aviso     (hook Notification)
      Aviso en el Ardutype con el mensaje de Claude Code: con pitido si pide
      permiso o pregunta algo; solo LED si ha terminado y espera (idle_prompt);
      nada con el resto de tipos.
  claude_permiso.py quitar    (hooks PostToolUse, PostToolUseFailure, Stop,
                               UserPromptSubmit)
      Quita del Ardutype el aviso de Claude: ya se ha contestado o ya no hace
      falta. Solo abre el puerto si queda un aviso puesto (archivo de marca).

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
MARCA = os.path.join(os.environ.get("XDG_RUNTIME_DIR") or "/tmp", f"ardutype-aviso-claude-{os.getuid()}")


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


CON_PITIDO = {"permission_prompt", "elicitation_dialog"}
SOLO_LED = {"idle_prompt"}


def aviso(datos):
    tipo = datos.get("notification_type")
    if tipo not in CON_PITIDO | SOLO_LED:
        return
    texto = datos.get("message") or "Claude Code te necesita"
    if notificar("aviso", *(["--mudo"] if tipo in SOLO_LED else []), texto=texto) == 0:
        open(MARCA, "w").close()


def quitar():
    try:
        os.remove(MARCA)
    except FileNotFoundError:
        return  # no hay aviso puesto: no hace falta abrir el puerto
    notificar("quitar", texto="")


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
    elif modo == "quitar":
        quitar()
    return 0


if __name__ == "__main__":
    sys.exit(main())
