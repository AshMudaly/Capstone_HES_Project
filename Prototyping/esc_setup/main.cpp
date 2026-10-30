#include <Arduino.h>

//   D9  ESC signal (ESC red BEC wire removed from the plug), GND to ESC signal ground

#include "pwm.h"

const uint8_t  ESC_PIN     = 9;
const uint16_t BOTTOM_US   = 1000;
const uint16_t TOP_US      = 2000;

PwmOut esc(ESC_PIN);  // hardware PWM, 50 Hz

void setPulse(uint16_t us) {
  if (!esc.pulseWidth_us(us)) Serial.println(F("# PWM update failed"));
  Serial.print(F("# throttle "));
  Serial.print(us);
  Serial.println(F(" us"));
}

void setup() {
  Serial.begin(115200);
  if (!esc.begin(20000u, (uint32_t)BOTTOM_US)) {
    Serial.println(F("# PWM failed to start on D9"));
    while (true) {}
  }
  Serial.println(F("# ESC setup helper. Prop off. u = 2000 us (top), d = 1000 us (bottom). Now 1000 us."));
}

void loop() {
  if (!Serial.available()) return;
  char c = Serial.read();
  if (c == 'u') {
    setPulse(TOP_US);
  } else if (c == 'd') {
    setPulse(BOTTOM_US);
  }
}
