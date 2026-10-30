// calibrate.cpp guided load cell calibration for the thrust rig
// Keep the HX711 RATE switch where the thrust runs will use it (currently H)
#include <Arduino.h>

constexpr uint8_t HX_DATA_PIN = 2;
constexpr uint8_t HX_SCK_PIN = 3;
constexpr uint32_t HX_PULSE_US = 25;  // same clocking as the logger
constexpr uint32_t HX_TIMEOUT_MS = 1000;

constexpr uint16_t SAMPLES = 100;     // readings logged per step
constexpr uint32_t SETTLE_MS = 5000;  // wait after 'n' before logging

constexpr double G_PER_OZ = 28.349523125;

struct Step {
  const char* label;
  double grams;
};

// Replace a grams value with the kitchen-scale reading if that weight was measured
constexpr Step STEPS[] = {
    {"empty plate (tare)", 0},
    {"2 oz", 2 * G_PER_OZ},
    {"4 oz", 4 * G_PER_OZ},
    {"4 oz + 2 oz", 6 * G_PER_OZ},
    {"8 oz", 8 * G_PER_OZ},
    {"8 oz + 2 oz", 10 * G_PER_OZ},
    {"8 oz + 4 oz", 12 * G_PER_OZ},
    {"8 oz + 4 oz + 2 oz", 14 * G_PER_OZ},
    {"1 lb", 16 * G_PER_OZ},
    {"empty plate (zero check)", 0},
};
constexpr size_t N_STEPS = sizeof(STEPS) / sizeof(STEPS[0]);

static int32_t raw[SAMPLES];
static int32_t sorted[SAMPLES];

// Blocks until HX711 has a conversion then clocks out one 24-bit sample
static int32_t readRaw() {
  uint32_t t0 = millis();
  while (digitalRead(HX_DATA_PIN) == HIGH) {
    if (millis() - t0 > HX_TIMEOUT_MS) {
      Serial.println("# ERROR: HX711 not ready, check wiring. Halted.");
      while (true) {
      }
    }
  }
  uint32_t v = 0;
  for (uint8_t i = 0; i < 24; i++) {
    digitalWrite(HX_SCK_PIN, HIGH);
    delayMicroseconds(HX_PULSE_US);
    v = (v << 1) | (uint32_t)digitalRead(HX_DATA_PIN);  // read at end of high phase
    digitalWrite(HX_SCK_PIN, LOW);
    delayMicroseconds(HX_PULSE_US);
  }
  digitalWrite(HX_SCK_PIN, HIGH);  // 25th pulse next conversion is channel A, gain 128
  delayMicroseconds(HX_PULSE_US);
  digitalWrite(HX_SCK_PIN, LOW);
  delayMicroseconds(HX_PULSE_US);
  if (v & 0x800000UL) v |= 0xFF000000UL;  // sign-extend 24 bit
  return (int32_t)v;
}

// Sorts a[] in place.
static double median(int32_t* a, uint16_t n) {
  for (uint16_t i = 1; i < n; i++) {
    int32_t key = a[i];
    int j = (int)i - 1;
    while (j >= 0 && a[j] > key) {
      a[j + 1] = a[j];
      j--;
    }
    a[j + 1] = key;
  }
  if (n % 2) return a[n / 2];
  return ((double)a[n / 2 - 1] + (double)a[n / 2]) / 2.0;
}

static void waitFor(char c) {
  while (Serial.available()) Serial.read();  // drop stray characters
  while (true) {
    if (Serial.available() && Serial.read() == c) return;
  }
}

static void runCalibration() {
  double med[N_STEPS];
  int32_t tare = 0;

  Serial.print("# samples_per_step=");
  Serial.print((int)SAMPLES);
  Serial.print(" settle_ms=");
  Serial.println((long)SETTLE_MS);
  Serial.println("step,label,mass_g,net_counts");

  for (size_t s = 0; s < N_STEPS; s++) {
    Serial.print("# step ");
    Serial.print((int)s);
    Serial.print(": put on the plate: ");
    Serial.print(STEPS[s].label);
    Serial.print(" (");
    Serial.print(STEPS[s].grams, 2);
    Serial.println(" g). Hands off, then send n");
    waitFor('n');
    delay(SETTLE_MS);

    readRaw();  // flush the conversion that was waiting during the settle delay
    readRaw();
    for (uint16_t i = 0; i < SAMPLES; i++) raw[i] = readRaw();

    for (uint16_t i = 0; i < SAMPLES; i++) sorted[i] = raw[i];
    double m = median(sorted, SAMPLES);
    if (s == 0) tare = (int32_t)lround(m);

    for (uint16_t i = 0; i < SAMPLES; i++) {
      Serial.print((int)s);
      Serial.print(',');
      Serial.print(STEPS[s].label);
      Serial.print(',');
      Serial.print(STEPS[s].grams, 2);
      Serial.print(',');
      Serial.println((long)(raw[i] - tare));
    }
    med[s] = m - (double)tare;

    Serial.print("# step ");
    Serial.print((int)s);
    Serial.print(" median=");
    Serial.print(med[s], 1);
    Serial.print(" min=");
    Serial.print((long)(sorted[0] - tare));
    Serial.print(" max=");
    Serial.println((long)(sorted[SAMPLES - 1] - tare));
  }

  // Least-squares fit through zero over the loaded steps
  double smm = 0, smc = 0;
  for (size_t s = 0; s < N_STEPS; s++) {
    if (STEPS[s].grams > 0) {
      smm += STEPS[s].grams * STEPS[s].grams;
      smc += STEPS[s].grams * med[s];
    }
  }
  double slope = smc / smm;

  Serial.println("# fit through zero, loaded steps only");
  Serial.println("# step,label,mass_g,median_net_counts,fit_error_g,fit_error_pct");
  for (size_t s = 0; s < N_STEPS; s++) {
    double err = med[s] / slope - STEPS[s].grams;
    Serial.print("# ");
    Serial.print((int)s);
    Serial.print(',');
    Serial.print(STEPS[s].label);
    Serial.print(',');
    Serial.print(STEPS[s].grams, 2);
    Serial.print(',');
    Serial.print(med[s], 1);
    Serial.print(',');
    Serial.print(err, 2);
    Serial.print(',');
    if (STEPS[s].grams > 0) {
      Serial.println(100.0 * err / STEPS[s].grams, 2);
    } else {
      Serial.println("NA");
    }
  }
  Serial.print("# COUNTS_PER_G = ");
  Serial.println(slope, 2);
}

void setup() {
  Serial.begin(115200);
  uint32_t t0 = millis();
  while (!Serial && millis() - t0 < 3000) {
  }
  pinMode(HX_SCK_PIN, OUTPUT);
  digitalWrite(HX_SCK_PIN, LOW);
  pinMode(HX_DATA_PIN, INPUT);
  Serial.println("# build=calibrate");
}

void loop() {
  Serial.println("# send s to start the calibration");
  waitFor('s');
  runCalibration();
  Serial.println("# done. send s to run again");
}