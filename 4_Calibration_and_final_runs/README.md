# 4. Calibration and final runs (30 October 2026)

Load cell calibration, the final logger firmware and the runs the report is based on. This is stage 4 of 4 and the only stage whose data is reported as results; stages 1 to 3 are development and pilot work.

## Contents

| Item | What it is |
|---|---|
| `calibrate` | Firmware used to calibrate the load cell |
| Final logger | Cleaned `logger` source with the diagnostic code removed. This is the firmware that logged the reported runs |
| Reported runs | SD card CSVs for RUN67 to RUN71 and RUN75 to RUN79 |
| Calibration monitor log | Serial Monitor capture of the calibration session, 30 Oct |

## Run map

Run numbers (`RUNnn`) restart whenever the SD card is cleared, so a number alone is ambiguous. Use the label.

| Label | Run file | Condition | Status |
|---|---|---|---|
| B1 to B5 | RUN67 to RUN71 | Battery only, panels covered | Reported |
| H1 to H5 | RUN75 to RUN79 | Hybrid, panels uncovered | Reported |
| H6, H7 | RUN80, RUN81 | Hybrid, extra runs | Kept outside the analysis workbook |
| none | RUN72 | Hybrid attempt, Victron setpoint 16.60 V below the pack voltage, so solar delivered nothing | Excluded, kept as a record |
| none | RUN73 | Idle check after the setpoint was raised to 16.70 V | Passed, not a sweep |

## Where the data is

The final logger does not write data rows to the SD card. Writing rows coincided with load cell glitches (see stage 2), so each reported `RUNnn.CSV` holds only the header and footer lines. **The data rows are in the Serial Monitor captures**, one capture per run.

Serial rows use the same CSV columns as the SD file:

```
ms, throttle_us, bus_V, current_A, power_W, energy_Wh, net_counts, thrust_g
```

When loading a capture in pandas, keep only the header line and lines that start with a digit. This drops the PlatformIO preamble, which does not start with `#`. Lines before `# arming` are idle readings, not run data.

## Test profile

Every reported run uses the same sweep:

| Phase | Throttle | Duration |
|---|---|---|
| Arm | 1000 µs | 3 s |
| Steps | 1200, 1300, 1400, 1500, 1600, 1700 µs | 5 s each |

The sweep ends early on any of: current above 12 A, bus voltage below 12.0 V, thrust above 3000 g for five consecutive samples, or a sensor read failure. The throttle is capped at 1700 µs because 1800 µs tripped the current limit in the pilot runs.

## Calibration

The load cell was calibrated on 30 October 2026 with antique weights (2, 4 and 8 oz and 1 lb) that were re-weighed on a kitchen scale first (for example, the 4 oz weight reads 122 g).

| Where | `COUNTS_PER_G` |
|---|---|
| Firmware | -215.45 |
| Analysis workbook | 215.45 (sign flipped so thrust reads positive) |

Thrust is calibrated for the reported runs only. Pilot runs from 29 October used an estimate of 200 counts per gram or logged no thrust.

## Running a test

1. In `platformio.ini`, select the logger environment `logger_nosd`.
2. Build and upload, then open the Serial Monitor at 115200 baud and log it to a file.
3. Connect the pack. Confirm the idle bus voltage is in range (the logger refuses to start below 13.6 V or above 16.9 V).
4. Send `t` to tare with nothing touching the load cell. The tare lives in RAM and resets on every boot.
5. Send `g` to start the sweep. Send `x` to stop it.

Press reset between runs. The ESC must be powered after the logger has started so it sees the idle signal first.

## Known issues

- **Load cell glitches.** Three single-sample read errors occurred in the reported runs (RUN56, RUN77, RUN81). Each is a quantised bit error, not noise. Thrust uses the median per step.
- **Sensor timing.** The three INA260 sensors average over about 35 ms and are not sampled together, so step-edge peaks are not reliable. The 12 A stop guards sustained current only.
- **Irradiance was not measured** in any run.
- **Pre-run cell voltages and run times** for H1 to H7 are not yet recorded in the repository.
- **Charge surge.** Every covered-to-uncovered transition pushed 4.7 to 6.5 A into the pack for about a second, above the 4 A Victron setting.

See the report for analysis and for the full list of limitations.