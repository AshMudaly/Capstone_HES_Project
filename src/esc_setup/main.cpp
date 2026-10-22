#include <Arduino.h>

// ESC setup helper for the Hobbywing Skywalker V2: throttle calibration and stick programming from the keyboard.
// The Arduino plays the part of the transmitter stick. PROP OFF. Motor connected (the motor makes the beeps).
//
//   D9  ESC signal (ESC red BEC wire removed from the plug), GND to ESC signal ground
//
// Serial Monitor at 115200 baud, any line ending.
//   u   2000 us  (stick to the top)
//   d   1000 us  (stick to the bottom)
//
// Powers up at 1000 us. The ESC reads the stick position when the pack is connected, so send u BEFORE connecting
// the pack when you want calibration or programming. Procedure: see the Phase 1 tab of the wiring page.

#include <Servo.h>

const uint8_t  ESC_PIN     = 9;
const uint16_t BOTTOM_US   = 1000;
const uint16_t TOP_US      = 2000;

Servo esc;

void setPulse(uint16_t us) {
  esc.writeMicroseconds(us);
  Serial.print(F("# throttle "));
  Serial.print(us);
  Serial.println(F(" us"));
}

void setup() {
  Serial.begin(115200);
  esc.attach(ESC_PIN);
  esc.writeMicroseconds(BOTTOM_US);
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
