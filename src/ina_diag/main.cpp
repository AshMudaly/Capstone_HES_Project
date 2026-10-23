// INA260 I2C diagnostic. Wire and Serial only: no HX711, SD or ESC signal.
// Matches the logger's I2C load: same INA260 settings (16 averages, 1.1 ms), three register reads per loop,
// back to back with no delay. Prints every change between OK and FAIL with the error codes.
//
// Codes (R4 Wire.endTransmission): 2 = NACK, 4 = FSP driver error, 5 = timeout (100 ms), 6 = Wire not initialised.
//                                  20 = requestFrom returned fewer than 2 bytes.
//
// Commands, send while it is failing:
//   w  Wire.end() then Wire.begin()   (re-opens the Arduino's I2C controller only)
//   i  write the INA260 reset bit      (resets the INA260's registers only)
//   r  read all three registers once now and print the codes
// Then watch for "# FAIL end".
//
// Optional logger features, one per build (platformio.ini build_flags), to find which one triggers the failure:
//   -DWITH_SERVO  attach the ESC pin (D9) and hold 1000 us, so the Servo timer interrupt runs as in the logger
//   -DWITH_PWM    ESC pulse (D9, 1000 us every 20 ms) from a hardware PWM timer, no interrupt (PwmOut)
//   -DWITH_HX711  clock the HX711 (D2/D3) with the logger's hxRead() every time it is ready (about 10 per second)
//   -DWITH_SD     SD.begin(D10) and open a file for writing, as the logger does in setup()
//   -DWITH_SDWRITE (needs WITH_SD) write a CSV row to the file every 100 ms and flush every 2 s, as a logger run does
//   -DWITH_ROWS   print the same CSV row to Serial every 100 ms, as a logger run does
#include <Arduino.h>
#include <Wire.h>
#ifdef WITH_SERVO
#include <Servo.h>
#endif
#ifdef WITH_SD
#include <SD.h>
#endif
#ifdef WITH_PWM
#include "pwm.h"
#endif

const uint8_t  INA_ADDR   = 0x40;
const uint8_t  REG_CONFIG = 0x00;
const uint8_t  REG_CUR    = 0x01;
const uint8_t  REG_BUSV   = 0x02;
const uint8_t  REG_POWER  = 0x03;
const uint32_t REPORT_MS  = 5000;

#ifdef WITH_SERVO
const uint8_t  ESC_PIN = 9;
Servo esc;
#endif

#ifdef WITH_PWM
PwmOut escPwm(9);
#endif

#ifdef WITH_SD
File logFile;
#endif

#if defined(WITH_SDWRITE) || defined(WITH_ROWS)
const uint32_t ROW_PERIOD_MS   = 100;
const uint32_t FLUSH_PERIOD_MS = 2000;
uint32_t lastRowMs = 0, lastFlushMs = 0;

// Same field layout and widths as the logger's rows (the values are fixed, only the size matters here).
void writeRow(Print &out, uint32_t ms) {
  out.print(ms);
  out.print(',');
  out.print(1000);
  out.print(',');
  out.print(16.679f, 3);
  out.print(',');
  out.print(0.051f, 3);
  out.print(',');
  out.print(0.85f, 2);
  out.print(',');
  out.print(0.0f, 4);
  out.print(',');
  out.print(22);
  out.println(F(",NA"));
}
#endif

#ifdef WITH_HX711
const uint8_t HX_DT_PIN    = 2;
const uint8_t HX_SCK_PIN   = 3;
const uint8_t HX_PULSE_US  = 25;
uint32_t hxReads = 0;

// Same clocking as the logger. Interrupts stay enabled.
long hxRead() {
  uint32_t word = 0;
  for (uint8_t i = 0; i < 24; i++) {
    digitalWrite(HX_SCK_PIN, HIGH);
    delayMicroseconds(HX_PULSE_US);
    word = (word << 1) | (digitalRead(HX_DT_PIN) ? 1 : 0);
    digitalWrite(HX_SCK_PIN, LOW);
    delayMicroseconds(HX_PULSE_US);
  }
  digitalWrite(HX_SCK_PIN, HIGH);
  delayMicroseconds(HX_PULSE_US);
  digitalWrite(HX_SCK_PIN, LOW);
  if (word & 0x800000UL) word |= 0xFF000000UL;
  return (int32_t)word;
}
#endif

bool failing = false;
uint32_t failStartMs = 0;
uint32_t okCount = 0, failCount = 0, episodes = 0;
uint32_t lastReportMs = 0, lastReportTx = 0;

// Returns 0 on success, otherwise the error code of the first step that failed.
uint8_t readReg(uint8_t reg, uint16_t &value) {
  Wire.beginTransmission(INA_ADDR);
  Wire.write(reg);
  uint8_t err = Wire.endTransmission(false);
  if (err != 0) return err;
  if (Wire.requestFrom(INA_ADDR, (size_t)2) != 2) return 20;
  uint16_t hi = Wire.read();
  value = (hi << 8) | Wire.read();
  return 0;
}

void printCodes(uint8_t eBus, uint8_t eCur, uint8_t ePow) {
  Serial.print(F(" errBus="));
  Serial.print(eBus);
  Serial.print(F(" errCur="));
  Serial.print(eCur);
  Serial.print(F(" errPow="));
  Serial.println(ePow);
}

void setup() {
  Serial.begin(115200);
  delay(2000);  // let the monitor attach
#ifdef WITH_SERVO
  esc.attach(ESC_PIN);
  esc.writeMicroseconds(1000);
  Serial.println(F("# WITH_SERVO: ESC pin attached, 1000 us"));
#endif
#ifdef WITH_PWM
  Serial.print(F("# WITH_PWM: hardware PWM on D9, 20000 us period, 1000 us pulse, begin="));
  Serial.println(escPwm.begin(20000, 1000));
#endif
  Wire.begin();
  // Same settings the logger applies through the Adafruit library: 16 averages, 1.1 ms conversions, continuous.
  Wire.beginTransmission(INA_ADDR);
  Wire.write(REG_CONFIG);
  Wire.write((uint8_t)0x65);
  Wire.write((uint8_t)0x27);
  Serial.print(F("# INA260 config written, endTransmission="));
  Serial.println(Wire.endTransmission());
#ifdef WITH_SD
  if (!SD.begin(10)) {
    Serial.println(F("# SD init failed"));
    while (true) {}
  }
  logFile = SD.open("DIAG.TXT", FILE_WRITE);
  if (!logFile) {
    Serial.println(F("# cannot open DIAG.TXT"));
    while (true) {}
  }
  logFile.println(F("# ina_diag"));
  logFile.flush();
  Serial.println(F("# WITH_SD: card open, file DIAG.TXT written"));
#endif
#ifdef WITH_HX711
  pinMode(HX_SCK_PIN, OUTPUT);
  digitalWrite(HX_SCK_PIN, LOW);
  pinMode(HX_DT_PIN, INPUT_PULLUP);
  Serial.println(F("# WITH_HX711: clocking the HX711 whenever it is ready"));
#endif
  Serial.println(F("# ina_diag: reads INA260 0x40 flat out. Commands: w = Wire.end+begin, i = INA260 reset, r = read once"));
}

void loop() {
  while (Serial.available()) {
    char c = Serial.read();
    if (c == 'w') {
      Wire.end();
      Wire.begin();
      Serial.println(F("# Wire.end + Wire.begin done"));
    } else if (c == 'i') {
      Wire.beginTransmission(INA_ADDR);
      Wire.write(REG_CONFIG);
      Wire.write((uint8_t)0x80);
      Wire.write((uint8_t)0x00);
      Serial.print(F("# INA260 reset written, endTransmission="));
      Serial.println(Wire.endTransmission());
    } else if (c == 'r') {
      uint16_t b = 0, a = 0, w = 0;
      uint8_t eBus = readReg(REG_BUSV, b);
      uint8_t eCur = readReg(REG_CUR, a);
      uint8_t ePow = readReg(REG_POWER, w);
      Serial.print(F("# read once:"));
      printCodes(eBus, eCur, ePow);
    }
  }

#if defined(WITH_SDWRITE) || defined(WITH_ROWS)
  uint32_t rowNow = millis();
  if (rowNow - lastRowMs >= ROW_PERIOD_MS) {
    lastRowMs = rowNow;
#ifdef WITH_ROWS
    writeRow(Serial, rowNow);
#endif
#ifdef WITH_SDWRITE
    writeRow(logFile, rowNow);
#endif
  }
#ifdef WITH_SDWRITE
  if (rowNow - lastFlushMs >= FLUSH_PERIOD_MS) {
    lastFlushMs = rowNow;
    logFile.flush();
  }
#endif
#endif

#ifdef WITH_HX711
  if (digitalRead(HX_DT_PIN) == LOW) {
    hxRead();
    hxReads++;
  }
#endif

  uint32_t now = millis();
  uint16_t busRaw = 0, curRaw = 0, powRaw = 0;
  uint8_t eBus = readReg(REG_BUSV, busRaw);
  uint8_t eCur = readReg(REG_CUR, curRaw);
  uint8_t ePow = readReg(REG_POWER, powRaw);
  bool ok = (eBus == 0 && eCur == 0 && ePow == 0);
  if (ok) okCount++; else failCount++;

  if (!ok && !failing) {
    failing = true;
    failStartMs = now;
    episodes++;
    Serial.print(F("# FAIL start t="));
    Serial.print(now);
    Serial.print(F(" ms after "));
    Serial.print(okCount);
    Serial.print(F(" good loops,"));
    printCodes(eBus, eCur, ePow);
  }
  if (ok && failing) {
    failing = false;
    Serial.print(F("# FAIL end after "));
    Serial.print(now - failStartMs);
    Serial.println(F(" ms"));
  }

  if (now - lastReportMs >= REPORT_MS) {
    uint32_t tx = 3 * (okCount + failCount);
    uint32_t txPerSec = (tx - lastReportTx) / (REPORT_MS / 1000);
    lastReportTx = tx;
    lastReportMs = now;
    Serial.print(F("t="));
    Serial.print(now / 1000);
    Serial.print(F(" s ok="));
    Serial.print(okCount);
    Serial.print(F(" fail="));
    Serial.print(failCount);
    Serial.print(F(" episodes="));
    Serial.print(episodes);
    Serial.print(F(" tx/s="));
    Serial.print(txPerSec);
#ifdef WITH_HX711
    Serial.print(F(" hx_reads="));
    Serial.print(hxReads);
#endif
    if (failing) {
      Serial.print(F(" FAILING,"));
      printCodes(eBus, eCur, ePow);
    } else {
      Serial.print(F(" bus_mV="));
      Serial.print(busRaw * 1.25f, 0);
      Serial.print(F(" cur_mA="));
      Serial.println((int16_t)curRaw * 1.25f, 1);
    }
  }
}
