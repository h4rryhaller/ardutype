#include <Adafruit_NeoPixel.h>
#include "led.h"

static Adafruit_NeoPixel pixel(1, PIN_NEOPIXEL, NEO_GRB + NEO_KHZ800);

static bool activo = true;
static uint8_t brillo = 8;
static bool tecleando = false;
static bool reposo = false;
static uint32_t colorPomodoro = 0;
static ModoLedPomodoro modoPomodoro = LED_POMO_NADA;

// Destello en curso: color, número de encendidos y duración de cada fase
static uint32_t colorDestello = 0;
static uint8_t vecesDestello = 0;
static uint16_t faseMs = 0;
static uint32_t inicioDestelloMs = 0;
static bool destelloImportante = false;  // una pulsación no tapa un error

static uint32_t colorMostrado = 0xFFFFFFFF;

// Con brillos tan bajos, un canal encendido nunca se queda en 0
static uint8_t canal(uint8_t v) {
  if (!v || !brillo) return 0;
  return max(1, v * brillo / 255);
}

static uint32_t escalar(uint8_t r, uint8_t g, uint8_t b) {
  return pixel.Color(canal(r), canal(g), canal(b));
}

static void destellar(uint32_t rgb, uint8_t veces, uint16_t ms, bool importante) {
  if (!importante && destelloImportante && vecesDestello) return;
  colorDestello = rgb;
  vecesDestello = veces;
  faseMs = ms;
  destelloImportante = importante;
  inicioDestelloMs = millis();
}

void ledIniciar() {
  pixel.begin();
  pixel.clear();
  pixel.show();
}

void ledTick() {
  uint32_t ahora = millis();
  uint32_t color = 0;

  if (!activo) {
    color = 0;
  } else if (vecesDestello) {
    uint32_t fase = (ahora - inicioDestelloMs) / faseMs;
    if (fase >= 2u * vecesDestello - 1) {
      vecesDestello = 0;  // termina: el siguiente tick pinta el fondo
    } else if (fase % 2 == 0) {
      color = escalar(colorDestello >> 16, colorDestello >> 8, colorDestello);
    }
  } else if (tecleando) {
    // Ámbar latiendo una vez cada 2 s
    uint32_t t = ahora % 2000;
    uint8_t nivel = 40 + (t < 1000 ? t : 2000 - t) * 215 / 1000;
    color = escalar(255 * nivel / 255, 120 * nivel / 255, 0);
  } else if (modoPomodoro != LED_POMO_NADA) {
    // Pausa: 1 s encendido y 1 s apagado. Terminado: 2 veces por segundo
    bool encendido = modoPomodoro == LED_POMO_FIJO ||
                     (modoPomodoro == LED_POMO_LENTO && ahora % 2000 < 1000) ||
                     (modoPomodoro == LED_POMO_RAPIDO && ahora % 500 < 250);
    if (encendido) color = escalar(colorPomodoro >> 16, colorPomodoro >> 8, colorPomodoro);
  } else if (reposo) {
    // Una vuelta al círculo de color cada 24 s
    uint16_t tono = (ahora % 24000) * 65536UL / 24000;
    // Gamma a pleno brillo y luego escalado: al revés, con brillos bajos se apaga
    uint32_t pleno = pixel.gamma32(pixel.ColorHSV(tono, 255, 255));
    color = escalar(pleno >> 16, pleno >> 8, pleno);
  }

  if (color != colorMostrado) {
    colorMostrado = color;
    pixel.setPixelColor(0, color);
    pixel.show();
  }
}

void ledActivo(bool si) {
  activo = si;
}

void ledBrillo(uint8_t b) {
  brillo = b;
}

void ledPulsacion() {
  destellar(0x0050FF, 1, 120, false);
}

void ledError() {
  destellar(0xFF0000, 2, 300, true);
}

void ledOk() {
  destellar(0x00FF00, 1, 600, true);
}

void ledTecleo(bool si) {
  tecleando = si;
}

void ledReposo(bool si) {
  reposo = si;
}

void ledPomodoro(uint32_t rgb, ModoLedPomodoro modo) {
  colorPomodoro = rgb;
  modoPomodoro = modo;
}
