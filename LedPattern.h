#pragma once
#include <Adafruit_NeoPixel.h>

// Kleiner Animator für „Rundumkennleuchte“ mit Sinus-Spot.
// Nicht-blockierend: pro loop()-Tick 0..1 Frames.
// Verwendung:
//   LedPattern anim(pixel, NUM_LEDS);
//   anim.beaconSin(0,0,255, 2.6f, 28, 1.6f, 1.0f);  // Beispiel

class LedPattern {
public:
  LedPattern(Adafruit_NeoPixel& strip, uint8_t count);

  // Ein Frame der Sinus-Rundumkennleuchte zeichnen.
  // r,g,b: Basisfarbe
  // speed: Phasen-Geschwindigkeit (1.2…3.5 typ.)
  // dtMs : Ziel-Bildrate (24…40ms typ. → ~25–40 FPS)
  // gamma: 1.0 weich; 1.5–2.2 spotiger
  // intensScale: zusätzliche Helligkeits-Skalierung 0..1
  void beaconSin(uint8_t r, uint8_t g, uint8_t b,
                 float speed, uint16_t dtMs, float gamma, float intensScale);

  // Phase/Timer zurücksetzen (optional, bei Moduswechsel nice-to-have)
  void reset();

private:
  Adafruit_NeoPixel& px;
  uint8_t num;
  uint32_t nextMs = 0;
  float phase     = 0.0f;

  static uint8_t scale8(uint8_t c, float f);
};
