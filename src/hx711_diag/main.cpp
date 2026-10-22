// HX711 bit-level diagnostic. Read-only: no library, no ESC, no SD, no INA260.
// Clocks the HX711 by hand exactly as the library does (interrupts off, 24 data pulses + 1 gain pulse).
// One line per read:  <24 bits, first clocked first>  <value as signed 24-bit>  <longest SCK-high time in us, upper bound>
// A reset inside the HX711 happens if SCK stays high for more than 60 us.
#include <Arduino.h>

const uint8_t HX_DT_PIN  = 2;
const uint8_t HX_SCK_PIN = 3;

// Extra time SCK is held high and low, in microseconds. 0 = as fast as the R4 can toggle (what the library does).
// Test with the RATE switch at L. Keep it far below 60 us.
const uint8_t PULSE_US = 15;

void setup() {
  Serial.begin(115200);
  delay(2000);  // let the monitor attach
  pinMode(HX_SCK_PIN, OUTPUT);
  digitalWrite(HX_SCK_PIN, LOW);
  pinMode(HX_DT_PIN, INPUT_PULLUP);
  Serial.print(F("# hx711 bits: <24 bits> <signed value> <max SCK-high us>, PULSE_US="));
  Serial.println(PULSE_US);
}

void loop() {
  uint32_t waitStart = millis();
  while (digitalRead(HX_DT_PIN) != LOW) {
    if (millis() - waitStart > 500) {
      Serial.println(F("not ready (DT stayed high 500 ms)"));
      return;
    }
  }

  char bits[25];
  uint32_t word = 0;
  uint32_t maxHighUs = 0;
  noInterrupts();
  for (uint8_t i = 0; i < 24; i++) {
    uint32_t start = micros();
    digitalWrite(HX_SCK_PIN, HIGH);
    delayMicroseconds(PULSE_US);
    uint8_t bit = digitalRead(HX_DT_PIN) ? 1 : 0;
    bits[i] = bit ? '1' : '0';
    word = (word << 1) | bit;
    digitalWrite(HX_SCK_PIN, LOW);
    delayMicroseconds(PULSE_US);
    uint32_t held = micros() - start;
    if (held > maxHighUs) maxHighUs = held;
  }
  digitalWrite(HX_SCK_PIN, HIGH);  // 25th pulse: channel A, gain 128
  delayMicroseconds(PULSE_US);
  digitalWrite(HX_SCK_PIN, LOW);
  interrupts();
  bits[24] = '\0';
  if (word & 0x800000UL) word |= 0xFF000000UL;  // sign-extend 24 -> 32 bit

  Serial.print(bits);
  Serial.print(' ');
  Serial.print((int32_t)word);
  Serial.print(' ');
  Serial.println(maxHighUs);
  delay(250);
}