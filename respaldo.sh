#!/usr/bin/env bash
# Copia de seguridad de la memoria de ficheros del Ardutype: la bóveda y la
# clave ADMIN del ATECC608 (/chip.bin). Si un sketch equivocado o un fallo
# borrara la memoria, restaurar.sh la devuelve y el chip sigue sirviendo (sin
# /chip.bin habría que soldar otro).
#
#   ./respaldo.sh                guarda en ~/Sys/ardutype-respaldos
#   ./respaldo.sh -d <carpeta>   guarda en otra carpeta
#
# Todo va cifrado (es lo mismo que se puede leer de la placa en mano), pero
# mejor fuera de carpetas sincronizadas. El Ardutype se reinicia: queda
# bloqueado.
set -euo pipefail
DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
source "$DIR/herramienta/memoria_comun.sh"

destino="$HOME/Sys/ardutype-respaldos"
while getopts "d:h" op; do
  case "$op" in
    d) destino="$OPTARG" ;;
    *) sed -n '2,13p' "$0" | sed 's/^# \{0,1\}//'; exit 0 ;;
  esac
done

poner_en_bootsel
umask 077
mkdir -p "$destino"
copia="$destino/ardutype-$(date +%Y%m%d-%H%M%S).bin"
"$PICOTOOL" save -r "$ZONA_DESDE" "$ZONA_HASTA" -v "$copia" -t bin "${SELECCION[@]}"

if ! comprobar_copia "$copia"; then
  mv "$copia" "$copia.invalida"
  echo "La copia no es válida: guardada como $copia.invalida"
  exit 1
fi
{
  echo "serie=$SERIE"
  echo "serie_bootsel=$SERIE_BOOTSEL"
  echo "fecha=$(date -Iseconds)"
  echo "zona=$ZONA_DESDE-$ZONA_HASTA"
  echo "sha256=$(sha256sum "$copia" | cut -d' ' -f1)"
} > "$copia.info"
echo "Copia guardada: $copia"
listar_copia "$copia"
