#!/usr/bin/env bash
# Compila el firmware del Ardutype y lo sube a la KB2040 con el Flash Size
# fijado («8MB (Sketch: 7MB, FS: 1MB)»), sin depender de lo que tenga
# seleccionado el IDE.
#
#   ./subir.sh                 compila y sube (busca la placa sola)
#   ./subir.sh -p /dev/ttyACM0 compila y sube a ese puerto
#   ./subir.sh -c              solo compila
#
# La bóveda está a salvo además por otras dos vías: config.h no compila con
# otro Flash Size, y el firmware nunca formatea la memoria por su cuenta.
set -euo pipefail

FQBN="rp2040:rp2040:adafruit_kb2040:flash=8388608_1048576"
VERSION_CLI="1.6.0"
DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
SKETCH="$DIR/ardutype"
SALIDA="$DIR/build"
CONFIG="$HOME/.arduinoIDE/arduino-cli.yaml"  # la del IDE: mismas librerías y cores

puerto=""
solo_compilar=0
while getopts "p:ch" op; do
  case "$op" in
    p) puerto="$OPTARG" ;;
    c) solo_compilar=1 ;;
    *) sed -n '2,10p' "$0" | sed 's/^# \{0,1\}//'; exit 0 ;;
  esac
done

# arduino-cli: el del sistema o uno propio en herramienta/bin (se descarga una vez)
if command -v arduino-cli >/dev/null; then
  CLI="arduino-cli"
else
  CLI="$DIR/herramienta/bin/arduino-cli"
  if [[ ! -x "$CLI" ]]; then
    echo "Descargando arduino-cli $VERSION_CLI en herramienta/bin..."
    mkdir -p "$DIR/herramienta/bin"
    tmp="$(mktemp -d)"
    trap 'rm -rf "$tmp"' EXIT
    # A un temporal: una descarga cortada no debe dejar un arduino-cli roto
    curl -fsSL --retry 3 -o "$tmp/cli.tgz" \
      "https://downloads.arduino.cc/arduino-cli/arduino-cli_${VERSION_CLI}_Linux_64bit.tar.gz"
    tar xzf "$tmp/cli.tgz" -C "$tmp" arduino-cli
    "$tmp/arduino-cli" version >/dev/null
    mv "$tmp/arduino-cli" "$CLI"
  fi
fi
[[ -f "$CONFIG" ]] || { echo "No encuentro $CONFIG (configuración del Arduino IDE)"; exit 1; }

UF2="$SALIDA/ardutype.ino.uf2"
rm -f "$UF2"
echo "Compilando con $FQBN..."
"$CLI" compile --config-file "$CONFIG" -b "$FQBN" --output-dir "$SALIDA" "$SKETCH" 2>&1 |
  grep -E 'error|Sketch uses|Global variables' || true
[[ -f "$UF2" ]] || { echo "La compilación ha fallado"; exit 1; }
(( solo_compilar )) && { echo "Compilado: $UF2"; exit 0; }

# La placa: por su puerto serie, o ya en modo BOOTSEL (unidad RPI-RP2 montada)
if [[ -z "$puerto" ]]; then
  puerto="$("$CLI" board list --config-file "$CONFIG" --format json | python3 -I -c '
import json, sys
d = json.load(sys.stdin)
puertos = d.get("detected_ports", d) if isinstance(d, dict) else d
encontrados = []
for p in puertos:
    info = p.get("port", {})
    vid = info.get("properties", {}).get("vid", "").lower()
    placas = [b.get("fqbn", "") for b in p.get("matching_boards", [])]
    if vid == "0x239a" or any("kb2040" in f for f in placas):
        encontrados.append(info.get("address", ""))
if len(encontrados) > 1:
    sys.exit("Hay varias placas: " + ", ".join(encontrados) + ". Elige una con -p")
print(encontrados[0] if encontrados else "")
')"
fi

if [[ -n "$puerto" ]]; then
  echo "Subiendo a $puerto (cierra antes ardusync o cualquier monitor serie)..."
  "$CLI" upload --config-file "$CONFIG" -b "$FQBN" -p "$puerto" --input-dir "$SALIDA" "$SKETCH"
else
  unidad="$(findmnt -rn -o TARGET -S LABEL=RPI-RP2 2>/dev/null || true)"
  [[ -n "$unidad" ]] || { echo "No encuentro el Ardutype: conéctalo, o mantén BOOT al enchufarlo"; exit 1; }
  echo "Copiando a la unidad $unidad (modo BOOTSEL)..."
  cp "$UF2" "$unidad/" && sync
fi
echo "Listo."
