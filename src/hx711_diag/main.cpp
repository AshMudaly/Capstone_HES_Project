// HX711 diagnostic. Read-only: no ESC, no SD, no INA260. Pins match the logger.
// One line every 250 ms:  <DT level before wait> <ready within 500 ms: 1/0> <raw 24-bit value or ->
#include <Arduino.h>
#include <HX711.h>

const uint8_t HX_DT_PIN  = 2;
const uint8_t HX_SCK_PIN = 3;

HX711 scale;

void setup() {
  Serial.begin(115200);
  delay(2000);  // let the monitor attach
  scale.begin(HX_DT_PIN, HX_SCK_PIN);
  Serial.println(F("# hx711 diag: dout_before ready raw"));
}

void loop() {
  int doutBefore = digitalRead(HX_DT_PIN);
  bool ready = scale.wait_ready_timeout(500);
  Serial.print(doutBefore);
  Serial.print(' ');
  Serial.print(ready ? 1 : 0);
  Serial.print(' ');
  if (ready) {
    Serial.println(scale.read());
  } else {
    Serial.println('-');
  }
  delay(250);
}
