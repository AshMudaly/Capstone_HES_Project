// HX711 diagnostic. Read-only
// One line every 250 ms:  <DT level before wait> <ready within 500 ms: 1/0> <raw 24-bit value or ->
#include <Arduino.h>

const uint8_t HX_DT_PIN  = 2;
const uint8_t HX_SCK_PIN = 3;

// Read one HX711 sample using channel A at gain 128.
bool waitReadyTimeout(unsigned long timeoutMs) {
  const unsigned long started = millis();
  while (digitalRead(HX_DT_PIN) != LOW) {
    if (millis() - started >= timeoutMs) return false;
    delay(1);
  }
  return true;
}

long readRaw() {
  uint32_t value = 0;
  noInterrupts();
  for (uint8_t bit = 0; bit < 24; ++bit) {
    digitalWrite(HX_SCK_PIN, HIGH);
    value = (value << 1) | (digitalRead(HX_DT_PIN) ? 1UL : 0UL);
    digitalWrite(HX_SCK_PIN, LOW);
  }
  // The 25th pulse selects channel A and gain 128 for the next conversion.
  digitalWrite(HX_SCK_PIN, HIGH);
  digitalWrite(HX_SCK_PIN, LOW);
  interrupts();

  if (value & 0x800000UL) value |= 0xFF000000UL;
  return static_cast<long>(static_cast<int32_t>(value));
}

void setup() {
  Serial.begin(115200);
  delay(2000);  // let the monitor attach
  pinMode(HX_DT_PIN, INPUT);
  pinMode(HX_SCK_PIN, OUTPUT);
  digitalWrite(HX_SCK_PIN, LOW);
  Serial.println(F("# hx711 diag: dout_before ready raw"));
}

void loop() {
  int doutBefore = digitalRead(HX_DT_PIN);
  bool ready = waitReadyTimeout(500);
  Serial.print(doutBefore);
  Serial.print(' ');
  Serial.print(ready ? 1 : 0);
  Serial.print(' ');
  if (ready) {
    Serial.println(readRaw());
  } else {
    Serial.println('-');
  }
  delay(250);
}
