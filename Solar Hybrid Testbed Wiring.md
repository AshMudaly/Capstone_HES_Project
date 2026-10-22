# Solar Hybrid Testbed Wiring

4S Li-ion pack on a shared bus, built in three views. Figures marked *est.* are my scaling estimates and should be checked against your first readings.

## Build order

1. Buzz out both 30 A terminal blocks with the multimeter and link the ways you use, so each block is one node: Bus + and Bus −.
2. Fused lead (built): pack − to Bus − way 1, and system XT60 + through the 15 A fuse holder to Bus + way 1. Fit the fuse last.
3. ESC branch: Bus + way 2 to INA260 #1 VIN+, then VIN− to the ESC red lead. Bus − way 2 to the ESC black lead.
4. Fit one 1000 µF 63 V capacitor across the ESC input, stripe leg to the black side, within about 5 cm of the ESC.
5. Join the three ESC phase wires to the motor in any order. Prop off.
6. Wire the signal lead to the Arduino (Logger tab). Leave Bus + way 3 and Bus − way 3 empty for Phase 2.

Pack current leaves +, passes the 15 A fuse and the ESC-branch sensor, and returns on −. Phase 2 plugs into the reserved way 3 on each block.

## Connections

| # | From | To | Cable and note |
| --- | --- | --- | --- |
| 1 | Pack + (male XT60) | System XT60 + (rig side, female) | Use the precharge lead on every connect |
| 2 | System XT60 + | Fuse holder 1 in (15 A) | 25 A cable, kept short |
| 3 | Fuse holder 1 out | Bus + way 1 | 25 A cable |
| 4 | System XT60 − | Bus − way 1 | 25 A cable |
| 5 | Bus + way 2 | INA260 #1 VIN+ | Current enters VIN+ |
| 6 | INA260 #1 VIN− | ESC red | VIN− is the ESC side |
| 7 | Bus − way 2 | ESC black | 25 A cable |
| 8 | Capacitor + | ESC red node | 1000 µF 63 V, within 5 cm of the ESC |
| 9 | Capacitor − (stripe) | ESC black node | Check the polarity twice |
| 10 | ESC phase wires (3) | Motor wires (3) | Any order. Swap two to reverse the spin |
| 11 | ESC signal and ground | Arduino D9 and GND | Red BEC wire removed (see Logger tab) |
| 12 | Bus + way 3, Bus − way 3 | Empty | Reserved for the Phase 2 solar branch |

## Ballpark numbers

Pack voltage

16.8 V full, 13.6 V minimum to start a sweep, 12.0 V stop.

ESC branch

About 5–8 A at full throttle (est., ±40%). The sketch stops at 12 A.

Limits

INA260 15 A, pack fuse 15 A, 25 A cable.

Idle

About 0.0x A with the motor off.

## Where it goes wrong

- INA260 direction. Current enters VIN+ and leaves VIN−, so swapped leads read negative.
- Reversed capacitor or XT60 polarity can destroy the ESC.
- Barrier strips often do not join the screws along a row. Confirm each node with the multimeter.
- Loose screws and stray strands. Tug-test every wire and re-check after the first run.
- One ground path only. Logic ground meets the power negative through the ESC signal ground and nowhere else.

## ESC setup, once prop off

**Calibrate first.** The Skywalker V2's default throttle range is 1100–1940 µs and the manual says to recalibrate for any new transmitter. Without it, the 1900 µs and 2000 µs sweep steps would be almost the same throttle.

The Arduino plays the transmitter stick using `esc_setup.ino`. In the Serial Monitor at 115200, `u` sends 2000 µs (stick top) and `d` sends 1000 µs (stick bottom). Keep the motor connected, because the motor makes the beeps, and keep the prop off. The ESC reads the stick position at the moment the pack connects, so send the letter before connecting the pack, through the precharge lead as usual.

## 1. Calibrate the throttle

1. Send `u`, then connect the pack.
2. Wait for the start-up tone, then two short beeps.
3. Within 5 s of those two beeps, send `d`.
4. The ESC beeps the cell count (four for 4S), then one long beep. That is calibrated.

## 2. Program the settings

1. With the pack disconnected, send `u`, then connect the pack. After the two short beeps send nothing, and about 5 s later the programming tone plays.
2. The item beeps repeat in a loop. When you hear the item you want, send `d` within 3 s.
3. The value beeps loop. When you hear the value you want, send `u`. A tone confirms it and you return to the items.
4. To finish, pick the Exit item (two long beeps and one short) with `d`. Settings only apply after you disconnect the pack and reconnect it.

| Item | Item beeps | Set to | Value beeps | Reason |
| --- | --- | --- | --- | --- |
| Voltage cutoff type | 3 short | Hard | 2 short | The pack has no BMS. The ESC is a backstop, and Soft only drops to 60% power and keeps running |
| LiPo cells | 4 short | 4S | 3 short | Auto uses 3.7 V per cell, so a nearly flat 4S pack could read as 3S and set the cutoff far too low |
| Cutoff voltage | 1 long | Low | 2 short | 2.8 V per cell is 11.2 V, below the sketch's 12.0 V stop. The default Medium is 3.0 V per cell, which is 12.0 V and would clash with the sketch |
| All other items | — | Default | — | Brake off, start-up normal, timing medium, active freewheeling on, search off. Record them in your methods |

This follows the Skywalker V2 manual you sent. Your ESC label (V2-UBEC, 3–6S, 5 V at 6 A) differs from that manual's 50 A model (3–4S, 5 V at 5 A), so yours may be an older revision. If the beeps do not match the tables above, stop and check before changing anything.

## Power-up order

1. Upload the sketch, open the Serial Monitor at 115200 and wait for `# ready`. The ESC now sees 1000 µs.
2. Touch the pack to the system input through the precharge lead for 2–3 s, then connect the XT60 directly. The ESC plays its start-up tone, then four beeps (4S), then one long beep.
3. Read the idle line. Bus voltage should match the pack (13.6–16.8 V) and current should be about 0.0x A.
4. Prop off: send `g`, check the spin direction and compare the INA260 readings with the multimeter at a low step.
5. Fit the prop and run the sweep. `x` stops it at any time.

**Check before the panels go live.** The SmartSolar picks 12 V or 24 V once, on its first power-up (10 s wait, then an averaged reading), and the manual does not give the switching voltage. A full 4S pack sits at 16.8 V, so confirm in VictronConnect that the system voltage reads 12 V, and change it there if it does not. Reverse battery polarity is not protected and is not covered by warranty, so check BATT + and − twice.

## Build order

1. Join the panels in series: mate Panel 1 + to Panel 2 −, and Panel 2 + to Panel 3 −. Keep them covered and leave the two end leads free.
2. Check the string in sun with nothing else connected: Voc about 29–32 V, Isc about 5–6 A.
3. Build the solar branch off the bus: MPPT BAT+ to the 25 A fuse, to INA260 #2 VIN+, then VIN− to Bus + way 3. MPPT BAT− to Bus − way 3. Use flexible fine-strand copper. Leave the PV terminals empty.
4. Bus join: connect the branch to way 3 on both blocks with the motor idle. The MPPT only powers up from the pack, so this comes before the panels.
5. Wait 10 seconds, then open VictronConnect on your phone (Bluetooth is built in; the default PIN is 000000, and the PUK on the unit's sticker resets it). Confirm the battery voltage reads 12 V and apply the settings below. The Windows version of the app has no Bluetooth.
6. Last link: with the panels covered, connect the string to PV+ and PV−, then uncover them. Watch the app and INA260 #2.
7. Hybrid runs: pick throttle steps where charge into the pack stays under 2 A, and never above 4 A.

The complete shared-bus schematic. Amber parts are new in Phase 2, grey parts were built in Phase 1, and the dashed amber box is an optional third sensor.

## MPPT settings VictronConnect

| Setting | Value | Reason |
| --- | --- | --- |
| Battery voltage | 12 V | 4S pack, 12–16.8 V. Set by hand if it detected 24 V |
| Expert mode | On | Needed to edit absorption and float |
| Battery preset | User defined | The lithium preset is 14.2 V absorption (LiFePO4) |
| Absorption | 16.6 V | 4.15 V per cell, with no balancing |
| Float | 16.0 V | Below absorption |
| Equalisation | Off | Lithium |
| Temp compensation | 0 mV/°C | The default −16 mV/°C is a lead-acid coefficient |
| Absorption limit | 1 h, tail 0.4 A | My suggestion (about C/10). The manual default is 6 h |
| Max charge current | 12 A | Keeps INA260 #2 under 15 A |

The manual does not list the settable voltage ranges. If the app rejects 16.6 V or 16.0 V, note the limit it shows and send it to me.

## Fuses

| Fuse | Where | Note |
| --- | --- | --- |
| 15 A | Pack + lead, holder 1 | Built in Phase 1 |
| 25 A | MPPT battery lead, holder 2 | Manual: 20 A min, 25 A max for the 100/15, even though the unit has its own 20 A blade fuse (the one in your photo). Replaces the earlier 15 A plan |
| 30 A | Not installed | Above the 25 A cable rating |

The unit's internal fuse blows on reverse battery polarity, and that is a fault, not protection you can rely on.

## Terminal strip, left to right

| BATT + | BATT − | PV − | PV + | LOAD − | LOAD + |
| --- | --- | --- | --- | --- | --- |
| 25 A fuse, then INA260 #2 VIN+ | Bus − way 3 | String − | String + | Empty | Empty |

This matches the labels on your unit. The VE.Direct port stays unused.

## Ballpark numbers

PV string

Voc 32.1 V, Vmp 27 V, Imp 5.7 A, Isc 6.1 A at STC.

Start and run

PV must exceed pack + 5 V to start (up to 21.8 V), then pack + 1 V.

In sun

About 100–130 W (est.), roughly 7–9 A at 14.4 V.

Pack charge

2 A standard and 4 A maximum, from the cell datasheet.

Two sensors only

Skip #3 and derive I_batt = I_ESC − I_solar.

## Checks

- Cover the panels before mating or unplugging any MC4 connector, and never unplug under load.
- PV − goes to PV − only. Do not link it to BAT − or to ground, and do not ground the array.
- INA260 #2 VIN+ faces the MPPT, so solar current reads positive.
- With the branch joined and the panels covered, INA260 #2 reads about 0 A and the app shows no PV voltage.
- Mount the MPPT upright with the terminals down, 10 cm clear above and below, within 3 m of the pack.
- Torque BATT, PV and LOAD screws to 0.75 N·m and use flexible multi-strand copper.

## Build order

1. Install the libraries Adafruit INA260, HX711 (bogde), Servo and SD. Put the sketch in a folder named `phase1_logger_4s`.
2. INA260 #1: solder the headers and wire VCC, GND, SDA and SCL as shown. The address stays 0x40.
3. HX711 (Adafruit board): VIN to the 5 V rail, GND to the GND rail, DATA to D2, SCK to D3. Leave VIO and the RATE pin unconnected, and slide the RATE switch to L (10 SPS). Load cell to E+, E−, A− and A+ (red, black, white, green on typical cells, so check the label). If thrust later reads negative, swap A+ and A−.
4. SD module: six wires as shown. A bare module needs an on-board regulator and level shifter. A stackable shield may use a different chip-select pin.
5. ESC servo lead: signal to D9, ground to GND, and pull the red wire out of the plug and insulate it.
6. Power the Arduino from USB only. Nothing from the bus goes to it, and the barrel jack and VIN stay unused.
7. Phase 2: INA260 #2 gets the A0 solder bridge (0x41) and #3 gets A1 (0x44). Chain their SDA, SCL, 5 V and GND from #1.

Everything the Arduino talks to. The INA260 VIN+ and VIN− terminals are the power path and appear in the Phase 1 and Phase 2 drawings.

## Pin map

| Arduino pin | Goes to | Where on your board |
| --- | --- | --- |
| 5V | INA260 VCC (all), HX711 VIN, SD VCC | POWER header, left edge. Only one 5V pin, so feed a rail |
| GND | INA260 GND (all), HX711 GND, SD GND, ESC signal ground | POWER header (two GND pins) or the GND beside AREF, top right. Feed a rail |
| A4 (SDA) | INA260 SDA, chained to all sensors | ANALOG IN header, bottom left |
| A5 (SCL) | INA260 SCL, chained to all sensors | ANALOG IN header, bottom left |
| D2 / D3 | HX711 DATA / SCK | DIGITAL header, lower group (7 down to 0) |
| D9 | ESC signal | DIGITAL header, upper group (13 down to 8), marked \~9 |
| D10 / D11 / D12 / D13 | SD CS / MOSI / MISO / SCK | DIGITAL header, upper group |
| USB | Laptop: power, Serial Monitor at 115200, live rows and commands | USB-C, top edge |
| Not used | Nothing | Barrel jack, VIN, the ICSP header, the white 4-pin connector on the bottom edge, and the SDA/SCL pins beside AREF |

**Rails.** With all three INA260s fitted you need five 5 V and six GND connections (three and four in Phase 1), but the board has one 5V pin and three GND pins. Make one 5 V rail and one GND rail with a small breadboard or a Wago splice each, fed from one 5V pin and one GND pin. Keep a single GND rail, so the ESC signal ground stays the only link to the power negative.

**Do not use the white 4-pin connector.** On the genuine Uno R4 WiFi it is a second I2C bus (`Wire1`), which this sketch does not read, and I could not confirm that your Freenove board differs. Stay on A4 and A5.

## Load cell mounting

- Bolt the fixed end of the bar to a rigid base on spacers. The motor plate goes on the free end and nothing else touches it.
- Point the sensing arrow along the thrust direction.
- Keep the HX711 next to the cell, twist the four cell wires and route them at least 10 cm from the motor and ESC wires.
- Keep the prop at least 25 cm from walls and the table, with the airflow away from the cell and the wiring.

## Calibrate and log

1. With the motor mounted and no thrust, send `t` to tare.
2. Put a known mass on the cell in the thrust direction and send `c500` for 500 g.
3. Copy the printed `COUNTS_PER_G` into the sketch for later runs. Flip its sign if thrust reads negative.
4. Send `g` to start. Each power-up writes a new `RUNnn.CSV` to the card, and the Serial Monitor shows the same rows.

## CSV columns

| Column | Meaning |
| --- | --- |
| ms | Milliseconds since power-up |
| throttle_us | Throttle pulse in force when the row was sampled |
| bus_V, current_A, power_W | INA260 #1 on the ESC branch |
| energy_Wh | Energy delivered since the sweep started |
| net_counts, thrust_g | HX711 counts after tare, and thrust once calibrated (NA before) |

Lines starting with `#` are comments. The Phase 1 sketch reads one INA260. Phase 2 needs an updated sketch that also reads 0x41 and 0x44.

Two test cases. Test case 1 proves the wiring is safe and the software logs correctly, with no motor. Test case 2 brings in the ESC, motor, load cell and propeller. Do not start test case 2 until every pass box in test case 1 is ticked.

## What is plugged in at each stage

| Stage | Sketch | USB | Pack | Motor | Prop |
| --- | --- | --- | --- | --- | --- |
| 1A Software check | logger | Yes | No | No | No |
| 1B Smoke test | logger | Yes | Yes, through the bulb | No | No |
| 2A ESC setup | esc_setup | Yes | Yes, precharge lead then direct | Yes | No |
| 2B First spin | logger | Yes | Yes, precharge lead then direct | Yes | No |
| 2C Load cell | logger | Yes | No | Mounted | No |
| 2D Propeller sweep | logger | Yes | Yes, precharge lead then direct | Yes | Yes |

## Using the VS Code monitor

- **Select the sketch:** the environment name in the bottom bar (logger or esc_setup) decides what gets built. Click the check mark to build and the arrow to upload.
- **Open the monitor:** click the plug icon in the bottom bar. A terminal opens at 115200 baud. Whatever you type in it goes to the Arduino, one key at a time, with no Enter needed.
- **Close it before uploading:** the monitor holds the USB port, and an upload fails while it is open.
- **RESET:** press the red button to restart the sketch and see its boot text from the start. The monitor may drop and need reopening afterwards.
- **Backup copy of the data:** `pio device monitor --filter log2file` in the VS Code terminal also saves everything the Arduino prints to a log file in the project folder.

## Test case 1: smoke test and software check

### 1A. USB only, no pack, no motor

1. Check the SD card is FAT32 and in the module, the motor and ESC power are disconnected, and the pack is out.
2. With the logger environment selected, plug in USB, open the monitor and press RESET.
3. You should see this boot text:

```
# logging to RUN00.CSV
ms,throttle_us,bus_V,current_A,power_W,energy_Wh,net_counts,thrust_g
# ready. connect the pack through the precharge lead, then send g
# idle V=0.00 I=0.000 thrust_g=NA counts=3
```

4. Press gently on the load cell. The `counts=` value should move and settle when you let go.
5. Type `t`. It prints `# load cell tared` and the counts return to about 0.
6. Type `g`. With no pack this must refuse:

```
# NOT STARTED: bus voltage too low (pack flat or not connected)
```

7. Type `x`. It prints `# STOP: operator` and `# energy_Wh: 0.000`, and the file is closed. Unplug USB, take the SD card out and open it on the laptop. `RUN00.CSV` should exist and contain a `# cells=4 ...` comment line and the header.

**Pass:** the boot text appears with no halt message, the counts move, `g` is refused with no pack, and the CSV file is on the card.

**If it halts:** `INA260 not found` means SDA and SCL or its power. `SD init failed` means the card format or the CS, MOSI, MISO and SCK wires. `HX711 not responding` means DATA, SCK or its power. Nothing printing at all means the wrong port or baud rate. Fix that one thing, press RESET, and repeat.

### 1B. Smoke test with the bulb, motor still disconnected

1. Prop off and motor wires disconnected. Fit the bulb in series in the pack + lead, in place of the precharge lead, with the 15 A fuse still in the circuit. The bulb must be a 12 V car-type bulb. A mains bulb stays dark at 16.8 V even on a dead short, so it would prove nothing.
2. With USB connected, the monitor open and the board reset, connect the pack. The bulb should flash briefly while the capacitors charge, then go dark.
3. The idle line should now show the pack voltage and a tiny current:

```
# idle V=16.52 I=0.041 thrust_g=NA counts=3
```

4. Measure the bus voltage with the multimeter. It should agree with the idle line within about 0.05 V.
5. Type `g`. The sketch holds 1000 µs for 3 s, then steps the throttle signal 1200 to 2000 µs for 5 s each, about 48 s in all, and prints a row every 100 ms:

```
# arming, ESC held at 1000 us
41230,1200,16.512,0.041,0.68,0.0000,15,NA
...
# STOP: sequence complete
# energy_Wh: 0.009
```

6. Disconnect the pack, then USB, take the SD card out and open `RUN01.CSV`.

**Pass:** the bulb flashes and goes dark, the voltage matches your meter, the sweep reaches `sequence complete`, and the CSV has about 480 rows. `throttle_us` steps from 1000 to 2000, `ms` (time since the board booted) advances in steps of about 100, `bus_V` is steady, and `current_A` stays near zero.

**Unplug the pack at once if:** the bulb stays lit, the idle voltage reads about 0 with the pack connected, anything smells, or anything gets warm.

## Test case 2: ESC, motor, current and thrust

### 2A. ESC calibration and settings (once)

Select the esc_setup environment, upload, open the monitor, and follow the "ESC setup, once" section on the Phase 1 tab. Motor connected, prop off, pack through the precharge lead. You must hear four beeps and a long beep at the end. Then switch back to the logger environment and upload it.

### 2B. First spin, prop off

1. Motor connected to the ESC, prop off, motor clamped so it can't move. Logger uploaded, monitor open, board reset, `# ready` showing.
2. Touch the pack to the system input through the precharge lead for 2 to 3 s, then connect the XT60 directly. The ESC plays its start-up tone, four beeps and a long beep.
3. The idle line should show the pack voltage and about 0.0x A. Check the voltage against your meter.
4. Type `g`. After 3 s the motor starts on the first step. Watch the direction, the smoothness, and the `current` value rising with each step. Type `x` after a few steps, or let it complete. Once a run has ended the sketch does nothing more, so press RESET before the next run.
5. If the direction is wrong, disconnect the pack and swap any two of the three motor wires.

**Pass:** correct direction, smooth at every step, current rising with throttle and staying low with no prop (a few amps at most, my estimate), the bus voltage steady, and nothing getting hot.

### 2C. Load cell calibration (USB only, no pack)

1. Motor mounted on the load cell in its final position, prop off. Reset the board and type `t`.
2. Apply a known mass along the thrust direction, for example 500 g, and type `c500` quickly, because the sketch waits only 0.2 s for the number to follow the `c`. If it answers `# calibrate: give a positive mass in grams`, you typed too slowly: try again, or paste `c500` with a right-click in the terminal. It prints `# COUNTS_PER_G = ...`.
3. Remove the mass and apply a different known one, for example 200 g. The idle line's `thrust_g` should now read close to it.
4. Copy the printed value into `COUNTS_PER_G` in `src/logger/main.cpp` (keep a minus sign if it has one), then rebuild and upload. A reset wipes the RAM value, so skipping this step loses the calibration.

**Pass:** the second mass reads within a few percent, and the reading returns to about 0 when you remove it.

### 2D. Propeller sweep

1. Fit the 9×4.5E prop with the rig clamped, the guard in place, and your safety checklist done. Reset the board and confirm `# ready`.
2. Connect the pack through the precharge lead, then direct. Check the idle line.
3. Type `g`. The sweep ends by itself on `over current` (above 12 A), `under voltage` (below 12.0 V), `over thrust` (above 3000 g), or `sequence complete`. `x` stops it at any time.
4. Disconnect the pack straight afterwards and feel the ESC, motor and pack for heat. Then take the SD card out.
5. Write down the run file name, the idle voltage before the run, the prop and the ESC settings in your lab notes.

**Pass:** the sweep completes, or stops for a reason you can explain. Thrust and current both rise with each step, the current stays under 12 A (I estimated 5–8 A at full throttle), and the CSV is complete.

## Reading the data

Copy each `RUNnn.CSV` into the `data/` folder. Lines starting with `#` are comments, so in Python:

```
import pandas as pd
df = pd.read_csv("data/RUN03.CSV", comment="#")
```

Each reset creates a new file, numbered RUN00 to RUN99, so keep a note of which run was which test.