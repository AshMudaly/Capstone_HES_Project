#include <Arduino.h>
#include "pwm.h"

// Checks the ESC pulse from the hardware PWM on D9 by timing it with the Arduino itself. No wiring needed
// (the ESC can stay unplugged). For each pulse width it prints what was asked and what was measured:
//   high_us    how long D9 stays high
//   period_us  rising edge to rising edge
// Expect high_us equal to the request and period_us about 20000, each within a few us.
// If it prints "no edge", D9 cannot be read back while the timer owns it: jumper D9 to D4, set MEASURE_PIN to 4.

const uint8_t  ESC_PIN     = 9;
const uint8_t  MEASURE_PIN = 9;
const uint32_t TIMEOUT_US  = 60000;
const uint16_t REQUESTS_US[] = {1000, 1100, 1500, 1900, 2000};

PwmOut esc(ESC_PIN);

// Waits for the pin to reach `level`. Returns false on timeout.
bool waitFor(uint8_t level) {
  uint32_t start = micros();
  while (digitalRead(MEASURE_PIN) != level) {
    if (micros() - start > TIMEOUT_US) return false;
  }
  return true;
}

void measure(uint16_t requestUs) {
  if (!esc.pulseWidth_us(requestUs)) {
    Serial.println(F("# pulseWidth_us failed"));
    return;
  }
  delay(100);  // let the new width take effect
  if (!waitFor(LOW) || !waitFor(HIGH)) { Serial.println(F("# no edge")); return; }
  uint32_t rise1 = micros();
  if (!waitFor(LOW)) { Serial.println(F("# no edge")); return; }
  uint32_t fall = micros();
  if (!waitFor(HIGH)) { Serial.println(F("# no edge")); return; }
  uint32_t rise2 = micros();
  Serial.print(F("request_us="));
  Serial.print(requestUs);
  Serial.print(F(" high_us="));
  Serial.print(fall - rise1);
  Serial.print(F(" period_us="));
  Serial.println(rise2 - rise1);
}

void setup() {
  Serial.begin(115200);
  delay(2000);
  Serial.print(F("# PWM begin="));
  Serial.println(esc.begin(20000u, 1000u));
}

void loop() {
  for (uint16_t us : REQUESTS_US) measure(us);
  Serial.println(F("# ---"));
  delay(1000);
}
