# Motor Test Logger

This Arduino program runs a timed ESC throttle test and measures motor power,
solar and battery readings, and thrust. It reports data over Serial and creates
a numbered CSV file on the SD card.

## Before running

Check these settings near the top of `main.cpp` and update them for your board,
wiring, battery, sensors, and test:

- **Pins:** `ESC_PIN`, `SD_CS_PIN`, `HX_DT_PIN`, and `HX_SCK_PIN`.
- **INA260 wiring and addresses:** `INA_SDA_PIN`, `INA_SCL_PIN`, and the three
  `INA_*_ADDR` values. The defaults are motor `0x40`, solar `0x41`, and battery
  `0x44`; set the sensor address jumpers to match.
- **Battery and limits:** `CELLS`, the cell-voltage thresholds, `MAX_CURRENT_A`,
  `MAX_IDLE_CURRENT_A`, and `MAX_THRUST_G`. These checks are safety limits;
  verify them against the actual hardware and test plan.
- **Test profile:** `STEPS_US` sets the throttle levels, `HOLD_MS` sets the
  duration of each level, and `ARM_MS` sets the ESC arming time. Keep
  `THROTTLE_OFF_US` appropriate for the ESC.
- **Thrust calibration:** `COUNTS_PER_G` must match the load cell and HX711.
  Send `t` to tare, then place a known mass on the cell and send `c500` (replace
  `500` with its mass in grams). The returned value is only used for that
  session; copy it into `COUNTS_PER_G` in the source to keep it for future
  power-ups.

Use the correct board support and provide the custom `pwm.h` dependency when
building. Check the wiring and sensor addresses before powering the motor.
Secure the test setup and keep clear of the propeller.

## Run it

1. Connect the sensors, SD card, and ESC; power the system and open Serial
   Monitor at **115200 baud**.
2. The logger initializes the sensors, creates the next available `RUNnn.CSV`,
   and tares the load cell at startup.
3. Optionally send `t` to tare again, or `c<grams>` to calibrate thrust.
4. Send `g` to start. The logger checks the INA260 readings, bus voltage, and
   motor-off current before arming.
5. When the run finishes or stops, read the stop reason and energy totals from
   Serial. Send `x` to stop a run manually.

Commands to start, tare, and calibrate are accepted only while idle. A run
holds the ESC at the off throttle for `ARM_MS`, then applies each value in
`STEPS_US` for `HOLD_MS`.

## What the code does

- **Configuration and state:** pin assignments, battery and safety limits,
  throttle profile, calibration, and run state.
- **HX711 load cell:** reads counts, tares the sensor, and converts counts to
  thrust using `COUNTS_PER_G`.
- **INA260 sensors:** reads voltage and signed current for the motor, solar,
  and battery sensors using software I2C on the configured SDA/SCL pins.
- **ESC and run control:** arms the ESC, advances throttle steps, handles Serial
  commands, integrates energy, and stops on completion or a safety limit.
- **Logging:** prints a CSV header and measurement rows to Serial, and writes
  the header and run comments to the SD file.

The measurement columns are time, throttle, motor bus voltage/current/power and
energy, load-cell counts and thrust, solar current/power/energy, and battery
voltage/current/power/energy. Positive solar and battery current means
supplying the bus; negative battery current means charging.

**Important:** in this source version, measurement rows go to Serial only. The
SD file contains the header and run comments, not the sample rows. Save the
Serial capture as the data record for a run.

The logger stops for excessive motor current, low bus voltage, excessive
calculated thrust, INA260 read failure, operator stop, or completion of all
throttle steps. The start-voltage and motor-off-current checks happen before
arming.
