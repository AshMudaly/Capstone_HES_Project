# Solar Hybrid Testbed: Wiring and Bring-up

Wiring, configuration and bring-up procedure for the ground testbed described in the [main README](README.md). The testbed runs a brushless motor from a 4S Li-ion pack, a solar array through an MPPT charge controller, or both, on one shared DC bus, and logs voltage and current on every branch.

> **Warning.** The pack has no battery management system (BMS). Nothing on the bus protects individual cells from overcharge, over-discharge or imbalance. Voltage limits are enforced only by the logger firmware, the ESC cutoff and the MPPT setpoint. Treat the charge settings below as a record of what this rig used, not as a recommended charging configuration.

## Contents

1. [System overview](#1-system-overview)
2. [Components](#2-components)
3. [Power wiring](#3-power-wiring)
4. [Logic wiring](#4-logic-wiring)
5. [ESC calibration and settings](#5-esc-calibration-and-settings)
6. [Solar branch and MPPT configuration](#6-solar-branch-and-mppt-configuration)
7. [Load cell mounting and calibration](#7-load-cell-mounting-and-calibration)
8. [Logger firmware and data format](#8-logger-firmware-and-data-format)
9. [Bring-up procedure](#9-bring-up-procedure)
10. [Test sweep](#10-test-sweep)
11. [Typical values](#11-typical-values)
12. [Common wiring faults](#12-common-wiring-faults)

## 1. System overview

Three branches meet at a shared bus, so the battery, the solar array and the ESC are joined at one node (Kirchhoff's current law). One INA260 current and voltage sensor sits on each branch.

```
 Solar string ─► MPPT ─► 25 A fuse ─► INA260 #2 (0x41) ─┐
                                                        │
 Pack ─► 15 A fuse ─► INA260 #3 (0x44), in series ──────┼─── Bus + ───► INA260 #1 (0x40) ─► ESC ─► motor
                                                        │
 Pack − ────────────── MPPT BAT − ──────────────────────┴─── Bus − ───────────────────────► ESC −
```

The bus is two 30 A terminal blocks, one for + and one for −. Each block is a single node, linked across the ways in use. Way 1 takes the pack, way 2 the ESC branch and way 3 the solar branch.

The build has two stages:

| Stage | Branches fitted | Purpose |
|---|---|---|
| Phase 1 | ESC branch and pack | Battery-only baseline |
| Phase 2 | Solar branch added | Hybrid runs |

## 2. Components

| Part | Detail |
|---|---|
| Controller | Freenove Control Board V5 (Uno R4 WiFi layout, 5 V logic), powered from laptop USB only |
| Pack | Dogcom 4S Li-ion, 4 × INR21700-40T, 4000 mAh, 59.2 Wh, no BMS. 16.8 V full, 12.0 V stop |
| ESC | Hobbywing Skywalker 50 A V2 (label V2-UBEC, 3–6S, 5 V at 6 A) |
| Motor and prop | Hobbywing Skywalker 2820SL 550 KV, APC 9×4.5E |
| Solar array | 3 × GoFlex GFS-50 flexible panels (50 W each) in series |
| MPPT | Victron SmartSolar 100/15 with Bluetooth |
| Sensors | 3 × Adafruit INA260 (15 A, 36 V, 2 mΩ shunt) at 0x40, 0x41 and 0x44 |
| Load cell | Makerverse 10 kg bar with an Adafruit HX711 amplifier |
| Storage | SD card module (FAT32) on the SPI pins |
| Protection | 15 A blade fuse in the pack lead, 25 A blade fuse in the MPPT battery lead |
| Bus capacitor | 1000 µF 63 V across the ESC input |
| Precharge lead | 10 Ω 5 W resistor in the + leg only, removable |

Cable is 25 A tinned auto cable for the high-current paths and flexible fine-strand copper on the solar branch.

## 3. Power wiring

### 3.1 Phase 1 build order

1. Check both terminal blocks with a multimeter and link the ways in use, so each block is one node. Barrier strips often do not join the screws along a row.
2. Fused pack lead: pack − to Bus − way 1, and system XT60 + through the 15 A fuse holder to Bus + way 1. Fit the fuse last.
3. ESC branch: Bus + way 2 to INA260 #1 VIN+, then VIN− to the ESC red lead. Bus − way 2 to the ESC black lead.
4. Fit the 1000 µF capacitor across the ESC input, stripe leg to the black side, within about 5 cm of the ESC.
5. Join the three ESC phase wires to the motor in any order. Leave the propeller off.
6. Wire the ESC signal lead to the Arduino ([section 4](#4-logic-wiring)). Leave Bus + way 3 and Bus − way 3 empty until Phase 2.

### 3.2 Connection table

| # | From | To | Note |
|---|---|---|---|
| 1 | Pack + (male XT60) | System XT60 + (rig side, female) | Use the precharge lead on every connect |
| 2 | System XT60 + | Fuse holder 1 in (15 A) | 25 A cable, kept short |
| 3 | Fuse holder 1 out | Bus + way 1 | 25 A cable. INA260 #3 is in series in this lead |
| 4 | System XT60 − | Bus − way 1 | 25 A cable |
| 5 | Bus + way 2 | INA260 #1 VIN+ | Current enters VIN+ |
| 6 | INA260 #1 VIN− | ESC red | VIN− is the ESC side |
| 7 | Bus − way 2 | ESC black | 25 A cable |
| 8 | Capacitor + | ESC red node | Within 5 cm of the ESC |
| 9 | Capacitor − (stripe) | ESC black node | Check polarity twice |
| 10 | ESC phase wires (3) | Motor wires (3) | Any order. Swap two to reverse the spin |
| 11 | ESC signal and ground | Arduino D9 and GND | ESC red BEC wire removed |

ESC black goes straight to the − block and never passes through a sensor. Two wires must never share one INA260 terminal, or current bypasses the shunt.

### 3.3 Precharge

Before every pack connection, touch the pack to the system input through the precharge lead for 2 to 3 s, then connect the XT60 directly. This limits inrush into the bus capacitor.

### 3.4 Single ground path

Logic ground and power negative must meet in one place only: the ESC signal ground. The Arduino is powered from USB only, and nothing from the bus connects to it.

### 3.5 Phase 2 solar branch

1. Join the panels in series: Panel 1 + to Panel 2 −, and Panel 2 + to Panel 3 −. Keep them covered and leave the two end leads free.
2. Check the string in sun with nothing else connected: Voc about 29–32 V, Isc about 5–6 A.
3. MPPT BAT+ to the 25 A fuse, to INA260 #2 VIN+, then VIN− to Bus + way 3. MPPT BAT− to Bus − way 3.
4. Join the branch to way 3 on both blocks with the motor idle. The MPPT powers up from the pack, so this comes before the panels.
5. Connect the string to PV − and PV + last, with the panels covered.

MPPT terminal strip, left to right:

| BATT + | BATT − | PV − | PV + | LOAD − | LOAD + |
|---|---|---|---|---|---|
| 25 A fuse, then INA260 #2 VIN+ | Bus − way 3 | String − | String + | Empty | Empty |

The VE.Direct port is unused. Mount the MPPT upright with terminals down, 10 cm clear above and below, within 3 m of the pack. Torque the BATT, PV and LOAD screws to 0.75 N·m.

Rules for the solar side:

- Cover the panels before mating or unplugging any MC4 connector, and never unplug under load.
- PV − goes to PV − only. Do not link it to BAT − or to ground.
- INA260 #2 VIN+ faces the MPPT, so solar current reads positive.
- Reverse battery polarity is not protected on the MPPT and is not covered by warranty. Check BATT + and − twice.

## 4. Logic wiring

### 4.1 Pin map

| Arduino pin | Connects to |
|---|---|
| 5V | INA260 VCC (all), HX711 VIN, SD VCC |
| GND | INA260 GND (all), HX711 GND, SD GND, ESC signal ground |
| A4 (SDA) | INA260 SDA, chained to all sensors |
| A5 (SCL) | INA260 SCL, chained to all sensors |
| D2 | HX711 DATA |
| D3 | HX711 SCK |
| D9 | ESC signal (hardware PWM) |
| D10, D11, D12, D13 | SD CS, MOSI, MISO, SCK |
| USB | Laptop: power, Serial Monitor at 115200 baud, commands and live rows |

Not used: barrel jack, VIN, the ICSP header, the white 4-pin connector, and the SDA/SCL pins beside AREF. The white connector is the second I2C bus (`Wire1`) on a genuine Uno R4 WiFi.

### 4.2 Rails

The board has one 5V pin and three GND pins, so one 5 V rail and one GND rail are made on a small breadboard, each fed from a single board pin. All three INA260s, the HX711 and the SD module share these rails.

### 4.3 INA260 notes

- Addresses: #1 stays at 0x40 (ESC), #2 has the A0 solder bridge for 0x41 (solar), #3 has the A1 bridge for 0x44 (battery). Chain SDA, SCL, 5 V and GND from #1.
- Only VCC, GND, SDA and SCL get header leads. The VB and Vin+ header pins stay unconnected.
- Current enters VIN+ and leaves VIN−, so swapped leads read negative.
- The sensors are read over bit-banged (software) I2C on A4 and A5, not the hardware `Wire` driver. The hardware driver aborted transfers under interrupt load (see [2_Redevelopment](2_Redevelopment)).
- Each sensor averages 16 samples (about 35 ms windows) and the three are not sampled together.

### 4.4 HX711 notes

- VIN to the 5 V rail, GND to the GND rail, DATA to D2, SCK to D3. VIO stays unconnected.
- Load cell: E+ red, E− black, A− white, A+ green on typical cells (check the label). B+ and B− are unused. If thrust reads negative, swap A+ and A−.
- The final firmware clocks the HX711 directly (25 µs high, 25 µs low, 25 pulses per read, channel A, gain 128) rather than using a library. The RATE switch was moved to H (80 samples per second) on 30 October 2026, and the load cell was plugged into a different ground at about the same time.

### 4.5 SD module

Six wires: 5 V, GND and D10 to D13. A bare module needs an on-board regulator and level shifter. A stackable shield may use a different chip-select pin.

## 5. ESC calibration and settings

Do these once, with the propeller off.

The Arduino plays the transmitter stick using the `esc_setup` environment. In the Serial Monitor at 115200 baud, `u` sends 2000 µs (stick top) and `d` sends 1000 µs (stick bottom). Keep the motor connected, because the motor makes the beeps. The ESC reads the stick position at the moment the pack connects, so send the letter before connecting the pack, through the precharge lead as usual.

### 5.1 Calibrate the throttle

The default throttle range is 1100–1940 µs. Without calibration, 1900 µs and 2000 µs would be almost the same throttle.

1. Send `u`, then connect the pack.
2. Wait for the start-up tone, then two short beeps.
3. Within 5 s of those two beeps, send `d`.
4. The ESC beeps the cell count (four for 4S), then one long beep. The ESC is now calibrated.

### 5.2 Program the settings

1. With the pack disconnected, send `u`, then connect the pack. After the two short beeps send nothing. About 5 s later the programming tone plays.
2. The item beeps repeat in a loop. When the wanted item sounds, send `d` within 3 s.
3. The value beeps loop. When the wanted value sounds, send `u`. A tone confirms it and the ESC returns to the item list.
4. To finish, select the Exit item (two long beeps and one short) with `d`. Settings apply after the pack is disconnected and reconnected.

| Item | Item beeps | Set to | Value beeps | Reason |
|---|---|---|---|---|
| Voltage cutoff type | 3 short | Hard | 2 short | No BMS. Soft only drops to 60% power and keeps running |
| LiPo cells | 4 short | 4S | 3 short | Auto assumes 3.7 V per cell, so a nearly flat 4S pack could read as 3S and set the cutoff far too low |
| Cutoff voltage | 1 long | Low | 2 short | 2.8 V per cell is 11.2 V, below the firmware's 12.0 V stop. The default Medium (3.0 V per cell, 12.0 V) would clash with it |
| All other items | n/a | Default | n/a | Brake off, start-up normal, timing medium, active freewheeling on, search off |

These beep tables follow the Skywalker V2 manual for the 50 A model (3–4S, 5 V at 5 A). The label on the unit used here (V2-UBEC, 3–6S, 5 V at 6 A) differs, so it may be an older revision. If the beeps do not match, stop and check before changing anything.

### 5.3 ESC signal lead

Pull the red (BEC) wire out of the signal plug and insulate it. If ESC 5 V reached the Arduino rail while USB was connected, two supplies would conflict and could back-feed the laptop port.

## 6. Solar branch and MPPT configuration

### 6.1 System voltage

The SmartSolar selects 12 V or 24 V once, on its first power-up (10 s wait, then an averaged reading), and the manual does not give the switching voltage. A full 4S pack sits at 16.8 V. Confirm in VictronConnect that the system voltage reads 12 V, and set it by hand if it does not. The Windows version of VictronConnect has no Bluetooth.

### 6.2 Charge settings used

The charge setpoint is the battery-side regulation target. 24 V defaults are twice the 12 V defaults, so the 24 V defaults would give 28.8 V absorption, which is unsafe for a 4S Li-ion pack.

| Setting | Value | Note |
|---|---|---|
| Battery voltage | 12 V | 4S pack, 12.0–16.8 V |
| Battery preset | User defined | The lithium preset is a LiFePO4 profile (14.2 V absorption) |
| Absorption | 16.70 V | 4.175 V per cell |
| Float | 16.70 V | Set equal to absorption |
| Equalisation | Off | Lithium |
| Temperature compensation | 0 mV/°C* | The default (−16 mV/°C) is a lead-acid coefficient |
| Max charge current | 4 A | Hybrid runs |

\* Planned value, not re-verified against the unit.

Findings from commissioning:

- A setpoint below the pack voltage makes the controller deliver nothing. A 16.60 V setpoint did this in RUN72, which was excluded from the results. Raising the setpoint to 16.70 V fixed it, confirmed by an idle test (RUN73).
- The controller regulated the bus at 16.67 V for the 16.70 V setpoint (an offset of about 25 mV).
- Uncovering the panels pushed 4.7 to 6.5 A into the pack for about a second, above the 4 A setting. The cell datasheet gives 2 A as the standard charge current.

### 6.3 Fuses

| Fuse | Location | Note |
|---|---|---|
| 15 A | Pack + lead, holder 1 | Phase 1 |
| 25 A | MPPT battery lead, holder 2 | Manual: 20 A minimum, 25 A maximum for the 100/15. The unit also has its own internal 20 A blade fuse, which blows on reverse polarity and is not protection to rely on |

A 30 A fuse is not installed because it is above the 25 A cable rating.

## 7. Load cell mounting and calibration

### 7.1 Mounting

- Bolt the fixed end of the bar to a rigid base on spacers. The motor plate goes on the free end and nothing else touches it.
- Point the sensing arrow along the thrust direction.
- Keep the HX711 next to the cell. Twist the four cell wires and route them at least 10 cm from the motor and ESC wires.
- Keep the propeller at least 25 cm from walls and the table, with the airflow directed away from the cell and its wiring.

### 7.2 Calibration

1. With the motor mounted and no thrust, send `t` to tare. The tare lives in RAM and resets on every boot.
2. Apply a known mass in the thrust direction and send `c<grams>` (for example `c500`) promptly. The sketch waits only 0.2 s for the number after the `c`.
3. Copy the printed `COUNTS_PER_G` into the logger source and rebuild. A reset wipes the RAM value.
4. Apply a different known mass and check that `thrust_g` reads within a few percent, and returns to about 0 when removed.

The reported runs use 215.45 counts per gram, stored as −215.45 in the firmware so that propeller thrust reads positive. Calibration masses were weighed on a kitchen scale first.

## 8. Logger firmware and data format

Build with PlatformIO in VS Code. The environment name in the bottom bar selects the firmware (`logger`, `logger_nosd` or `esc_setup`). Close the Serial Monitor before uploading, because it holds the USB port.

Serial commands:

| Command | Action |
|---|---|
| `t` | Tare the load cell (send before every run, with nothing touching the cell) |
| `c<grams>` | Calibrate against a known mass |
| `g` | Start the sweep |
| `x` | Stop |

Each reset opens a new `RUNnn.CSV` (RUN00 to RUN99), so run numbers repeat across SD card sessions. Always cite a run with its date or label.

### 8.1 CSV columns

Lines starting with `#` are comments.

| Column | Meaning |
|---|---|
| `ms` | Milliseconds since power-up. Use this as the time axis, not an assumed 100 ms step |
| `throttle_us` | Throttle pulse in force when the row was sampled |
| `bus_V`, `current_A`, `power_W` | INA260 #1, ESC branch |
| `energy_Wh` | Energy delivered to the ESC since the sweep started |
| `net_counts`, `thrust_g` | HX711 counts after tare, and calibrated thrust (`NA` before calibration) |
| `solar_A`, `solar_W`, `solar_Wh` | INA260 #2, solar branch. Positive when the solar branch supplies the bus |
| `batt_V`, `batt_A`, `batt_W`, `batt_Wh` | INA260 #3, pack. Negative current means the pack is charging |

### 8.2 Where the data is

The `logger_nosd` environment does not write data rows to the SD card, because row writes coincided with load cell glitches. The SD file holds only the header and footer lines, and the data rows are in the Serial Monitor output. Log the monitor to a file:

```
pio device monitor --environment logger_nosd --port COM3 -f log2file
```

When loading a capture in pandas, keep the header line and lines that start with a digit, which drops the PlatformIO preamble. Only rows after `# arming` are run data.

## 9. Bring-up procedure

Complete each stage before starting the next. These checks do not certify the wiring as safe.

| Stage | Firmware | USB | Pack | Motor | Prop |
|---|---|---|---|---|---|
| 1A Software check | `logger` | Yes | No | No | No |
| 1B Smoke test | `logger` | Yes | Yes, through a 12 V car bulb | No | No |
| 2A ESC setup | `esc_setup` | Yes | Yes, precharge then direct | Yes | No |
| 2B First spin | `logger` | Yes | Yes, precharge then direct | Yes | No |
| 2C Load cell | `logger` | Yes | No | Mounted | No |
| 2D Propeller sweep | `logger` | Yes | Yes, precharge then direct | Yes | Yes |

### 9.1 Before first power

- Buzz out the bus: a steady beep between + and − is a fault. A reading that climbs and settles at OL is the capacitor charging.
- Check for loose strands, bare solder blobs and stray wire across the + strip, and tug-test every screw and splice.
- Confirm capacitor and XT60 polarity. Reversed polarity can destroy the ESC.
- With USB only, the 5 V and GND rails must not beep to each other, and each module's power pins should read about 5 V.

### 9.2 Stage 1A: USB only

With the pack and motor disconnected, upload the logger, open the monitor and reset. Expected boot text:

```
# logging to RUN00.CSV
ms,throttle_us,bus_V,current_A,power_W,energy_Wh,net_counts,thrust_g
# ready. connect the pack through the precharge lead, then send g
# idle V=0.00 I=0.000 thrust_g=NA counts=3
```

Press the load cell and check that `counts=` moves and settles. Send `t` to zero it. Send `g` with no pack and confirm it refuses to start. Send `x` to stop.

Failure messages: `INA260 not found` points to SDA, SCL or sensor power. `SD init failed` points to the card format or the SPI wires. `HX711 not responding` points to DATA, SCK or its power.

### 9.3 Stage 1B: smoke test

With the motor disconnected, fit a 12 V car bulb in series in the pack + lead in place of the precharge lead, keeping the 15 A fuse in circuit. A mains bulb stays dark at 16.8 V even on a short and proves nothing. Connect the pack: the bulb should flash briefly while the capacitors charge, then go dark. The idle line should show the pack voltage and a current near 0.05 A. Check the bus voltage against a multimeter (within about 0.05 V).

Disconnect the pack at once if the bulb stays lit, the idle voltage reads about 0 with the pack connected, anything smells, or anything warms.

### 9.4 Stage 2A to 2D

- **2A:** run the ESC calibration and settings in [section 5](#5-esc-calibration-and-settings), then switch back to the logger.
- **2B:** with the motor clamped and the propeller off, start a sweep. Check the spin direction, smooth running, and that current rises with each step. If the direction is wrong, disconnect the pack and swap any two motor wires. Press reset before the next run.
- **2C:** calibrate the load cell ([section 7.2](#72-calibration)) with USB only.
- **2D:** do not run the propeller sweep until the prop-off test confirms INA260 #1 reads positive current under load. Fit the propeller with the rig clamped and the guard in place. After the run, disconnect the pack and check the ESC, motor and pack for heat.

For each run, record the log file, the date, the firmware environment, the idle voltage before the run, the ESC settings, whether it was battery-only or hybrid, and any events.

### 9.5 Hybrid runs

Before connecting the PV string, confirm the MPPT system voltage ([section 6.1](#61-system-voltage)) and the charge settings ([section 6.2](#62-charge-settings-used)). Battery-only runs are made with the panels covered. For hybrid runs the panels are uncovered. Irradiance was not measured in any run.

## 10. Test sweep

| Phase | Throttle | Duration |
|---|---|---|
| Arm | 1000 µs | 3 s |
| Steps | 1200, 1300, 1400, 1500, 1600, 1700 µs | 5 s each |

One CSV row is logged about every 100 ms, so a full sweep takes about 33 s. The logger refuses to start below 13.6 V or above 16.9 V.

The sweep ends on whichever comes first:

- sequence complete
- current above 12 A (ESC sensor)
- bus voltage below 12.0 V
- thrust above 3000 g for five consecutive samples
- a sensor read failure
- `x` from the operator

The throttle is capped at 1700 µs because 1800 µs produced a current surge in the pilot runs. The 12 A stop guards sustained ESC current only. Brief step-edge peaks are not reliable, because the sensors are not sampled together.

## 11. Typical values

| Quantity | Value |
|---|---|
| Pack voltage | 16.8 V full, 13.6 V minimum to start a sweep, 12.0 V stop |
| Idle ESC current (motor connected, throttle at 1000 µs) | about 0.05 A at about 16.6 V |
| Battery-only ESC power, 1200 to 1700 µs | 3.7, 8.0, 14.1, 23.3, 37.7 and 54.7 W |
| Peak solar power at 1700 µs | about 52 W (3.1 A), against 150 W nominal |
| PV string at STC | Voc 32.1 V, Vmp 27 V, Imp 5.7 A, Isc 6.1 A |
| MPPT start condition | PV above pack + 5 V (up to 21.8 V), then pack + 1 V |
| Current limits | INA260 15 A, pack fuse 15 A, 25 A cable |

## 12. Common wiring faults

- **INA260 direction.** Current enters VIN+ and leaves VIN−, so swapped leads read negative.
- **Reversed capacitor or XT60 polarity** can destroy the ESC.
- **Barrier strips** often do not join the screws along a row. Confirm each node with a multimeter.
- **Loose screws and stray strands.** Tug-test every wire and re-check after the first run.
- **More than one ground path.** Logic ground meets power negative through the ESC signal ground and nowhere else.
- **ESC BEC wire left in the signal plug**, which puts a second 5 V supply on the logic rail.
- **Two wires on one INA260 terminal.** Current then bypasses the shunt and the sensor under-reads.