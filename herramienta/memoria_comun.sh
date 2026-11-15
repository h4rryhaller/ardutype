# Común a respaldo.sh y restaurar.sh (se carga con source).
#
# Zona del sistema de ficheros con Flash Size «8MB (Sketch: 7MB, FS: 1MB)»:
# fs_start y fs_end del core (boards.txt), los mismos que exige config.h.
# Ahí están la bóveda (/boveda.bin) y la clave ADMIN del chip (/chip.bin).
ZONA_DESDE=0x106FF000
ZONA_HASTA=0x107FF000
ZONA_TAM=1048576

# picotool del core arduino-pico (el más reciente instalado)
PICOTOOL="$(ls -d "$HOME"/.arduino15/packages/rp2040/tools/pqt-picotool/*/ 2>/dev/null | sort -V | tail -1)picotool"
[[ -x "$PICOTOOL" ]] || { echo "No encuentro picotool (viene con el core arduino-pico)"; exit 1; }

# Ruta en /sys del dispositivo USB con ese VID:PID (y número de serie, si se da)
dispositivo_usb() {
  local d
  for d in /sys/bus/usb/devices/*; do
    [[ -f "$d/idVendor" ]] || continue
    [[ "$(cat "$d/idVendor"):$(cat "$d/idProduct")" == "$1" ]] || continue
    [[ -z "${2:-}" || "$(cat "$d/serial" 2>/dev/null)" == "$2" ]] && echo "$d"
  done
}

# Identifica la placa conectada sin tocarla: "marcha:<serie>" o
# "bootsel:<serie>". Son dos números distintos: en marcha, el identificador
# único de la flash (lo pone el core); en BOOTSEL, el de la ROM del RP2040.
identidad_conectada() {
  local d
  d="$(dispositivo_usb 239a:8106 | head -1)"
  [[ -n "$d" ]] && { echo "marcha:$(cat "$d/serial")"; return; }
  d="$(dispositivo_usb 2e8a:0003 | head -1)"
  [[ -n "$d" ]] && echo "bootsel:$(cat "$d/serial")"
}

# Busca el Ardutype en marcha (239a:8106) o en BOOTSEL (2e8a:0003) y lo deja
# en BOOTSEL, que es donde picotool lee y escribe la flash. Deja SERIE (en
# marcha; vacío si ya estaba en BOOTSEL), SERIE_BOOTSEL y en SELECCION las
# opciones de picotool para esa placa (por su sitio en el bus: el filtro
# --ser de picotool no la encuentra en BOOTSEL). Para pasarla a BOOTSEL abre
# su puerto serie a 1200 baudios, como el IDE (la interfaz de reinicio de
# «picotool -f» solo existe si el firmware se compila con el método Picotool).
poner_en_bootsel() {
  local marcha bootsel d tty nodo i
  marcha=($(dispositivo_usb 239a:8106))
  bootsel=($(dispositivo_usb 2e8a:0003))
  if (( ${#marcha[@]} + ${#bootsel[@]} == 0 )); then echo "No encuentro el Ardutype: conéctalo"; exit 1; fi
  if (( ${#marcha[@]} + ${#bootsel[@]} > 1 )); then echo "Hay varias placas RP2040 conectadas: deja solo el Ardutype"; exit 1; fi

  SERIE=""
  if (( ${#marcha[@]} )); then
    d="${marcha[0]}"
    SERIE="$(cat "$d/serial")"
    tty="$(ls "$d"/*/tty 2>/dev/null | head -1)"
    [[ -n "$tty" ]] || { echo "El Ardutype no tiene puerto serie"; exit 1; }
    if fuser -s "/dev/$tty" 2>/dev/null; then
      echo "/dev/$tty está en uso (monitor serie del IDE, ardusync...): ciérralo y repite."
      exit 1
    fi
    echo "Reiniciando el Ardutype en modo BOOTSEL (quedará bloqueado)..."
    stty -F "/dev/$tty" 1200
    d=""
    for i in $(seq 1 40); do
      d="$(dispositivo_usb 2e8a:0003 | head -1)"
      [[ -n "$d" ]] && break
      sleep 0.25
    done
    [[ -n "$d" ]] || { echo "No ha entrado en BOOTSEL: mantén BOOT al enchufarlo y repite"; exit 1; }
  else
    d="${bootsel[0]}"
  fi
  SERIE_BOOTSEL="$(cat "$d/serial")"
  SELECCION=(--bus "$(cat "$d/busnum")" --address "$(cat "$d/devnum")")
  EN_BOOTSEL=1

  # picotool necesita escribir en el dispositivo USB: regla de udev (que tarda un poco)
  nodo="/dev/bus/usb/$(printf %03d "$(cat "$d/busnum")")/$(printf %03d "$(cat "$d/devnum")")"
  for i in $(seq 1 20); do [[ -w "$nodo" ]] && break; sleep 0.25; done
  if [[ ! -w "$nodo" ]]; then
    echo "Sin permiso sobre $nodo. Instala la regla de udev (una vez):"
    echo "  sudo cp $DIR/herramienta/60-ardutype.rules /etc/udev/rules.d/"
    echo "  sudo udevadm control --reload-rules && sudo udevadm trigger"
    echo "desenchufa el Ardutype (está en BOOTSEL) y vuelve a enchufarlo."
    exit 1
  fi
}

# Vuelve a arrancar el firmware si lo dejamos en BOOTSEL (también si algo falla)
EN_BOOTSEL=0
salir_de_bootsel() {
  (( EN_BOOTSEL )) || return 0
  "$PICOTOOL" reboot "${SELECCION[@]}" >/dev/null 2>&1 ||
    echo "No se ha podido reiniciar: desenchufa el Ardutype y vuelve a enchufarlo"
}
trap salir_de_bootsel EXIT

# Una copia válida mide 1 MB y lleva la marca de LittleFS
comprobar_copia() {
  local f="$1"
  [[ "$(stat -c %s "$f")" == "$ZONA_TAM" ]] || { echo "$f no mide $ZONA_TAM bytes"; return 1; }
  grep -aq littlefs "$f" || { echo "$f no contiene un sistema de ficheros LittleFS"; return 1; }
}

# Lista los ficheros de una copia (mklittlefs del core). Bloques de 4 KB.
listar_copia() {
  local m; m="$(ls -d "$HOME"/.arduino15/packages/rp2040/tools/pqt-mklittlefs/*/ 2>/dev/null | sort -V | tail -1)mklittlefs"
  [[ -x "$m" ]] || return 0
  "$m" -l -b 4096 -p 256 -s "$ZONA_TAM" "$1" 2>/dev/null | sed 's/^/    /' || true
}
