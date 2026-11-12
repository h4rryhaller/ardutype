// Ardutype: prueba del hardware montado (KB2040 + OLED SSD1306 128x32 + joystick)
//
// Placa: "Adafruit KB2040" del core arduino-pico (Earle Philhower).
// Librería: U8g2. Opcional: Adafruit NeoPixel (si está instalada, el LED de la
// placa cambia de color con cada dirección).
//
// Monitor serie a 115200. Cada 5 s escanea el bus I2C; si el OLED aparece
// (aunque se conecte después de arrancar) lo inicializa y dibuja el joystick.
// Cada pulsación sale por el serie y en la pantalla.

#include <Wire.h>
#include <U8g2lib.h>

#if __has_include(<Adafruit_NeoPixel.h>)
#include <Adafruit_NeoPixel.h>
#define CON_NEOPIXEL 1
#ifndef PIN_NEOPIXEL
#define PIN_NEOPIXEL 17
#endif
Adafruit_NeoPixel pixel(1, PIN_NEOPIXEL, NEO_GRB + NEO_KHZ800);
#endif

// I2C0 en los pines TX y RX del borde (GPIO0 = SDA, GPIO1 = SCL). GPIO12/13
// solo salen por el conector STEMMA QT, no por el borde.
const uint8_t PIN_SDA = 0;  // TX
const uint8_t PIN_SCL = 1;  // RX

// Joystick, mapeado con el polímetro (común a GND, INPUT_PULLUP)
enum { ARRIBA, DERECHA, IZQUIERDA, ABAJO, CENTRO, NUM_DIRECCIONES };
const uint8_t PIN_DIRECCION[NUM_DIRECCIONES] = {2, 3, 4, 5, 6};
const char *NOMBRE[NUM_DIRECCIONES] = {"ARRIBA", "DERECHA", "IZQUIERDA", "ABAJO", "CENTRO"};

const uint32_t ANTIRREBOTE_MS = 30;
const uint32_t PULSACION_LARGA_MS = 800;
const uint32_t ESCANEO_MS = 5000;

U8G2_SSD1306_128X32_UNIVISION_F_HW_I2C oled(U8G2_R0, U8X8_PIN_NONE);
uint8_t direccionOled = 0;  // 0 = sin OLED

bool pulsado[NUM_DIRECCIONES];
bool lecturaAnterior[NUM_DIRECCIONES];
uint32_t cambioMs[NUM_DIRECCIONES];
uint32_t pulsadoDesdeMs[NUM_DIRECCIONES];
bool largaAvisada = false;

char ultimoEvento[22] = "pulsa el joystick";
uint32_t eventos = 0;
bool redibujar = true;

void ponerColor(int direccion) {
#ifdef CON_NEOPIXEL
  static const uint32_t COLOR[NUM_DIRECCIONES] = {
    0x200000, 0x002000, 0x000020, 0x202000, 0x200020};
  pixel.setPixelColor(0, direccion < 0 ? 0 : COLOR[direccion]);
  pixel.show();
#else
  (void)direccion;
#endif
}

// Devuelve la dirección del OLED si hay algo en 0x3C/0x3D. Lista todo lo que encuentra.
uint8_t escanearI2C() {
  uint8_t oledEncontrado = 0;
  int total = 0;
  Serial.print("I2C:");
  for (uint8_t dir = 1; dir < 127; dir++) {
    Wire.beginTransmission(dir);
    if (Wire.endTransmission() == 0) {
      Serial.printf(" 0x%02X", dir);
      total++;
      if (dir == 0x3C || dir == 0x3D) oledEncontrado = dir;
    }
  }
  if (total == 0) Serial.print(" nada (¿SDA/SCL cruzados, VCC o GND?)");
  Serial.println();
  return oledEncontrado;
}

void comprobarOled() {
  uint8_t encontrado = escanearI2C();
  if (encontrado == direccionOled) return;
  if (encontrado) {
    oled.setI2CAddress(encontrado * 2);  // U8g2 usa la dirección de 8 bits
    oled.begin();
    oled.setBusClock(400000);
    Serial.printf("OLED inicializado en 0x%02X\n", encontrado);
    redibujar = true;
  } else {
    Serial.println("OLED perdido");
  }
  direccionOled = encontrado;
}

// Cruz de 5 cuadros a la izquierda: relleno = pulsado
void dibujarCuadro(int x, int y, bool lleno) {
  if (lleno) oled.drawBox(x, y, 9, 9);
  else oled.drawFrame(x, y, 9, 9);
}

void dibujar() {
  if (!direccionOled) return;
  oled.clearBuffer();
  dibujarCuadro(11, 1, pulsado[ARRIBA]);
  dibujarCuadro(1, 11, pulsado[IZQUIERDA]);
  dibujarCuadro(11, 11, pulsado[CENTRO]);
  dibujarCuadro(21, 11, pulsado[DERECHA]);
  dibujarCuadro(11, 21, pulsado[ABAJO]);

  oled.setFont(u8g2_font_6x10_tf);
  oled.drawStr(38, 9, "Ardutype");
  char linea[22];
  snprintf(linea, sizeof(linea), "eventos: %lu", (unsigned long)eventos);
  oled.drawStr(38, 20, linea);
  oled.drawStr(38, 31, ultimoEvento);
  oled.sendBuffer();
}

void evento(const char *texto) {
  eventos++;
  Serial.printf("%8lu ms  %s\n", (unsigned long)millis(), texto);
  strncpy(ultimoEvento, texto, sizeof(ultimoEvento) - 1);
  ultimoEvento[sizeof(ultimoEvento) - 1] = '\0';
  redibujar = true;
}

void leerJoystick() {
  uint32_t ahora = millis();
  for (int i = 0; i < NUM_DIRECCIONES; i++) {
    bool lectura = digitalRead(PIN_DIRECCION[i]) == LOW;
    if (lectura != lecturaAnterior[i]) {
      lecturaAnterior[i] = lectura;
      cambioMs[i] = ahora;
    }
    if (lectura != pulsado[i] && ahora - cambioMs[i] >= ANTIRREBOTE_MS) {
      pulsado[i] = lectura;
      char texto[22];
      if (lectura) {
        pulsadoDesdeMs[i] = ahora;
        if (i == CENTRO) largaAvisada = false;
        snprintf(texto, sizeof(texto), "%s", NOMBRE[i]);
        ponerColor(i);
      } else {
        snprintf(texto, sizeof(texto), "%s suelto %lums", NOMBRE[i],
                 (unsigned long)(ahora - pulsadoDesdeMs[i]));
        ponerColor(-1);
      }
      evento(texto);
    }
  }

  // Pulsación larga del centro: se avisa una vez, sin esperar a soltar
  if (pulsado[CENTRO] && !largaAvisada && ahora - pulsadoDesdeMs[CENTRO] >= PULSACION_LARGA_MS) {
    largaAvisada = true;
    evento("CENTRO largo");
  }

  // Dos o más direcciones a la vez suele ser un puente de estaño
  int activas = 0;
  for (int i = 0; i < NUM_DIRECCIONES; i++) activas += pulsado[i];
  static int activasAvisadas = 0;
  if (activas >= 2 && activas != activasAvisadas) {
    Serial.printf("Aviso: %d direcciones a la vez (¿puente o joystick en diagonal?)\n", activas);
  }
  activasAvisadas = activas;
}

void setup() {
  Serial.begin(115200);
  uint32_t inicio = millis();
  while (!Serial && millis() - inicio < 3000) delay(10);
  Serial.println("\nArdutype: prueba de hardware");

  for (int i = 0; i < NUM_DIRECCIONES; i++) {
    pinMode(PIN_DIRECCION[i], INPUT_PULLUP);
  }
  delay(5);
  for (int i = 0; i < NUM_DIRECCIONES; i++) {
    lecturaAnterior[i] = pulsado[i] = digitalRead(PIN_DIRECCION[i]) == LOW;
    if (pulsado[i]) Serial.printf("Aviso: %s (D%u) a GND al arrancar\n", NOMBRE[i], PIN_DIRECCION[i]);
  }

#ifdef CON_NEOPIXEL
  pixel.begin();
  ponerColor(-1);
#else
  Serial.println("Sin la librería Adafruit NeoPixel: LED de la placa apagado");
#endif

  Wire.setSDA(PIN_SDA);
  Wire.setSCL(PIN_SCL);
  Wire.begin();
  comprobarOled();
}

void loop() {
  static uint32_t ultimoEscaneo = 0;
  if (millis() - ultimoEscaneo >= ESCANEO_MS) {
    ultimoEscaneo = millis();
    comprobarOled();
  }

  leerJoystick();

  if (redibujar) {
    redibujar = false;
    dibujar();
  }
  delay(2);
}
