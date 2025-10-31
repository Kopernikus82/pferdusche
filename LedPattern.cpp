#include "LedPattern.h"
#include <math.h>     // sinf, powf
#include <Arduino.h>  // millis()

static const float TWO_PI_F = 6.28318530718f;

LedPattern::LedPattern(Adafruit_NeoPixel& strip, uint8_t count)
: px(strip), num(count) {}

void LedPattern::reset() {
  phase = 0.0f;
  nextMs = 0;
}

uint8_t LedPattern::scale8(uint8_t c, float f) {
  int v = (int)(c * f + 0.5f);
  if (v < 0) v = 0; if (v > 255) v = 255;
  return (uint8_t)v;
}

void LedPattern::beaconSin(uint8_t r, uint8_t g, uint8_t b,
                           float speed, uint16_t dtMs, float gamma, float intensScale)
{
  uint32_t now = millis();
  if ((int32_t)(now - nextMs) < 0) return;  // noch nicht Zeit für den nächsten Frame
  nextMs = now + dtMs;

  // Phase weiterdrehen
  phase += speed * (TWO_PI_F / 256.0f);
  if (phase > TWO_PI_F) phase -= TWO_PI_F;

  float step = TWO_PI_F / (float)num;

  for (uint8_t i = 0; i < num; ++i) {
    float a   = phase + i * step;
    float s   = 0.5f * (sinf(a) + 1.0f);    // 0..1
    float amp = powf(s, gamma) * intensScale;
    uint8_t rr = scale8(r, amp);
    uint8_t gg = scale8(g, amp);
    uint8_t bb = scale8(b, amp);
    px.setPixelColor(i, px.Color(rr, gg, bb));
  }
  px.show();
}
