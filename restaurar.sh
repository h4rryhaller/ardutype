#!/usr/bin/env bash
# Devuelve al Ardutype una copia hecha con respaldo.sh: reescribe toda la
# memoria de ficheros (bóveda y /chip.bin) con la de la copia.
#
#   ./restaurar.sh <copia.bin>       solo en la misma placa de la copia
#   ./restaurar.sh -o <copia.bin>    en otra KB2040 (p. ej. si se cambió la
#                                    placa conservando el mismo ATECC608)
#
# Lo que se haya cambiado en el aparato después de la copia se pierde:
# después, sincroniza con KeePassXC. El Ardutype se reinicia.
set -euo pipefail
DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
source "$DIR/herramienta/memoria_comun.sh"

otra_placa=0
while getopts "oh" op; do
  case "$op" in
    o) otra_placa=1 ;;
    *) sed -n '2,10p' "$0" | sed 's/^# \{0,1\}//'; exit 0 ;;
  esac
done
shift $((OPTIND - 1))
copia="${1:-}"
[[ -f "$copia" ]] || { echo "Uso: ./restaurar.sh [-o] <copia.bin>"; exit 1; }
comprobar_copia "$copia" || exit 1

info="$copia.info"
serie_copia=""
if [[ -f "$info" ]]; then
  serie_copia="$(sed -n 's/^serie=//p' "$info")"
  sha="$(sed -n 's/^sha256=//p' "$info")"
  [[ "$(sha256sum "$copia" | cut -d' ' -f1)" == "$sha" ]] || { echo "La copia no coincide con su .info (dañada o modificada)"; exit 1; }
  echo "Copia del $(sed -n 's/^fecha=//p' "$info"), placa $serie_copia"
else
  echo "Sin fichero .info: no se puede comprobar de qué placa es ni si está íntegra."
  (( otra_placa )) || { echo "Usa -o si estás seguro."; exit 1; }
fi
listar_copia "$copia"

# Antes de reiniciar nada: ¿es la placa de la copia? (en marcha o en BOOTSEL)
conectada="$(identidad_conectada)"
[[ -n "$conectada" ]] || { echo "No encuentro el Ardutype: conéctalo"; exit 1; }
case "$conectada" in
  marcha:*) esperada="$serie_copia" ;;
  bootsel:*) esperada="$(sed -n 's/^serie_bootsel=//p' "$info" 2>/dev/null || true)" ;;
esac
if [[ -n "$esperada" && "${conectada#*:}" != "$esperada" ]] && (( ! otra_placa )); then
  echo "La copia es de otra placa (conectada: ${conectada#*:}, copia: $esperada). Usa -o si es a propósito."
  exit 1
fi
if [[ -z "$esperada" ]] && (( ! otra_placa )); then
  echo "No puedo comprobar que sea la misma placa (conéctala en marcha, no en BOOTSEL), o usa -o."
  exit 1
fi
echo
echo "Se va a SUSTITUIR toda la memoria de ficheros del Ardutype por esta copia."
echo "Lo cambiado en el aparato después de la copia se pierde."
read -r -p "Escribe RESTAURAR para seguir: " respuesta
[[ "$respuesta" == "RESTAURAR" ]] || { echo "Cancelado. No se ha tocado nada."; exit 1; }

poner_en_bootsel

"$PICOTOOL" load -v "$copia" -t bin -o "$ZONA_DESDE" "${SELECCION[@]}"
echo "Restaurada. Desbloquea el Ardutype y sincroniza con KeePassXC."
