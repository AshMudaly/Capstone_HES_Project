#include <Arduino.h>

// Runs a motor throttle test and logs power, energy, and thrust to Serial and an SD card.
// Connect the sensors and ESC, open Serial Monitor at 115200 baud, and send g to start.
// Send t to zero the load cell, c<grams> to calibrate it (for example c500), or x to stop.
// Each power-up creates a new RUNnn.CSV. CSV comment lines start with #.
// To run at one throttle for longer, set STEPS_US to one value and increase HOLD_MS.

#include "pwm.h"
#include <SD.h>

// Pin assignments for the ESC, SD card, and load cell, it will dioffer with your board and wiring. 
//The INA260 is on the ESC branch at address 0x40
const uint8_t ESC_PIN    = 9;
const uint8_t SD_CS_PIN  = 10;
const uint8_t HX_DT_PIN  = 2;
const uint8_t HX_SCK_PIN = 3;

// HX711 clock pulse width; keep it below the chip's 60 us reset limit
const uint8_t HX_PULSE_US = 25;

// Battery voltage thresholds. The test will not start below MIN_START_V and will stop below MIN_BUS_V
const uint8_t CELLS            = 4;
const float   MAX_CELL_V       = 4.20;
const float   MIN_CELL_V       = 3.00;
const float   MIN_START_CELL_V = 3.40;

const float MIN_BUS_V   = CELLS * MIN_CELL_V;
const float MIN_START_V = CELLS * MIN_START_CELL_V;
const float MAX_BUS_V   = CELLS * MAX_CELL_V + 0.1;

// Safety limits checked before and during run.
const float MAX_CURRENT_A      = 12.0;
const float MAX_IDLE_CURRENT_A = 0.5;
const float MAX_THRUST_G       = 3000.0;


// Edit these values to change throttle steps and how long each step runs
const uint16_t THROTTLE_OFF_US = 1000;
const uint16_t STEPS_US[]      = {1200, 1300, 1400, 1500, 1600, 1700};
const uint8_t  STEP_COUNT      = sizeof(STEPS_US) / sizeof(STEPS_US[0]);
const uint32_t HOLD_MS         = 5000;
const uint32_t ARM_MS          = 3000;
const uint32_t LOG_PERIOD_MS   = 100;
const uint32_t IDLE_PRINT_MS   = 1000;

// Calculated figure from the calibration.cpp, using known kitchen-scale weights and the load cell
// The value is negative because the load cell counts decrease as the thrust increases
const float COUNTS_PER_G = -215.45;

enum State { IDLE, ARMING, RUNNING, STOPPED };

PwmOut esc(ESC_PIN);
File logFile;

State state = IDLE;
uint8_t step = 0;
uint16_t throttleUs = THROTTLE_OFF_US;
uint32_t armStartMs = 0;
uint32_t stepStartMs = 0;
uint32_t lastLoopMs = 0;
uint32_t lastLogMs = 0;
uint32_t lastIdlePrintMs = 0;
long zeroCounts = 0;
long netCounts = 0;
float countsPerG = COUNTS_PER_G;
float energyWh = 0.0;
float solarWh = 0.0;
float battWh = 0.0;

// Voltage, current, and calculated power from one INA260 sensor
struct InaReading {
  float busV = 0;
  float currentA = 0;
  float powerW = 0;
};

// Load-cell reading, tare, and calibration
bool hxReady() {
  return digitalRead(HX_DT_PIN) == LOW;
}

bool hxWaitReady(uint32_t timeoutMs) {
  uint32_t start = millis();
  while (!hxReady()) {
    if (millis() - start > timeoutMs) return false;
    delay(1);
  }
  return true;
}

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

void hxHalt() {
  Serial.println(F("# HX711 not responding, check DT/SCK/power"));
  while (true) {}
}

long hxReadAverage(uint8_t times) {
  long sum = 0;
  for (uint8_t i = 0; i < times; i++) {
    if (!hxWaitReady(1000)) hxHalt();
    sum += hxRead();
  }
  return sum / times;
}

// ESC control. A PWM failure stops program rather than continuing without motor control
void escWrite(uint16_t us) {
  if (!esc.pulseWidth_us(us)) {
    esc.end();
    Serial.println(F("# ESC PWM update failed, signal stopped"));
    while (true) {}
  }
}

void motorOff() {
  throttleUs = THROTTLE_OFF_US;
  escWrite(THROTTLE_OFF_US);
}

void finishRun(const __FlashStringHelper *reason) {
  state = STOPPED;
  logFile.print(F("# STOP: "));
  logFile.println(reason);
  logFile.print(F("# energy_Wh: "));
  logFile.println(energyWh, 3);
  logFile.print(F("# solar_Wh: "));
  logFile.println(solarWh, 3);
  logFile.print(F("# batt_Wh: "));
  logFile.println(battWh, 3);
  logFile.flush();
  logFile.close();
  Serial.print(F("# STOP: "));
  Serial.println(reason);
  Serial.print(F("# energy_Wh: "));
  Serial.println(energyWh, 3);
  Serial.print(F("# solar_Wh: "));
  Serial.println(solarWh, 3);
  Serial.print(F("# batt_Wh: "));
  Serial.println(battWh, 3);
}

bool openLogFile() {
  char name[13];
  for (uint8_t i = 0; i < 100; i++) {
    snprintf(name, sizeof(name), "RUN%02u.CSV", (unsigned int)i);
    if (!SD.exists(name)) {
      logFile = SD.open(name, FILE_WRITE);
      if (!logFile) return false;
      Serial.print(F("# logging to "));
      Serial.println(name);
      return true;
    }
  }
  return false;
}

// Each row records throttle, motor/battery/solar readings, energy, and load-cell thrust
// Solar and battery current are positive when supplying the bus; battery current is negative while charging
void writeRow(Print &out, uint16_t sampledUs, const InaReading &inaEsc, const InaReading &inaSolar, const InaReading &inaBatt, float thrustG) {
  out.print(millis());
  out.print(',');
  out.print(sampledUs);
  out.print(',');
  out.print(inaEsc.busV, 3);
  out.print(',');
  out.print(inaEsc.currentA, 3);
  out.print(',');
  out.print(inaEsc.powerW, 2);
  out.print(',');
  out.print(energyWh, 4);
  out.print(',');
  out.print(netCounts);
  out.print(',');
  if (countsPerG != 0.0) {
    out.print(thrustG, 1);
  } else {
    out.print(F("NA"));
  }
  out.print(',');
  out.print(inaSolar.currentA, 3);
  out.print(',');
  out.print(inaSolar.powerW, 2);
  out.print(',');
  out.print(solarWh, 4);
  out.print(',');
  out.print(inaBatt.busV, 3);
  out.print(',');
  out.print(inaBatt.currentA, 3);
  out.print(',');
  out.print(inaBatt.powerW, 2);
  out.print(',');
  out.print(battWh, 4);
  out.println();
}

void tare() {
  zeroCounts = hxReadAverage(10);
  netCounts = 0;
  Serial.println(F("# load cell tared"));
}

void calibrate(float knownG) {
  if (knownG <= 0.0) {
    Serial.println(F("# calibrate: give a positive mass in grams, e.g. c500"));
    return;
  }
  long raw = hxReadAverage(20) - zeroCounts;
  if (raw == 0) {
    Serial.println(F("# calibrate: no change from tare, is the mass on the cell?"));
    return;
  }
  countsPerG = raw / knownG;
  Serial.print(F("# COUNTS_PER_G = "));
  Serial.println(countsPerG, 4);
}

// INA260 sensors share A4/A5: motor at 0x40, solar at 0x41, and battery at 0x44
// MAKE SURE ADDRESS JUMPERS ARE SET CORRECTLY: 0x40 = no jumpers, 0x41 = A0 bridged, 0x44 = A1 bridged!!!
// These helpers read and configure the sensors using software I2C
const uint8_t  INA_SDA_PIN    = A4;
const uint8_t  INA_SCL_PIN    = A5;
const uint8_t  INA_ESC_ADDR   = 0x40;
const uint8_t  INA_SOLAR_ADDR = 0x41;
const uint8_t  INA_BATT_ADDR  = 0x44;
const uint8_t  INA_REG_CONFIG = 0x00;
const uint8_t  INA_REG_CUR    = 0x01;
const uint8_t  INA_REG_BUSV   = 0x02;
const uint8_t  INA_REG_MFR_ID = 0xFE;
const uint16_t INA_MFR_ID_TI  = 0x5449;
const uint16_t INA_CONFIG     = 0x6527;
const uint8_t  I2C_HALF_US    = 5;
const uint32_t I2C_STRETCH_US = 1000;

void lineLow(uint8_t pin) {
  pinMode(pin, OUTPUT);
  digitalWrite(pin, LOW);
}

void lineRelease(uint8_t pin) {
  pinMode(pin, INPUT);
}

bool sclHigh() {
  lineRelease(INA_SCL_PIN);
  uint32_t start = micros();
  while (digitalRead(INA_SCL_PIN) == LOW) {
    if (micros() - start > I2C_STRETCH_US) return false;
  }
  return true;
}

bool i2cStart() {
  lineRelease(INA_SDA_PIN);
  if (!sclHigh()) return false;
  delayMicroseconds(I2C_HALF_US);
  lineLow(INA_SDA_PIN);
  delayMicroseconds(I2C_HALF_US);
  lineLow(INA_SCL_PIN);
  return true;
}

void i2cStop() {
  lineLow(INA_SDA_PIN);
  lineLow(INA_SCL_PIN);
  delayMicroseconds(I2C_HALF_US);
  sclHigh();
  delayMicroseconds(I2C_HALF_US);
  lineRelease(INA_SDA_PIN);
  delayMicroseconds(I2C_HALF_US);
}

bool i2cWrite(uint8_t value) {
  for (uint8_t mask = 0x80; mask; mask >>= 1) {
    if (value & mask) lineRelease(INA_SDA_PIN); else lineLow(INA_SDA_PIN);
    delayMicroseconds(I2C_HALF_US);
    if (!sclHigh()) return false;
    delayMicroseconds(I2C_HALF_US);
    lineLow(INA_SCL_PIN);
  }
  lineRelease(INA_SDA_PIN);
  delayMicroseconds(I2C_HALF_US);
  if (!sclHigh()) return false;
  delayMicroseconds(I2C_HALF_US);
  bool ack = digitalRead(INA_SDA_PIN) == LOW;
  lineLow(INA_SCL_PIN);
  return ack;
}

bool i2cRead(uint8_t &value, bool ack) {
  lineRelease(INA_SDA_PIN);
  value = 0;
  for (uint8_t i = 0; i < 8; i++) {
    delayMicroseconds(I2C_HALF_US);
    if (!sclHigh()) return false;
    delayMicroseconds(I2C_HALF_US);
    value = (value << 1) | (digitalRead(INA_SDA_PIN) ? 1 : 0);
    lineLow(INA_SCL_PIN);
  }
  if (ack) lineLow(INA_SDA_PIN); else lineRelease(INA_SDA_PIN);
  delayMicroseconds(I2C_HALF_US);
  if (!sclHigh()) return false;
  delayMicroseconds(I2C_HALF_US);
  lineLow(INA_SCL_PIN);
  lineRelease(INA_SDA_PIN);
  return true;
}

bool inaReadReg(uint8_t addr, uint8_t reg, uint16_t &value) {
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

bool inaWriteReg(uint8_t addr, uint8_t reg, uint16_t value) {
  bool ok = i2cStart()
         && i2cWrite(addr << 1) && i2cWrite(reg) && i2cWrite(value >> 8) && i2cWrite(value & 0xFF);
  i2cStop();
  return ok;
}

// Read sensor values and calculate signed power. Charging current is negative.
bool readIna(uint8_t addr, InaReading &r) {
  uint16_t bus = 0, cur = 0;
  if (!inaReadReg(addr, INA_REG_BUSV, bus) || !inaReadReg(addr, INA_REG_CUR, cur)) return false;
  r.busV = bus * 0.00125f;
  r.currentA = (int16_t)cur * 0.00125f;
  r.powerW = r.busV * r.currentA;
  return true;
}

bool readAllIna(InaReading &inaEsc, InaReading &inaSolar, InaReading &inaBatt) {
  return readIna(INA_ESC_ADDR, inaEsc) && readIna(INA_SOLAR_ADDR, inaSolar) && readIna(INA_BATT_ADDR, inaBatt);
}

bool inaInit(uint8_t addr) {
  lineRelease(INA_SDA_PIN);
  lineRelease(INA_SCL_PIN);
  uint16_t id = 0;
  if (!inaReadReg(addr, INA_REG_MFR_ID, id) || id != INA_MFR_ID_TI) return false;
  return inaWriteReg(addr, INA_REG_CONFIG, INA_CONFIG);
}

// Check the battery voltage and idle motor current before starting
const __FlashStringHelper *preflightFailure(float busV, float currentA) {
  if (busV < MIN_START_V) return F("bus voltage too low (pack flat or not connected)");
  if (busV > MAX_BUS_V) return F("bus voltage above full-pack voltage (check wiring and sensor)");
  if (fabs(currentA) > MAX_IDLE_CURRENT_A) return F("motor off but current is not near 0 A (check wiring and sensor)");
  return nullptr;
}

// A g command starts only after sensor readings and battery/current safety checks pass
// The ESC is held at 1000 us for ARM_MS before the first step

void startSweep() {
  InaReading inaEsc, inaSolar, inaBatt;
  if (!readAllIna(inaEsc, inaSolar, inaBatt)) {
    Serial.println(F("# NOT STARTED: INA260 read failed (I2C), try g again"));
    return;
  }
  const __FlashStringHelper *problem = preflightFailure(inaEsc.busV, inaEsc.currentA);
  if (problem) {
    Serial.print(F("# NOT STARTED: "));
    Serial.println(problem);
    return;
  }
  logFile.print(F("# counts_per_g="));
  logFile.println(countsPerG, 4);
  armStartMs = millis();
  state = ARMING;
  Serial.println(F("# arming, ESC held at 1000 us"));
}

// Start one configured throttle step
void beginStep(uint8_t i, uint32_t now) {
  step = i;
  throttleUs = STEPS_US[i];
  escWrite(throttleUs);
  stepStartMs = now;
}

// Read the available Serial commands. Start, tare, and calibration are only accepted while idle
void handleSerial() {
  while (Serial.available()) {
    char c = Serial.read();
    if (c == 'x') {
      motorOff();
      finishRun(F("operator"));
      return;
    }
    if (state != IDLE) continue;
    if (c == 'g') {
      startSweep();
    } else if (c == 't') {
      tare();
    } else if (c == 'c') {
      calibrate(Serial.parseFloat());
    }
  }
}

// Start the sensors and ESC, create a fresh log, and zero the load cell
void setup() {
  Serial.begin(115200);
  Serial.setTimeout(200);
  if (!esc.begin(20000u, (uint32_t)THROTTLE_OFF_US)) {
    Serial.println(F("# ESC PWM failed to start on D9"));
    while (true) {}
  }

  if (!inaInit(INA_ESC_ADDR) || !inaInit(INA_SOLAR_ADDR) || !inaInit(INA_BATT_ADDR)) {
    Serial.println(F("# INA260 not found at 0x40, 0x41 or 0x44, run ina_scan"));
    while (true) {}
  }

  if (!SD.begin(SD_CS_PIN)) {
    Serial.println(F("# SD init failed, check card and wiring"));
    while (true) {}
  }
  if (!openLogFile()) {
    Serial.println(F("# cannot open log file"));
    while (true) {}
  }

  pinMode(HX_SCK_PIN, OUTPUT);
  digitalWrite(HX_SCK_PIN, LOW);
  pinMode(HX_DT_PIN, INPUT_PULLUP);
  if (!hxWaitReady(1000)) hxHalt();
  zeroCounts = hxReadAverage(10);

  logFile.print(F("# cells="));
  logFile.print(CELLS);
  logFile.print(F(" min_bus_V="));
  logFile.print(MIN_BUS_V, 1);
  logFile.print(F(" max_current_A="));
  logFile.print(MAX_CURRENT_A, 1);
  logFile.print(F(" hold_ms="));
  logFile.print(HOLD_MS);
  logFile.println(F(" build=logger_nosd"));
  const __FlashStringHelper *header = F("ms,throttle_us,bus_V,current_A,power_W,energy_Wh,net_counts,thrust_g,solar_A,solar_W,solar_Wh,batt_V,batt_A,batt_W,batt_Wh");
  logFile.println(F("# solar_* is INA260 0x41, positive when the MPPT feeds the bus. batt_* is INA260 0x44, positive when the pack discharges."));
  logFile.println(header);
  Serial.println(header);

  Serial.println(F("# ready. connect the pack through the precharge lead, then send g"));
}

// Read sensors, update energy and thrust, advance the throttle steps, and stop on completion or a safety limit
void loop() {
  if (state == STOPPED) return;
  handleSerial();
  if (state == STOPPED) return;

  uint32_t now = millis();
  InaReading inaEsc, inaSolar, inaBatt;
  bool inaFailed = !readAllIna(inaEsc, inaSolar, inaBatt);
  float busV = inaEsc.busV, currentA = inaEsc.currentA;
  uint16_t sampledUs = throttleUs;
  if (inaFailed && (state == ARMING || state == RUNNING)) {
    motorOff();
    finishRun(F("INA260 read failed"));
    return;
  }

  if (state == RUNNING) {
    double dtH = (now - lastLoopMs) / 3600000.0;
    energyWh += inaEsc.powerW * dtH;
    solarWh += inaSolar.powerW * dtH;
    battWh += inaBatt.powerW * dtH;
  }
  lastLoopMs = now;

  if (hxReady()) {
    netCounts = hxRead() - zeroCounts;
  }
  float thrustG = 0.0;
  if (countsPerG != 0.0) {
    thrustG = netCounts / countsPerG;
  }

  if (state == ARMING && now - armStartMs >= ARM_MS) {
    energyWh = 0.0;
    solarWh = 0.0;
    battWh = 0.0;
    beginStep(0, now);
    state = RUNNING;
  }

  const __FlashStringHelper *stopReason = nullptr;
  if (state == RUNNING) {
    if (currentA > MAX_CURRENT_A) {
      stopReason = F("over current");
    } else if (busV < MIN_BUS_V) {
      stopReason = F("under voltage");
    } else if (countsPerG != 0.0 && fabs(thrustG) > MAX_THRUST_G) {
      stopReason = F("over thrust");
    } else if (now - stepStartMs >= HOLD_MS) {
      if (step + 1 >= STEP_COUNT) {
        stopReason = F("sequence complete");
      } else {
        beginStep(step + 1, now);
      }
    }
    if (stopReason) motorOff();
  }

  bool logging = (state == ARMING || state == RUNNING);
  if (logging && (stopReason || now - lastLogMs >= LOG_PERIOD_MS)) {
    lastLogMs = now;
    writeRow(Serial, sampledUs, inaEsc, inaSolar, inaBatt, thrustG);
  }

  if (!logging && now - lastIdlePrintMs >= IDLE_PRINT_MS) {
    lastIdlePrintMs = now;
    if (inaFailed) {
      Serial.println(F("# idle INA260 READ FAILED"));
    } else {
      Serial.print(F("# idle V="));
      Serial.print(busV, 2);
      Serial.print(F(" I="));
      Serial.print(currentA, 3);
      Serial.print(F(" solar_A="));
      Serial.print(inaSolar.currentA, 3);
      Serial.print(F(" batt_A="));
      Serial.print(inaBatt.currentA, 3);
      Serial.print(F(" thrust_g="));
      if (countsPerG != 0.0) {
        Serial.println(thrustG, 1);
      } else {
        Serial.print(F("NA counts="));
        Serial.println(netCounts);
      }
    }
  }

  if (stopReason) finishRun(stopReason);
}