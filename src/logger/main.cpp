#include <Arduino.h>

// Phase 1 battery-only logger for the solar-hybrid testbed.
// Pack: Dogcom 4S1P Li-ion, 4 x INR21700-40T, no BMS (all protection is here, the ESC, and the 15 A fuse).
//
// Wiring (unchanged):
//   D9  ESC signal (ESC red/BEC wire removed from the plug)
//   D10 SD chip select, D11 MOSI, D12 MISO, D13 SCK
//   D2  HX711 DT, D3 HX711 SCK
//   A4/A5 INA260 SDA/SCL, INA260 on the ESC branch at address 0x40
//
// Libraries: Adafruit INA260, HX711 (bogde), Servo, SD.
//
// Serial commands, 115200 baud:
//   g        start the sweep (IDLE only; preflight checks run first)
//   x        motor off and close the log (any time)
//   t        re-tare the load cell (IDLE only; nothing may touch it)
//   c<grams> calibrate with a known mass on the cell, e.g. c500 (IDLE only)
//
// One run = one power-up. Each boot opens a new RUNnn.CSV on the SD card.
// CSV rows: ms,throttle_us,bus_V,current_A,power_W,energy_Wh,net_counts,thrust_g
// Lines starting with '#' are comments (pandas: comment='#').
//
// Endurance run: set STEPS_US to a single value and HOLD_MS very large. The
// run ends at MIN_BUS_V and energy_Wh is the energy delivered.

#include <Wire.h>
#include <Servo.h>
#include <SD.h>
#include <HX711.h>
#include <Adafruit_INA260.h>

// ---------- Pins ----------
const uint8_t ESC_PIN    = 9;
const uint8_t SD_CS_PIN  = 10;
const uint8_t HX_DT_PIN  = 2;
const uint8_t HX_SCK_PIN = 3;

// ---------- Pack (datasheet: max charge 4.20 V, recommended cut-off 3.00 V, absolute 2.75 V) ----------
const uint8_t CELLS            = 4;
const float   MAX_CELL_V       = 4.20;
const float   MIN_CELL_V       = 3.00;
const float   MIN_START_CELL_V = 3.40;

const float MIN_BUS_V   = CELLS * MIN_CELL_V;        // 12.0 V, stop while running
const float MIN_START_V = CELLS * MIN_START_CELL_V;  // 13.6 V, refuse to start below this
const float MAX_BUS_V   = CELLS * MAX_CELL_V + 0.1;  // 16.9 V, above a full pack means a fault

// ---------- Limits ----------
// INA260 is rated 15 A and the battery lead has a 15 A fuse, so 12 A keeps an 80% margin.
const float MAX_CURRENT_A      = 12.0;
const float MAX_IDLE_CURRENT_A = 0.5;     // motor off must read about 0 A
const float MAX_THRUST_G       = 3000.0;  // load cell is 10 kg; only checked once calibrated

// ---------- Sweep ----------
// Nominal 1000 + 10 x percent. Check the Hobbywing throttle range if the motor does not respond as expected.
const uint16_t THROTTLE_OFF_US = 1000;
const uint16_t STEPS_US[]      = {1200, 1300, 1400, 1500, 1600, 1700, 1800, 1900, 2000};
const uint8_t  STEP_COUNT      = sizeof(STEPS_US) / sizeof(STEPS_US[0]);
const uint32_t HOLD_MS         = 5000;
const uint32_t ARM_MS          = 3000;   // ESC sees 1000 us for this long after 'g' before step 1
const uint32_t LOG_PERIOD_MS   = 100;
const uint32_t FLUSH_PERIOD_MS = 2000;
const uint32_t IDLE_PRINT_MS   = 1000;

// Counts per gram. 0 = uncalibrated (thrust logs as NA, thrust cut-off off).
// Calibrate with the c command, then paste the printed value here.
const float COUNTS_PER_G = 0.0;

enum State { IDLE, ARMING, RUNNING, STOPPED };

Adafruit_INA260 ina260;
HX711 scale;
Servo esc;
File logFile;

State state = IDLE;
uint8_t step = 0;
uint16_t throttleUs = THROTTLE_OFF_US;
uint32_t armStartMs = 0;
uint32_t stepStartMs = 0;
uint32_t lastLoopMs = 0;
uint32_t lastLogMs = 0;
uint32_t lastFlushMs = 0;
uint32_t lastIdlePrintMs = 0;
long zeroCounts = 0;
long netCounts = 0;
float countsPerG = COUNTS_PER_G;
float energyWh = 0.0;

void motorOff() {
  throttleUs = THROTTLE_OFF_US;
  esc.writeMicroseconds(THROTTLE_OFF_US);
}

void finishRun(const __FlashStringHelper *reason) {
  state = STOPPED;
  logFile.flush();
  logFile.close();
  Serial.print(F("# STOP: "));
  Serial.println(reason);
  Serial.print(F("# energy_Wh: "));
  Serial.println(energyWh, 3);
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

// throttle_us is the value in force when the sample was taken, so a stop row keeps the step that caused it.
void writeRow(Print &out, uint16_t sampledUs, float busV, float currentA, float powerW, float thrustG) {
  out.print(millis());
  out.print(',');
  out.print(sampledUs);
  out.print(',');
  out.print(busV, 3);
  out.print(',');
  out.print(currentA, 3);
  out.print(',');
  out.print(powerW, 2);
  out.print(',');
  out.print(energyWh, 4);
  out.print(',');
  out.print(netCounts);
  out.print(',');
  if (countsPerG != 0.0) {
    out.println(thrustG, 1);
  } else {
    out.println(F("NA"));
  }
}

void tare() {
  zeroCounts = scale.read_average(10);
  netCounts = 0;
  Serial.println(F("# load cell tared"));
}

void calibrate(float knownG) {
  if (knownG <= 0.0) {
    Serial.println(F("# calibrate: give a positive mass in grams, e.g. c500"));
    return;
  }
  long raw = scale.read_average(20) - zeroCounts;
  if (raw == 0) {
    Serial.println(F("# calibrate: no change from tare, is the mass on the cell?"));
    return;
  }
  countsPerG = raw / knownG;
  Serial.print(F("# COUNTS_PER_G = "));
  Serial.println(countsPerG, 4);
}

// Returns nullptr when the sweep may start, otherwise the reason it must not.
const __FlashStringHelper *preflightFailure(float busV, float currentA) {
  if (busV < MIN_START_V) return F("bus voltage too low (pack flat or not connected)");
  if (busV > MAX_BUS_V) return F("bus voltage above full-pack voltage (check wiring and sensor)");
  if (fabs(currentA) > MAX_IDLE_CURRENT_A) return F("motor off but current is not near 0 A (check wiring and sensor)");
  return nullptr;
}

void startSweep() {
  float busV = ina260.readBusVoltage() / 1000.0;
  float currentA = ina260.readCurrent() / 1000.0;
  const __FlashStringHelper *problem = preflightFailure(busV, currentA);
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

void beginStep(uint8_t i) {
  step = i;
  throttleUs = STEPS_US[i];
  esc.writeMicroseconds(throttleUs);
  stepStartMs = millis();
}

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

void setup() {
  Serial.begin(115200);
  Serial.setTimeout(200);
  esc.attach(ESC_PIN);
  motorOff();

  if (!ina260.begin()) {
    Serial.println(F("# INA260 not found, check SDA/SCL/power"));
    while (true) {}
  }
  ina260.setAveragingCount(INA260_COUNT_16);
  ina260.setCurrentConversionTime(INA260_TIME_1_1_ms);
  ina260.setVoltageConversionTime(INA260_TIME_1_1_ms);

  if (!SD.begin(SD_CS_PIN)) {
    Serial.println(F("# SD init failed, check card and wiring"));
    while (true) {}
  }
  if (!openLogFile()) {
    Serial.println(F("# cannot open log file"));
    while (true) {}
  }

  scale.begin(HX_DT_PIN, HX_SCK_PIN);
  if (!scale.wait_ready_timeout(1000)) {
    Serial.println(F("# HX711 not responding, check DT/SCK/power"));
    while (true) {}
  }
  zeroCounts = scale.read_average(10);

  logFile.print(F("# cells="));
  logFile.print(CELLS);
  logFile.print(F(" min_bus_V="));
  logFile.print(MIN_BUS_V, 1);
  logFile.print(F(" max_current_A="));
  logFile.print(MAX_CURRENT_A, 1);
  logFile.print(F(" hold_ms="));
  logFile.println(HOLD_MS);
  const __FlashStringHelper *header = F("ms,throttle_us,bus_V,current_A,power_W,energy_Wh,net_counts,thrust_g");
  logFile.println(header);
  Serial.println(header);

  Serial.println(F("# ready. connect the pack through the precharge lead, then send g"));
}

void loop() {
  if (state == STOPPED) return;
  handleSerial();
  if (state == STOPPED) return;

  uint32_t now = millis();
  float busV = ina260.readBusVoltage() / 1000.0;
  float currentA = ina260.readCurrent() / 1000.0;
  float powerW = ina260.readPower() / 1000.0;
  uint16_t sampledUs = throttleUs;

  if (state == RUNNING) {
    energyWh += powerW * (now - lastLoopMs) / 3600000.0;
  }
  lastLoopMs = now;

  if (scale.is_ready()) {
    netCounts = scale.read() - zeroCounts;
  }
  float thrustG = 0.0;
  if (countsPerG != 0.0) {
    thrustG = netCounts / countsPerG;
  }

  if (state == ARMING && now - armStartMs >= ARM_MS) {
    energyWh = 0.0;
    beginStep(0);
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
        beginStep(step + 1);
      }
    }
    if (stopReason) motorOff();
  }

  bool logging = (state == ARMING || state == RUNNING);
  if (logging && (stopReason || now - lastLogMs >= LOG_PERIOD_MS)) {
    lastLogMs = now;
    writeRow(Serial, sampledUs, busV, currentA, powerW, thrustG);
    writeRow(logFile, sampledUs, busV, currentA, powerW, thrustG);
  }
  if (logging && now - lastFlushMs >= FLUSH_PERIOD_MS) {
    lastFlushMs = now;
    logFile.flush();
  }

  if (!logging && now - lastIdlePrintMs >= IDLE_PRINT_MS) {
    lastIdlePrintMs = now;
    Serial.print(F("# idle V="));
    Serial.print(busV, 2);
    Serial.print(F(" I="));
    Serial.print(currentA, 3);
    Serial.print(F(" thrust_g="));
    if (countsPerG != 0.0) {
      Serial.println(thrustG, 1);
    } else {
      Serial.print(F("NA counts="));
      Serial.println(netCounts);
    }
  }

  if (stopReason) finishRun(stopReason);
}
