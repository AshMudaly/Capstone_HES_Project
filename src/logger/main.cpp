#include <Arduino.h>

// Phase 1 battery-only logger for the solar-hybrid testbed.
// Pack: Dogcom 4S1P Li-ion, 4 x INR21700-40T, no BMS (all protection is here, the ESC, and the 15 A fuse).
//
// Wiring (unchanged):
//   D9  ESC signal (ESC red/BEC wire removed from the plug)
//   D10 SD chip select, D11 MOSI, D12 MISO, D13 SCK
//   D2  HX711 DT, D3 HX711 SCK
//   A4/A5 INA260 SDA/SCL, three INA260s: ESC branch 0x40, solar branch 0x41 (A0 bridged), pack lead 0x44 (A1 bridged)
//
// Libraries: SD. The HX711 and the INA260 are driven directly (see hxRead, readIna). The ESC pulse comes from a hardware
// PWM timer (PwmOut), not the Servo library: the Servo timer interrupt made the I2C reads fail.
//
// Serial commands, 115200 baud:
//   g        start the sweep (IDLE only; preflight checks run first)
//   x        motor off and close the log (any time)
//   t        re-tare the load cell (IDLE only; nothing may touch it)
//   c<grams> calibrate with a known mass on the cell, e.g. c500 (IDLE only)
//
// One run = one power-up. Each boot opens a new RUNnn.CSV on the SD card.
// CSV rows: ms,throttle_us,bus_V,current_A,power_W,energy_Wh,net_counts,thrust_g,solar_A,solar_W,solar_Wh,batt_V,batt_A,batt_W,batt_Wh
// bus_V, current_A, power_W, energy_Wh are the ESC branch. solar_* is positive when the MPPT feeds the bus, batt_* is
// positive when the pack discharges. Power is bus voltage x current, so it is negative when the pack charges.
// Lines starting with '#' are comments (pandas: comment='#').
//
// Endurance run: set STEPS_US to a single value and HOLD_MS very large. The
// run ends at MIN_BUS_V and energy_Wh is the energy delivered.

#include "pwm.h"
#include <SD.h>

// ---------- Pins ----------
const uint8_t ESC_PIN    = 9;
const uint8_t SD_CS_PIN  = 10;
const uint8_t HX_DT_PIN  = 2;
const uint8_t HX_SCK_PIN = 3;

// HX711 SCK high time and low time in microseconds. Measured on this board: at 10 SPS the chip's output
// follows the clock about 20 us after the first rising edge, and the library's clock is far faster, so
// the first bits of every word read wrong. The HX711 resets if SCK stays high longer than 60 us.
const uint8_t HX_PULSE_US = 25;

#ifdef HX_TIMING
// Diagnostic build (platformio.ini env logger_hxtime), normally not defined. The question it answers: are the
// corrupted load cell rows the reads where something stretched an SCK-high phase (timing), or do they have a
// normal clock (electrical noise on DT/SCK)? Every read records its longest SCK-high phase and its total
// duration, and each CSV row carries the figures of the read that produced its net_counts.
// The clock is timed around the high phase (just before the rising edge to just after the falling edge), so the
// timing calls add nothing inside it. The figure includes the two digitalWrite calls, so a normal high phase
// reads a little above HX_PULSE_US. The overrun flag uses 40 us: clearly above normal, below the 60 us reset limit.
const uint32_t HX_OVERRUN_US = 40;
uint32_t hxLastMaxHighUs = 0;  // longest SCK-high phase of the most recent read
uint32_t hxLastReadUs    = 0;  // duration of the most recent read
uint32_t hxReads         = 0;  // counted from 'g'
uint32_t hxOverruns      = 0;  // reads with a high phase over HX_OVERRUN_US, counted from 'g'
uint32_t hxMaxHighUs     = 0;  // worst high phase, counted from 'g'
#endif

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

PwmOut esc(ESC_PIN);  // 50 Hz, pulse width = throttle in us
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
float solarWh = 0.0;
float battWh = 0.0;

// One INA260 sample. powerW is busV x currentA, so it carries the current's sign.
struct InaReading {
  float busV = 0;
  float currentA = 0;
  float powerW = 0;
};

// ---------- HX711 (24-bit, channel A, gain 128) ----------
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

// Clocks out one conversion. Call only when hxReady().
// Interrupts stay enabled: an interrupt can only lengthen the clock-high time by a few us, far below the
// 60 us reset limit.
long hxRead() {
#ifdef HX_TIMING
  uint32_t readStartUs = micros();
  uint32_t maxHighUs = 0;
  uint32_t highStartUs = 0;
  uint32_t highUs = 0;
#endif
  uint32_t word = 0;
  for (uint8_t i = 0; i < 24; i++) {
#ifdef HX_TIMING
    highStartUs = micros();
#endif
    digitalWrite(HX_SCK_PIN, HIGH);
    delayMicroseconds(HX_PULSE_US);
    word = (word << 1) | (digitalRead(HX_DT_PIN) ? 1 : 0);
    digitalWrite(HX_SCK_PIN, LOW);
#ifdef HX_TIMING
    highUs = micros() - highStartUs;
    if (highUs > maxHighUs) maxHighUs = highUs;
#endif
    delayMicroseconds(HX_PULSE_US);
  }
#ifdef HX_TIMING
  highStartUs = micros();
#endif
  digitalWrite(HX_SCK_PIN, HIGH);  // 25th pulse selects channel A, gain 128 for the next conversion
  delayMicroseconds(HX_PULSE_US);
  digitalWrite(HX_SCK_PIN, LOW);
#ifdef HX_TIMING
  highUs = micros() - highStartUs;
  if (highUs > maxHighUs) maxHighUs = highUs;
  hxLastMaxHighUs = maxHighUs;
  hxLastReadUs = micros() - readStartUs;
  hxReads++;
  if (maxHighUs > HX_OVERRUN_US) hxOverruns++;
  if (maxHighUs > hxMaxHighUs) hxMaxHighUs = maxHighUs;
#endif
  if (word & 0x800000UL) word |= 0xFF000000UL;  // sign-extend 24 -> 32 bit
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

// Sets the ESC pulse width. If the timer refuses, stop the signal (the ESC cuts the motor on signal loss) and halt.
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

#ifdef HX_TIMING
void printHxSummary(Print &out) {
  out.print(F("# hx_reads="));
  out.print(hxReads);
  out.print(F(" hx_overruns_over_"));
  out.print(HX_OVERRUN_US);
  out.print(F("us="));
  out.print(hxOverruns);
  out.print(F(" hx_max_high_us="));
  out.println(hxMaxHighUs);
}
#endif

void finishRun(const __FlashStringHelper *reason) {
  state = STOPPED;
  // The file records why the run ended, so a CSV on its own tells the whole story.
  logFile.print(F("# STOP: "));
  logFile.println(reason);
  logFile.print(F("# energy_Wh: "));
  logFile.println(energyWh, 3);
  logFile.print(F("# solar_Wh: "));
  logFile.println(solarWh, 3);
  logFile.print(F("# batt_Wh: "));
  logFile.println(battWh, 3);
#ifdef HX_TIMING
  printHxSummary(logFile);
#endif
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
#ifdef HX_TIMING
  printHxSummary(Serial);
#endif
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
#ifdef HX_TIMING
  // Last two columns are the timing of the read that produced net_counts (see the HX_TIMING comment).
  out.print(',');
  out.print(hxLastMaxHighUs);
  out.print(',');
  out.print(hxLastReadUs);
#endif
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

// ---------- INA260 over software I2C on A4 (SDA) and A5 (SCL) ----------
// The board's hardware I2C controller aborts transfers at random while the USB, SPI and timer interrupts run
// (Wire.endTransmission() = 2 with both lines idle high), so the bus is driven by hand instead. A line is
// driven low by making the pin an output at 0 and released by making it an input; the INA260 board's pull-ups
// bring it high. An interrupt can only make a clock phase longer, which I2C allows.
const uint8_t  INA_SDA_PIN    = A4;
const uint8_t  INA_SCL_PIN    = A5;
const uint8_t  INA_ESC_ADDR   = 0x40;  // ESC branch, no jumpers
const uint8_t  INA_SOLAR_ADDR = 0x41;  // solar branch, A0 bridged
const uint8_t  INA_BATT_ADDR  = 0x44;  // pack lead, A1 bridged
const uint8_t  INA_REG_CONFIG = 0x00;
const uint8_t  INA_REG_CUR    = 0x01;
const uint8_t  INA_REG_BUSV   = 0x02;
const uint8_t  INA_REG_MFR_ID = 0xFE;
const uint16_t INA_MFR_ID_TI  = 0x5449;
const uint16_t INA_CONFIG     = 0x6527;  // 16 averages, 1.1 ms conversions, continuous (reset value 0x6127 is 1 average)
const uint8_t  I2C_HALF_US    = 5;
const uint32_t I2C_STRETCH_US = 1000;

void lineLow(uint8_t pin) {
  pinMode(pin, OUTPUT);
  digitalWrite(pin, LOW);
}

void lineRelease(uint8_t pin) {
  pinMode(pin, INPUT);
}

// Releases SCL and waits for it to go high. False if something holds it low.
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

// Leaves both lines released. Called after every transfer, failed or not.
void i2cStop() {
  lineLow(INA_SDA_PIN);
  lineLow(INA_SCL_PIN);
  delayMicroseconds(I2C_HALF_US);
  sclHigh();
  delayMicroseconds(I2C_HALF_US);
  lineRelease(INA_SDA_PIN);
  delayMicroseconds(I2C_HALF_US);
}

// Sends one byte, returns true if the INA260 pulled SDA low for the ninth clock (ACK).
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

// Reads one byte and answers with ACK (more bytes follow) or NACK (last byte). False if SCL is held low.
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

// Reads bus voltage (1.25 mV/LSB) and current (signed, 1.25 mA/LSB). Power is computed here as their product, so it
// carries the current's sign (the battery branch goes negative while the pack charges). False if any transfer failed.
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

// True if a TI INA260 answers, and it is then configured.
bool inaInit(uint8_t addr) {
  lineRelease(INA_SDA_PIN);
  lineRelease(INA_SCL_PIN);
  uint16_t id = 0;
  if (!inaReadReg(addr, INA_REG_MFR_ID, id) || id != INA_MFR_ID_TI) return false;
  return inaWriteReg(addr, INA_REG_CONFIG, INA_CONFIG);
}

// Returns nullptr when the sweep may start, otherwise the reason it must not.
const __FlashStringHelper *preflightFailure(float busV, float currentA) {
  if (busV < MIN_START_V) return F("bus voltage too low (pack flat or not connected)");
  if (busV > MAX_BUS_V) return F("bus voltage above full-pack voltage (check wiring and sensor)");
  if (fabs(currentA) > MAX_IDLE_CURRENT_A) return F("motor off but current is not near 0 A (check wiring and sensor)");
  return nullptr;
}

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
#ifdef HX_TIMING
  hxReads = 0;
  hxOverruns = 0;
  hxMaxHighUs = 0;
#endif
  armStartMs = millis();
  state = ARMING;
  Serial.println(F("# arming, ESC held at 1000 us"));
}

// stepStartMs must come from the loop's own `now`: a later millis() would be ahead of `now`, and the hold check
// `now - stepStartMs` in the same pass would wrap and end the step at once.
void beginStep(uint8_t i, uint32_t now) {
  step = i;
  throttleUs = STEPS_US[i];
  escWrite(throttleUs);
  stepStartMs = now;
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
#if defined(NO_SD_ROWS)
  logFile.println(F(" build=logger_nosd"));
#elif defined(NO_SERIAL_ROWS)
  logFile.println(F(" build=logger_noserial"));
#elif defined(HX_TIMING)
  logFile.println(F(" build=logger_hxtime"));
#else
  logFile.println(F(" build=logger"));
#endif
#ifdef HX_TIMING
  const __FlashStringHelper *header = F("ms,throttle_us,bus_V,current_A,power_W,energy_Wh,net_counts,thrust_g,solar_A,solar_W,solar_Wh,batt_V,batt_A,batt_W,batt_Wh,hx_max_high_us,hx_read_us");
#else
  const __FlashStringHelper *header = F("ms,throttle_us,bus_V,current_A,power_W,energy_Wh,net_counts,thrust_g,solar_A,solar_W,solar_Wh,batt_V,batt_A,batt_W,batt_Wh");
#endif
  logFile.println(F("# solar_* is INA260 0x41, positive when the MPPT feeds the bus. batt_* is INA260 0x44, positive when the pack discharges."));
  logFile.println(header);
  Serial.println(header);
#ifdef HX_TIMING
  Serial.println(F("# build=logger_hxtime (HX711 timing columns on)"));
#endif

  Serial.println(F("# ready. connect the pack through the precharge lead, then send g"));
}

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
#ifndef NO_SERIAL_ROWS  // diagnostic switch (platformio.ini env logger_noserial), normally not defined
    writeRow(Serial, sampledUs, inaEsc, inaSolar, inaBatt, thrustG);
#endif
#ifndef NO_SD_ROWS      // diagnostic switch (env logger_nosd), normally not defined
    writeRow(logFile, sampledUs, inaEsc, inaSolar, inaBatt, thrustG);
#endif
  }
#ifndef NO_SD_ROWS
  if (logging && now - lastFlushMs >= FLUSH_PERIOD_MS) {
    lastFlushMs = now;
    logFile.flush();
  }
#endif

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