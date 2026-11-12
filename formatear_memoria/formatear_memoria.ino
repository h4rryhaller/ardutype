// Ardutype: formatea la memoria de ficheros (LittleFS) de la KB2040
//
// Borra la bóveda (/boveda.bin) y la clave ADMIN del chip (/chip.bin). Después,
// al cargar otra vez el firmware, arranca en «Ardutype nuevo».
//
// Hay que subirlo con el MISMO Flash Size que el firmware:
// «8MB (Sketch: 7MB, FS: 1MB)». Si no, formatearía otra zona de la flash.
//
// Monitor serie a 115200, con fin de línea. Enseña lo que hay y pide escribir
// FORMATEAR para seguir.
//
// OJO: si existe /chip.bin, el ATECC608 ya está provisionado con esa bóveda.
// Formatear lo deja sin su clave ADMIN: el PIN no podría ir con ese chip y
// habría que poner otro. Ese caso pide escribir FORMATEAR CHIP.

#include <LittleFS.h>

static bool hayChip = false;

static void listar() {
  hayChip = false;
  Dir d = LittleFS.openDir("/");
  int n = 0;
  while (d.next()) {
    Serial.printf("  %s (%u bytes)\n", d.fileName().c_str(), (unsigned)d.fileSize());
    if (d.fileName() == "chip.bin") hayChip = true;
    n++;
  }
  if (!n) Serial.println("  (vacía)");
}

static String leerLinea() {
  String s;
  while (true) {
    if (Serial.available()) {
      char c = Serial.read();
      if (c == '\n' || c == '\r') {
        if (s.length()) return s;
      } else {
        s += c;
      }
    }
    delay(1);
  }
}

void setup() {
  Serial.begin(115200);
  while (!Serial) delay(10);
  delay(200);
  Serial.println("\n==== Ardutype: formatear la memoria de ficheros ====");
  if (!LittleFS.begin()) {
    Serial.println("No hay memoria de ficheros: elige Flash Size «8MB (Sketch: 7MB, FS: 1MB)» y vuelve a subirlo.");
    return;
  }
  Serial.println("Contenido:");
  listar();

  const char *pedir = "FORMATEAR";
  if (hayChip) {
    Serial.println("\n¡OJO! Hay /chip.bin: el chip ATECC608 ya está provisionado con esta bóveda.");
    Serial.println("Si formateas, ese chip se queda sin su clave ADMIN y habrá que poner otro.");
    pedir = "FORMATEAR CHIP";
  }
  Serial.printf("\nSe borra TODO. Escribe %s para seguir:\n", pedir);
  if (leerLinea() != pedir) {
    Serial.println("Cancelado. No se ha borrado nada.");
    return;
  }
  bool ok = LittleFS.format() && LittleFS.begin();
  Serial.println(ok ? "Formateada. Contenido:" : "ERROR al formatear");
  if (ok) listar();
  Serial.println("Ahora vuelve a cargar el firmware ardutype.");
}

void loop() {}
