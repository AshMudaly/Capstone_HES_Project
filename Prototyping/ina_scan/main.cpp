// ina_scan: confirms the three INA260 addresses before any power-side test. No pack, no panels, USB power only
// Software I2C on A4 (SDA) and A5 (SCL), same bit-banged routines as the logger, with the address as a parameter
// Expected: #1 0x40 (no jumpers), #2 0x41 (A0 bridged), #3 0x44 (A1 bridged)
// Every 2 s it prints every address that answers (0x08 to 0x77), then reads manufacturer ID, bus voltage and current
// from each expected board. Manufacturer ID must be 0x5449 (Texas Instruments)
#include <Arduino.h>

const uint8_t  SDA_PIN        = A4;
const uint8_t  SCL_PIN        = A5;
const uint8_t  I2C_HALF_US    = 5;
const uint32_t I2C_STRETCH_US = 1000;
const uint8_t  REG_CUR        = 0x01;
const uint8_t  REG_BUSV       = 0x02;
const uint8_t  REG_MFR_ID     = 0xFE;
const uint16_t MFR_ID_TI      = 0x5449;

struct Expected {
  uint8_t addr;
  const char *name;
};
const Expected EXPECTED[] = {{0x40, "#1 ESC"}, {0x41, "#2 solar"}, {0x44, "#3 battery"}};

void lineLow(uint8_t pin) {
  pinMode(pin, OUTPUT);
  digitalWrite(pin, LOW);
}

void lineRelease(uint8_t pin) {
  pinMode(pin, INPUT);
}

bool sclHigh() {
  lineRelease(SCL_PIN);
  uint32_t start = micros();
  while (digitalRead(SCL_PIN) == LOW) {
    if (micros() - start > I2C_STRETCH_US) return false;
  }
  return true;
}

bool i2cStart() {
  lineRelease(SDA_PIN);
  if (!sclHigh()) return false;
  delayMicroseconds(I2C_HALF_US);
  lineLow(SDA_PIN);
  delayMicroseconds(I2C_HALF_US);
  lineLow(SCL_PIN);
  return true;
}

void i2cStop() {
  lineLow(SDA_PIN);
  lineLow(SCL_PIN);
  delayMicroseconds(I2C_HALF_US);
  sclHigh();
  delayMicroseconds(I2C_HALF_US);
  lineRelease(SDA_PIN);
  delayMicroseconds(I2C_HALF_US);
}

bool i2cWrite(uint8_t value) {
  for (uint8_t mask = 0x80; mask; mask >>= 1) {
    if (value & mask) lineRelease(SDA_PIN); else lineLow(SDA_PIN);
    delayMicroseconds(I2C_HALF_US);
    if (!sclHigh()) return false;
    delayMicroseconds(I2C_HALF_US);
    lineLow(SCL_PIN);
  }
  lineRelease(SDA_PIN);
  delayMicroseconds(I2C_HALF_US);
  if (!sclHigh()) return false;
  delayMicroseconds(I2C_HALF_US);
  bool ack = digitalRead(SDA_PIN) == LOW;
  lineLow(SCL_PIN);
  return ack;
}

bool i2cRead(uint8_t &value, bool ack) {
  lineRelease(SDA_PIN);
  value = 0;
  for (uint8_t i = 0; i < 8; i++) {
    delayMicroseconds(I2C_HALF_US);
    if (!sclHigh()) return false;
    delayMicroseconds(I2C_HALF_US);
    value = (value << 1) | (digitalRead(SDA_PIN) ? 1 : 0);
    lineLow(SCL_PIN);
  }
  if (ack) lineLow(SDA_PIN); else lineRelease(SDA_PIN);
  delayMicroseconds(I2C_HALF_US);
  if (!sclHigh()) return false;
  delayMicroseconds(I2C_HALF_US);
  lineLow(SCL_PIN);
  lineRelease(SDA_PIN);
  return true;
}

bool probe(uint8_t addr) {
  bool ack = i2cStart() && i2cWrite(addr << 1);
  i2cStop();
  return ack;
}

bool readReg(uint8_t addr, uint8_t reg, uint16_t &value) {
  uint8_t hi = 0, lo = 0;
  bool ok = i2cStart()
         && i2cWrite(addr << 1) && i2cWrite(reg)
         && i2cStart()
         && i2cWrite((addr << 1) | 1)
         && i2cRead(hi, true) && i2cRead(lo, false);
  i2cStop();
  value = ((uint16_t)hi << 8) | lo;
  return ok;
}

void printHex(uint16_t value) {
  Serial.print(F("0x"));
  Serial.print(value, HEX);
}

void setup() {
  Serial.begin(115200);
  while (!Serial && millis() < 3000) {}
  Serial.println(F("# ina_scan: software I2C on A4 (SDA) and A5 (SCL), expecting 0x40 0x41 0x44"));
}

void loop() {
  lineRelease(SDA_PIN);
  lineRelease(SCL_PIN);
  delay(5);
  if (digitalRead(SDA_PIN) == LOW || digitalRead(SCL_PIN) == LOW) {
    Serial.println(F("# BUS FAULT: SDA or SCL held low (short to GND, or a lead on the wrong pin)"));
    delay(2000);
    return;
  }

  Serial.print(F("# found:"));
  for (uint8_t addr = 0x08; addr < 0x78; addr++) {
    if (probe(addr)) {
      Serial.print(' ');
      printHex(addr);
    }
  }
  Serial.println();

  bool allOk = true;
  for (const Expected &e : EXPECTED) {
    uint16_t mfr = 0, bus = 0, cur = 0;
    bool ok = readReg(e.addr, REG_MFR_ID, mfr) && mfr == MFR_ID_TI
           && readReg(e.addr, REG_BUSV, bus) && readReg(e.addr, REG_CUR, cur);
    printHex(e.addr);
    Serial.print(' ');
    Serial.print(e.name);
    if (ok) {
      Serial.print(F("  mfr="));
      printHex(mfr);
      Serial.print(F("  V="));
      Serial.print(bus * 0.00125f, 3);
      Serial.print(F("  I="));
      Serial.print((int16_t)cur * 0.00125f, 3);
      Serial.println(F(" A"));
    } else {
      allOk = false;
      Serial.println(F("  NOT FOUND (jumper not bridged, or VCC/GND/SDA/SCL wiring)"));
    }
  }
  Serial.println(allOk ? F("# RESULT: PASS") : F("# RESULT: FAIL"));
  delay(2000);
}