# Solar–Battery Hybrid Propulsion Testbed

Firmware, run logs and test records for a ground-based testbed that measures how much of a lightweight RC aircraft's propulsion power a solar panel array can supply alongside a battery.

> **Research question:** What share of a lightweight RC aircraft’s motor can a solar array supply when it shares a direct-current (DC) bus with a Li-ion battery?

UTS Mechatronics capstone project, 2026. Nothing here flies: the rig clamps a brushless motor and propeller to a bench, sweeps the throttle, and logs voltage and current on each branch of the power system.

## What was built

A shared DC bus connects three sources and sinks, with one INA260 current/voltage sensor on each branch:

| Branch | Hardware | INA260 address |
|---|---|---|
| ESC (load) | Hobbywing Skywalker 50 A V2 ESC, 2820SL 550 KV motor, APC 9x4.5E propeller | 0x40 |
| Solar | 3 × GoFlex GFS-50 flexible panels (150 W nominal) through a Victron SmartSolar 100/15 MPPT | 0x41 |
| Battery | Dogcom 4S Li-ion pack (4 × INR21700-40T, 59.2 Wh, no BMS) | 0x44 |

A Freenove Control Board V5 (Uno R4 WiFi layout, 5 V logic) plays the transmitter stick, reads the sensors and a load cell (Makerverse 10 kg bar on an HX711), and logs to serial and SD. Wiring, fuse sizes and the bus layout are in [Solar Hybrid Testbed Wiring.md](Solar%20Hybrid%20Testbed%20Wiring.md).

## Test method

Each run holds the ESC at 1000 µs for 3 s, then steps the throttle through 1200, 1300, 1400, 1500, 1600 and 1700 µs for 5 s each (about 33 s). The sweep stops on over-current (12 A), under-voltage (12.0 V), over-thrust or a sensor read failure.

- **Battery-only baseline:** panels covered, five runs (B1–B5).
- **Hybrid:** panels uncovered, five runs (H1–H5) plus two extra runs (H6–H7).
- **Main metric:** solar share of ESC power, solar W ÷ ESC W within the same run.

## Headline results

From the 30 October 2026 reported runs:

| Measure | Result |
|---|---|
| Battery-only ESC power, 1200 to 1700 µs | 3.7 to 54.7 W |
| Whole-run solar share of ESC energy, H1–H5 | 73% to 91%, mean 84.9% |
| Solar share over the 1400 to 1700 µs steps | 72% to 88% |
| Pack energy against battery-only | about 85% lower |
| Peak solar power (1700 µs) | about 52 W against 150 W nominal |

These come from a short ground sweep in sunlight that was not measured. **Irradiance was not recorded**, so run-to-run differences cannot be separated from changing sun. Solar share also drifted from 91% (H2) to 80% (H6–H7) across the session with an unconfirmed cause. The report is the authoritative record; where its figures differ from this table, use the report.

## Repository layout

The folders follow the order the work was done, by git date.

```
Capstone_HES_Project/
├── 1_Prototyping/                 ESC set-up and first HX711 diagnostics (22 Oct)
├── 2_Redevelopment/               Diagnostic builds, logger variants, debugging run logs (22–27 Oct)
├── 3_Pilot_runs/                  Pilot hybrid and battery-only runs, run manifest (29 Oct)
├── 4_Calibration_and_final_runs/  Load cell calibration, final logger, SD card header/footer files (30 Oct)
├── 5_Reported_data/               Per-run data, manifest and analysis workbook behind the report (30 Oct)
├── 3D Prints/                     3D printed parts for the rig
├── Solar Hybrid Testbed Wiring.md Wiring, fuse sizes and bring-up checks
└── platformio.ini                 Build environments for all firmware
```

| Stage | Folder | What it records |
|---|---|---|
| 1 | [1_Prototyping](1_Prototyping) | ESC programming from the keyboard, first look at the HX711 load cell amplifier |
| 2 | [2_Redevelopment](2_Redevelopment) | Isolation of three faults: HX711 clock timing, I2C transfers aborting under interrupt load, and load cell outliers that appeared only when rows were written to the SD card |
| 3 | [3_Pilot_runs](3_Pilot_runs) | Pilot runs that set the 1700 µs throttle cap and the Victron charge settings |
| 4 | [4_Calibration_and_final_runs](4_Calibration_and_final_runs) | Calibration, final logger and the SD card files for the reported runs (header and footer only) |
| 5 | [5_Reported_data](5_Reported_data) | **The data behind the report:** one CSV per run (B1–B5, H1–H5), a manifest and the analysis workbook |

## Finding the data behind the report

Open [5_Reported_data](5_Reported_data). [`manifest.csv`](5_Reported_data/manifest.csv) lists each run with its label, condition, pack and cell voltages, energy totals and checksum. Run numbers repeat across SD card sessions, so cite runs by label (B1, H1 …). The report's analysis workbook is in the same folder.

## Build and upload

Firmware is built with [PlatformIO](https://platformio.org/) in VS Code. Environments are defined in `platformio.ini`.

```
pio run -e <env>                  # build
pio run -e <env> -t upload        # build and upload
pio device monitor -b 115200      # Serial Monitor
```

The Serial Monitor output is the data record for the final runs, so log it to a file. See [4_Calibration_and_final_runs](4_Calibration_and_final_runs) for the exact procedure.

## Things to know before reusing anything

- Run numbers (`RUNnn`) repeat across SD card sessions. Always cite a run with its date or its B/H label.
- The final logger does not write data rows to the SD card, because row writes coincided with load cell glitches. The SD files hold only header and footer lines; the data rows are in [5_Reported_data](5_Reported_data).
- The load cell was calibrated only for the 30 October runs. Earlier thrust values use an estimate or are absent.
- There is no battery management system on the pack. Voltage limits are enforced in firmware and in the ESC settings only.

## Use of generative AI

Claude (Anthropic) was used to write the script and formulas that collate the run logs into the analysis workbook, to help with debugging discussions, and to draft repository documentation. The experiments, measurements, wiring and analysis decisions are the author's own.

## Author

Ash, GitHub [@AshMudaly](https://github.com/AshMudaly). University of Technology Sydney (UTS), Mechatronics.